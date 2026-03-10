#define _POSIX_C_SOURCE 200809L
#include "journal.h"
#include "block.h"
#include "common.h"
#include <regex.h>
#include <stdio.h>
#include <string.h>

static int extract_regex_match(AppConfig *config, const char *message, char *ip, size_t ip_max, char *server_name, size_t srv_max) {
    regmatch_t pmatch[MAX_REGEX_GROUPS];
    if (regexec(&config->log_regex, message, MAX_REGEX_GROUPS, pmatch, 0) != 0) return 0;
    
    int ip_idx = config->ip_group_idx;
    int srv_idx = config->server_group_idx;
    
    if (ip_idx > 0 && ip_idx < MAX_REGEX_GROUPS && pmatch[ip_idx].rm_so != -1) {
        snprintf(ip, ip_max, "%.*s", (int)(pmatch[ip_idx].rm_eo - pmatch[ip_idx].rm_so), message + pmatch[ip_idx].rm_so);
    }
    if (srv_idx > 0 && srv_idx < MAX_REGEX_GROUPS && pmatch[srv_idx].rm_so != -1) {
        snprintf(server_name, srv_max, "%.*s", (int)(pmatch[srv_idx].rm_eo - pmatch[srv_idx].rm_so), message + pmatch[srv_idx].rm_so);
    }
    return 1;
}

static int extract_manual_match(const char *message, char *ip, size_t ip_max, char *server_name, size_t srv_max) {
    if (!strstr(message, "limiting requests")) return 0;
    
    const char *client_ptr = strstr(message, "client: ");
    if (client_ptr) {
        client_ptr += 8;
        const char *comma = strchr(client_ptr, ',');
        snprintf(ip, ip_max, "%.*s", comma ? (int)(comma - client_ptr) : (int)strlen(client_ptr), client_ptr);
    }
    
    const char *server_ptr = strstr(message, "server: ");
    if (server_ptr) {
        server_ptr += 8;
        const char *comma = strchr(server_ptr, ',');
        snprintf(server_name, srv_max, "%.*s", comma ? (int)(comma - server_ptr) : (int)strlen(server_ptr), server_ptr);
    }
    return 1;
}

void parse_nginx_log(AppConfig *config, const char *message) {
    char ip[IP_BUF_SIZE] = {0};
    char server_name[SERVER_NAME_BUF_SIZE] = {0};
    int matched = 0;

    if (config->regex_compiled) {
        matched = extract_regex_match(config, message, ip, sizeof(ip), server_name, sizeof(server_name));
    } else {
        matched = extract_manual_match(message, ip, sizeof(ip), server_name, sizeof(server_name));
    }

    if (matched && strlen(ip) > 0 && strlen(server_name) > 0) {
        block_ip(config, ip, server_name, 0);
    }
}
