#define _POSIX_C_SOURCE 200809L
#include "redis_sync.h"
#include "block.h"
#include <errno.h>
#include <hiredis/hiredis.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void load_redis_config(AutobanContext *ctx, int *enabled, char *host, int *port, char *password, char *channel) {
    pthread_mutex_lock(ctx->config_mutex);
    if (*ctx->config_ptr) {
        *enabled = (*ctx->config_ptr)->redis_enabled;
        *port = (*ctx->config_ptr)->redis_port;
        strncpy(host, (*ctx->config_ptr)->redis_host, MAX_STRING - 1); host[MAX_STRING - 1] = '\0';
        strncpy(password, (*ctx->config_ptr)->redis_password, MAX_STRING - 1); password[MAX_STRING - 1] = '\0';
        strncpy(channel, (*ctx->config_ptr)->redis_channel, MAX_STRING - 1); channel[MAX_STRING - 1] = '\0';
    } else {
        *enabled = 0; host[0] = password[0] = channel[0] = '\0'; *port = 6379;
    }
    pthread_mutex_unlock(ctx->config_mutex);
}

static redisContext* connect_to_redis(const char *host, int port, const char *password) {
    redisContext *c = redisConnect(host, port);
    if (c == NULL || c->err) {
        fprintf(stderr, "[AutoBan Redis] Lỗi kết nối Subscriber tới Redis %s:%d. Thử lại sau 5s...\n", host, port);
        if (c) redisFree(c);
        return NULL;
    }
    struct timeval tv = {1, 0};
    redisSetTimeout(c, tv);
    if (strlen(password) > 0) {
        redisReply *reply = redisCommand(c, "AUTH %s", password);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            fprintf(stderr, "[AutoBan Redis] Lỗi xác thực Redis Subscriber.\n");
            if (reply) freeReplyObject(reply);
            redisFree(c);
            return NULL;
        }
        freeReplyObject(reply);
    }
    return c;
}

static void handle_redis_message(AutobanContext *ctx, redisReply *r) {
    if (r->type == REDIS_REPLY_ARRAY && r->elements == 3) {
        if (r->element[0]->type == REDIS_REPLY_STRING && strcmp(r->element[0]->str, "message") == 0) {
            char *synced_ip = r->element[2]->str;
            pthread_mutex_lock(ctx->config_mutex);
            if (*ctx->config_ptr) {
                block_ip(*ctx->config_ptr, synced_ip, "synced_global", 1);
            }
            pthread_mutex_unlock(ctx->config_mutex);
        }
    }
}

void *redis_subscriber_thread(void *arg) {
    AutobanContext *ctx = (AutobanContext *)arg;

    while (*ctx->running) {
        int enabled, port;
        char host[MAX_STRING], password[MAX_STRING], channel[MAX_STRING];
        
        load_redis_config(ctx, &enabled, host, &port, password, channel);
        if (!enabled || strlen(host) == 0) {
            sleep(5);
            continue;
        }

        redisContext *c = connect_to_redis(host, port, password);
        if (!c) {
            sleep(5);
            continue;
        }

        printf("[AutoBan Redis] Đã kết nối Distributed Sync (Channel: %s)\n", channel);
        redisReply *reply = redisCommand(c, "SUBSCRIBE %s", channel);
        if (reply) freeReplyObject(reply);

        while (*ctx->running) {
            int cur_enabled, cur_port;
            char cur_host[MAX_STRING], cur_password[MAX_STRING], cur_channel[MAX_STRING];
            load_redis_config(ctx, &cur_enabled, cur_host, &cur_port, cur_password, cur_channel);

            if (!cur_enabled || strcmp(host, cur_host) != 0 || port != cur_port)
                break;

            void *r_raw = NULL;
            int ret = redisGetReply(c, &r_raw);
            if (ret == REDIS_OK && r_raw != NULL) {
                handle_redis_message(ctx, (redisReply *)r_raw);
                freeReplyObject((redisReply *)r_raw);
            } else if (ret == REDIS_ERR) {
                if (c->err == REDIS_ERR_IO && (errno == EAGAIN || errno == EWOULDBLOCK))
                    continue;
                fprintf(stderr, "[AutoBan Redis] Mất kết nối Subscriber. Đang reconnect...\n");
                break;
            }
        }
        redisFree(c);
    }
    return NULL;
}
