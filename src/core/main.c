#define _POSIX_C_SOURCE 200809L
#include "ban_hash.h"
#include "cache.h"
#include "common.h"
#include "config.h"
#include "control_socket.h"
#include "ipset.h"
#include "journal.h"
#include "nginx_sync.h"
#include "queue.h"
#include "redis_sync.h"
#include "worker.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t reload_config_flag = 0;
static volatile sig_atomic_t dump_metrics_flag = 0;
static volatile int worker_shutdown = 0;

static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
static AppConfig *config = NULL;

static void on_sigchld(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {}
}

static void on_signal(int sig) {
    if (sig == SIGHUP)
        reload_config_flag = 1;
    else if (sig == SIGUSR1)
        dump_metrics_flag = 1;
    else
        running = 0;
}

static void handle_config_reload(const char *config_file) {
    sd_notify(0, "RELOADING=1\nSTATUS=Reloading config...");
    printf("[AutoBan] Nhận tín hiệu SIGHUP, đang tải lại cấu hình %s...\n", config_file);

    AppConfig *new_config = malloc(sizeof(AppConfig));
    if (new_config && load_config(config_file, new_config) == 0) {
        nginx_sync_generate_config(new_config); // Generate Nginx config for the new config
        pthread_mutex_lock(&config_mutex);
        AppConfig *old_config = config;
        config = new_config;
        pthread_mutex_unlock(&config_mutex);

        if (new_config->cache_size > 0 && new_config->cache_size != cache_get_size()) {
            if (cache_resize(new_config->cache_size))
                printf("[AutoBan] Đã cập nhật cache_size thành %d.\n", cache_get_size());
            else
                fprintf(stderr, "[AutoBan] Lỗi realloc cache, giữ nguyên cache_size %d.\n",
                        cache_get_size());
        }

        free_config(old_config);
        free(old_config);
        printf("[AutoBan] Tải lại cấu hình thành công.\n");
        sd_notify(0, "READY=1\nSTATUS=AutoBan is running");
    } else {
        if (new_config)
            free(new_config);
        fprintf(stderr, "[AutoBan] Lỗi: Cấu hình mới không hợp lệ! Giữ cấu hình cũ.\n");
    }
}

static void dump_status_metrics(void) {
    printf("[AutoBan Metrics] Status Dump:\n");
    printf("  - Total Blocks Executed: %d\n", cache_get_metrics_total_blocks());
    printf("  - Cache Hits (Debounce): %d\n", cache_get_metrics_cache_hits());
    printf("  - Current Queue Length : %d/%d\n", queue_get_count(), MAX_QUEUE_SIZE);
}

static void process_journal_entry(sd_journal *j) {
    const void *data;
    size_t length;
    if (sd_journal_get_data(j, "MESSAGE", &data, &length) == 0) {
        if (length <= 8)
            return;
        size_t msg_len = length - 8;
        char msg_buf[8192];
        if (msg_len >= sizeof(msg_buf))
            msg_len = sizeof(msg_buf) - 1;
        memcpy(msg_buf, (const char *)data + 8, msg_len);
        msg_buf[msg_len] = '\0';

        pthread_mutex_lock(&config_mutex);
        parse_nginx_log(config, msg_buf);
        pthread_mutex_unlock(&config_mutex);
    }
}

