#define _POSIX_C_SOURCE 200809L
#include "block.h"
#include "queue.h"
#include "cache.h"
#include "ban_hash.h"
#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int block_is_valid_ip(const char *ip) {
    struct sockaddr_in sa;
    struct sockaddr_in6 sa6;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0 ||
           inet_pton(AF_INET6, ip, &(sa6.sin6_addr)) != 0;
}

static const char* determine_block_cmd(AppConfig *config, const char *ip, const char *server_name, int offense_count) {
    int is_v6 = (strchr(ip, ':') != NULL);
    int retries = config->max_ban_retries > 0 ? config->max_ban_retries : 3;
    
    if (offense_count >= retries && (strlen(config->permanent_ban_cmd) > 0 || strlen(config->permanent_ban_cmd_v6) > 0)) {
        printf("[AutoBan] Multi-Level Triggered! IP %s đã vi phạm %d lần. Áp dụng lệnh nâng cao.\n", ip, offense_count);
        const char *cmd = (is_v6 && strlen(config->permanent_ban_cmd_v6) > 0) ? config->permanent_ban_cmd_v6 : config->permanent_ban_cmd;
        if (strlen(cmd) == 0 && strlen(config->permanent_ban_cmd) > 0) cmd = config->permanent_ban_cmd;
        return cmd;
    }
    
    return is_v6 ? get_block_cmd_v6_for_site(config, server_name) 
                 : get_block_cmd_for_site(config, server_name);
}

static int build_job_command(char *dest, size_t dest_size, const char *cmd_template, const char *ip, int ban_time) {
    int written = snprintf(dest, dest_size, cmd_template, ip, ban_time);
    if (written < 0 || written >= (int)dest_size) {
        fprintf(stderr, "[AutoBan] WARNING: Lệnh chặn vượt quá độ dài buffer. Bỏ qua chặn %s.\n", ip);
        return -1;
    }
    return 0;
}

void block_ip(AppConfig *config, const char *ip, const char *server_name, int is_synced) {
    if (!block_is_valid_ip(ip)) {
        fprintf(stderr, "[AutoBan] SECURITY WARNING: IP Parse Error hoặc mã độc detected '%s'. Không ban.\n", ip);
        return;
    }
    
    if (is_whitelisted(config, ip)) return;
    
    if (cache_is_recently_blocked(ip, server_name)) {
        cache_inc_cache_hits();
        return;
    }

    int ban_time = get_ban_time_for_site(config, server_name);
    int window = config->ban_time_window > 0 ? config->ban_time_window : 86400;
    int offense_count = ban_record_and_get_count(ip, window);

    const char *block_cmd = determine_block_cmd(config, ip, server_name, offense_count);

    BlockJob job;
    if (build_job_command(job.cmd, sizeof(job.cmd), block_cmd, ip, ban_time) < 0) return;

    strncpy(job.ip, ip, IP_BUF_SIZE - 1);
    job.ip[IP_BUF_SIZE - 1] = '\0';
    strncpy(job.server_name, server_name, SERVER_NAME_BUF_SIZE - 1);
    job.server_name[SERVER_NAME_BUF_SIZE - 1] = '\0';
    job.is_synced = is_synced;

    if (!queue_enqueue(&job)) {
        fprintf(stderr, "[AutoBan] QUÁ TẢI CỤC BỘ: Drop IP %s do Queue Full!\n", ip);
    }
}
