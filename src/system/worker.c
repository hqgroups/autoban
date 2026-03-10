#define _POSIX_C_SOURCE 200809L
#include "worker.h"
#include "queue.h"
#include "cache.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

static int check_global_rate_limit(AutobanContext *ctx) {
    static pthread_mutex_t rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;
    static int current_block_count = 0;
    static time_t last_reset_time = 0;

    int max_blocks = 0;
    if (ctx->config_mutex && ctx->config_ptr && *(ctx->config_ptr)) {
        pthread_mutex_lock(ctx->config_mutex);
        max_blocks = (*(ctx->config_ptr))->max_blocks_per_minute;
        pthread_mutex_unlock(ctx->config_mutex);
    }

    pthread_mutex_lock(&rate_limit_mutex);
    time_t now = time(NULL);
    if (now - last_reset_time >= 60) {
        current_block_count = 0;
        last_reset_time = now;
    }

    if (max_blocks > 0 && current_block_count >= max_blocks) {
        pthread_mutex_unlock(&rate_limit_mutex);
        fprintf(stderr, "[AutoBan] EMERGENCY: Đạt giới hạn Block toàn cầu (%d/phút)! Tạm thời ngưng Fork để bảo vệ CPU/RAM.\n", max_blocks);
        return 1;
    }

    current_block_count++;
    pthread_mutex_unlock(&rate_limit_mutex);
    return 0;
}

static int parse_command_args(char *cmd_copy, char **args, int max_args) {
    int arg_count = 0;
    char *token = strtok(cmd_copy, " ");
    while (token != NULL && arg_count < max_args - 1) {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;
    return arg_count;
}

static pid_t execute_block_command(char **args, int *pfd, const char *job_ip) {
    if (pipe(pfd) < 0) {
        perror("[AutoBan Worker] pipe");
        return -1;
    }

    pid_t pid = -1;
    int retries = 3;
    while (retries > 0) {
        pid = fork();
        if (pid == -1) {
            fprintf(stderr, "[AutoBan Worker] Lỗi FORK! Thử lại... (%d)\n", retries);
            sleep(1);
            retries--;
        } else {
            break;
        }
    }

    if (pid == 0) {
        close(pfd[0]);
        fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
        
        struct rlimit rl;
        rl.rlim_cur = 32; rl.rlim_max = 64;
        setrlimit(RLIMIT_NPROC, &rl);
        rl.rlim_cur = 10; rl.rlim_max = 20;
        setrlimit(RLIMIT_CPU, &rl);
        rl.rlim_cur = 64 * 1024 * 1024; rl.rlim_max = 128 * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);

        execvp(args[0], args);
        int err = errno;
        write(pfd[1], &err, sizeof(err));
        close(pfd[1]);
        _exit(127);
    }
    
    return pid;
}

static void publish_redis_sync(AutobanContext *ctx, const char *ip, redisContext **pub_rc_ptr, char *cached_r_host, int *cached_r_port) {
    int r_enabled = 0;
    char r_host[MAX_STRING] = "", r_pass[MAX_STRING] = "", r_chan[MAX_STRING] = "";
    int r_port = 6379;

    pthread_mutex_lock(ctx->config_mutex);
    if (*ctx->config_ptr) {
        r_enabled = (*ctx->config_ptr)->redis_enabled;
        r_port = (*ctx->config_ptr)->redis_port;
        strncpy(r_host, (*ctx->config_ptr)->redis_host, MAX_STRING - 1);
        r_host[MAX_STRING - 1] = '\0';
        strncpy(r_pass, (*ctx->config_ptr)->redis_password, MAX_STRING - 1);
        r_pass[MAX_STRING - 1] = '\0';
        strncpy(r_chan, (*ctx->config_ptr)->redis_channel, MAX_STRING - 1);
        r_chan[MAX_STRING - 1] = '\0';
    }
    pthread_mutex_unlock(ctx->config_mutex);

    if (!r_enabled || strlen(r_host) == 0) return;

    redisContext *pub_rc = *pub_rc_ptr;
    if (!pub_rc || strcmp(cached_r_host, r_host) != 0 || *cached_r_port != r_port) {
        if (pub_rc) redisFree(pub_rc);
        pub_rc = redisConnect(r_host, r_port);
        strcpy(cached_r_host, r_host);
        *cached_r_port = r_port;
        if (pub_rc && !pub_rc->err) {
            if (redisSetTimeout(pub_rc, (struct timeval){1, 0}) == REDIS_ERR) {
                fprintf(stderr, "[AutoBan Worker] WARNING: Không thể set timeout cho Redis, có thể Socket đã bị đóng.\n");
                redisFree(pub_rc);
                pub_rc = NULL;
            } else if (strlen(r_pass) > 0) {
                redisReply *auth_r = redisCommand(pub_rc, "AUTH %s", r_pass);
                if (auth_r) freeReplyObject(auth_r);
            }
        }
    }
    
    if (pub_rc && !pub_rc->err) {
        redisReply *pub_r = redisCommand(pub_rc, "PUBLISH %s %s", r_chan, ip);
        if (pub_r) freeReplyObject(pub_r);
        else pub_rc = NULL;
    } else if (pub_rc) {
        redisFree(pub_rc);
        pub_rc = NULL;
    }
    *pub_rc_ptr = pub_rc;
}

void *queue_worker_thread(void *arg) {
    AutobanContext *ctx = (AutobanContext *)arg;
    redisContext *pub_rc = NULL;
    char cached_r_host[MAX_STRING] = "";
    int cached_r_port = 0;

    while (1) {
        BlockJob job;
        if (!queue_dequeue_blocking(&job, ctx->worker_shutdown)) break;
        if (check_global_rate_limit(ctx)) continue;

        char cmd_copy[CMD_BUF_SIZE];
        strncpy(cmd_copy, job.cmd, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        char *args[32];
        if (parse_command_args(cmd_copy, args, 32) == 0) continue;

        int pfd[2] = {-1, -1};
        pid_t pid = execute_block_command(args, pfd, job.ip);

        if (pid == -1) {
            fprintf(stderr, "[AutoBan Worker] FATAL: Hết số lần thử. Cân nhắc đưa job %s trở lại queue.\n", job.ip);
            if (pfd[0] >= 0) { close(pfd[0]); close(pfd[1]); }
            if (!queue_enqueue(&job)) {
                fprintf(stderr, "[AutoBan Worker] CRITICAL: Hàng đợi Data Queue hiện rỗng/đầy, Job Event IP %s đã BỊ DROP vĩnh viễn!\n", job.ip);
            }
            continue;
        }

        close(pfd[1]);
        int err = 0;
        int n = read(pfd[0], &err, sizeof(err));
        close(pfd[0]);

        if (n == 0) {
            printf("[AutoBan Worker] Thành công: Đã thi hành lệnh chặn IP %s (Site: %s)\n", job.ip, job.server_name);
            cache_mark_blocked(job.ip, job.server_name);
            if (job.is_synced == 0) {
                publish_redis_sync(ctx, job.ip, &pub_rc, cached_r_host, &cached_r_port);
            }
        } else if (n > 0) {
            fprintf(stderr, "[AutoBan Worker] Lỗi thực thi lệnh '%s' (IP %s): %s\n", args[0], job.ip, strerror(err));
        }
    }

    if (pub_rc) redisFree(pub_rc);
    return NULL;
}