int main(int argc, char *argv[]) {
    config = malloc(sizeof(AppConfig));
    if (!config) {
        fprintf(stderr, "Lỗi cấp phát bộ nhớ cho cấu hình. Thoát.\n");
        return 1;
    }

    const char *config_file = "/etc/autoban/autoban.conf";
    if (argc > 1)
        config_file = argv[1];

    if (load_config(config_file, config) != 0) {
        fprintf(stderr, "Cảnh báo: Lỗi khi đọc file config %s. Hệ thống chạy với mặc định.\n",
                config_file);
    }
    nginx_sync_generate_config(config);

    int cache_sz = config->cache_size > 0 ? config->cache_size : 256;
    if (cache_init(cache_sz) != 0) {
        fprintf(stderr, "Lỗi khởi tạo cache. Thoát.\n");
        free_config(config);
        free(config);
        return 1;
    }

    queue_init();
    ipset_init();

    printf("====== AutoBan Service Khởi Động ======\n");
    printf("- Cấu hình nạp từ   : %s\n", config_file);
    printf("- Format Lệnh Chặn  : %s\n", config->block_cmd);
    printf("- Hết thời hạn chặn : %d giây (Global)\n", config->default_ban_time);
    printf("- Tổng số Custom Host: %d\n", config->site_count);
    printf("- Tổng số Whitelist : %d IP(s)\n", config->whitelist_count);
    printf("- Dynamic Cache Size: %d\n", cache_get_size());
    printf("- Pthread Worker ID : Bật (%d Luồng)\n", WORKER_THREADS_DEFAULT);

    AutobanContext ctx = {
        .config_ptr = &config,
        .config_mutex = &config_mutex,
        .running = &running,
        .reload_config_flag = &reload_config_flag,
        .dump_metrics_flag = &dump_metrics_flag,
        .worker_shutdown = &worker_shutdown,
    };

    pthread_t worker_tids[WORKER_THREADS_DEFAULT];
    for (int i = 0; i < WORKER_THREADS_DEFAULT; i++) {
        if (pthread_create(&worker_tids[i], NULL, queue_worker_thread, &ctx) != 0) {
            fprintf(stderr, "Không tạo được worker thread %d. Thoát.\n", i);
            cache_free();
            free_config(config);
            free(config);
            return 1;
        }
    }

    pthread_t control_tid;
    pthread_create(&control_tid, NULL, control_socket_thread, &ctx);
    pthread_detach(control_tid);

    pthread_t redis_sub_tid;
    pthread_create(&redis_sub_tid, NULL, redis_subscriber_thread, &ctx);
    pthread_detach(redis_sub_tid);

    sd_journal *j;
    int r = sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);
    if (r < 0) {
        fprintf(stderr, "Không mở được sd-journal: %s\n",
                (-r > 0 && -r < 256) ? strerror(-r) : strerror(errno));
        worker_shutdown = 1;
        queue_wake_waiters();
        for (int i = 0; i < WORKER_THREADS_DEFAULT; i++)
            pthread_join(worker_tids[i], NULL);
        cache_free();
        free_config(config);
        free(config);
        return 1;
    }

    if (config->journal_match_count > 0) {
        for (int i = 0; i < config->journal_match_count; i++) {
            char match[MAX_STRING];
            snprintf(match, sizeof(match), "SYSLOG_IDENTIFIER=%s", config->journal_matches[i]);
            sd_journal_add_match(j, match, 0);
            printf("[AutoBan] Subscribed to journal match: %s\n", match);
        }
    } else {
        sd_journal_add_match(j, "SYSLOG_IDENTIFIER=nginx_error", 0);
        printf("[AutoBan] Subscribed to default journal match: SYSLOG_IDENTIFIER=nginx_error\n");
    }

    sd_journal_seek_tail(j);
    sd_journal_previous(j);

    struct sigaction sa;
    sa.sa_handler = on_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, 0);
    signal(SIGHUP, on_signal);
    signal(SIGUSR1, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    sd_notify(0, "READY=1\nSTATUS=AutoBan is running");

    time_t last_cleanup = time(NULL);

    while (running) {
        if (reload_config_flag) {
            reload_config_flag = 0;
            handle_config_reload(config_file);
        }

        if (dump_metrics_flag) {
            dump_metrics_flag = 0;
            dump_status_metrics();
        }

        time_t now = time(NULL);
        if (now - last_cleanup > 3600) {
            ban_cleanup(config->ban_time_window > 0 ? config->ban_time_window : 86400);
            last_cleanup = now;
        }

        r = sd_journal_next(j);
        if (r < 0) {
            fprintf(stderr, "Lỗi đọc sd-journal: %s\n",
                    (-r > 0 && -r < 256) ? strerror(-r) : strerror(errno));
            break;
        }
        if (r == 0) {
            sd_journal_wait(j, 1000000);
            continue;
        }

        process_journal_entry(j);
    }

    sd_notify(0, "STOPPING=1\nSTATUS=Shutting down process...");

    worker_shutdown = 1;
    queue_wake_waiters();
    for (int i = 0; i < WORKER_THREADS_DEFAULT; i++)
        pthread_join(worker_tids[i], NULL);

    sd_journal_close(j);
    cache_free();
    free_config(config);
    free(config);
    printf("[AutoBan] Đã thoát.\n");
    return 0;
}
