#define _POSIX_C_SOURCE 200809L
#include "control_socket.h"
#include "cache.h"
#include "queue.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void get_socket_path(AutobanContext *ctx, char *current_path) {
    pthread_mutex_lock(ctx->config_mutex);
    if (*ctx->config_ptr) {
        strncpy(current_path, (*ctx->config_ptr)->control_socket_path, MAX_PATH - 1);
        current_path[MAX_PATH - 1] = '\0';
    } else {
        current_path[0] = '\0';
    }
    pthread_mutex_unlock(ctx->config_mutex);
}

static int initialize_unix_socket(const char *socket_path) {
    unlink(socket_path);
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        if (listen(server_fd, 3) == 0) {
            if (chmod(socket_path, 0600) != 0) {
                perror("[AutoBan] ERROR: Không thể thiết lập quyền 0600 cho Control Socket");
                close(server_fd);
                unlink(socket_path);
                return -1;
            }
            printf("[AutoBan] Unix Control Socket đã mở tại: %s (Permissions: 0600)\n",
                   socket_path);
            return server_fd;
        }
    }
    close(server_fd);
    return -1;
}

static void handle_client_connection(AutobanContext *ctx, int server_fd) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0)
        return;

    char buf[256];
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *nl = strchr(buf, '\n');
        if (nl)
            *nl = '\0';
        nl = strchr(buf, '\r');
        if (nl)
            *nl = '\0';

        char resp[1024] = {0};
        if (strcmp(buf, "status") == 0) {
            int w_count = 0, s_count = 0;
            pthread_mutex_lock(ctx->config_mutex);
            if (*ctx->config_ptr) {
                w_count = (*ctx->config_ptr)->whitelist_count;
                s_count = (*ctx->config_ptr)->site_count;
            }
            pthread_mutex_unlock(ctx->config_mutex);
            int q_len = queue_get_count();
            snprintf(resp, sizeof(resp),
                     "AutoBan Status (v%s):\n"
                     "  Total Blocks : %d\n"
                     "  Cache Hits   : %d\n"
                     "  Queue Len    : %d/%d\n"
                     "  Whitelist IPs: %d\n"
                     "  Sites        : %d\n",
                     AUTOBAN_VERSION, cache_get_metrics_total_blocks(),
                     cache_get_metrics_cache_hits(), q_len, MAX_QUEUE_SIZE, w_count, s_count);
        } else if (strcmp(buf, "reload") == 0) {
            *ctx->reload_config_flag = 1;
            snprintf(resp, sizeof(resp), "Signal SIGHUP sent for reload.\n");
        } else if (strcmp(buf, "flush") == 0) {
            cache_flush();
            snprintf(resp, sizeof(resp), "Cache flushed.\n");
        } else {
            snprintf(resp, sizeof(resp), "Unknown command. Use: status, reload, flush\n");
        }
        write(client_fd, resp, strlen(resp));
    }
    close(client_fd);
}

void *control_socket_thread(void *arg) {
    AutobanContext *ctx = (AutobanContext *)arg;
    char socket_path[MAX_PATH] = "";
    int server_fd = -1;

    while (*ctx->running) {
        char current_path[MAX_PATH];
        get_socket_path(ctx, current_path);

        if (strcmp(socket_path, current_path) != 0) {
            if (server_fd >= 0) {
                close(server_fd);
                unlink(socket_path);
            }
            strncpy(socket_path, current_path, MAX_PATH - 1);
            socket_path[MAX_PATH - 1] = '\0';

            server_fd = initialize_unix_socket(socket_path);
        }

        if (server_fd < 0) {
            sleep(1);
            continue;
        }

        struct pollfd pfd;
        pfd.fd = server_fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 1000);

        if (ret > 0 && (pfd.revents & POLLIN)) {
            handle_client_connection(ctx, server_fd);
        }
    }

    if (server_fd >= 0) {
        close(server_fd);
        unlink(socket_path);
    }
    return NULL;
}
