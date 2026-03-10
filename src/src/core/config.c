#include "config.h"
#include "config_parser.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int compare_whitelist(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int is_dangerous_cmd(const char *cmd) {
    if (cmd[0] != '/') {
        fprintf(stderr,
                "[AutoBan] SECURITY: Lệnh '%s' phải bắt đầu bằng đường dẫn tuyệt đối (ví dụ "
                "/usr/sbin/ipset)\n",
                cmd);
        return 1;
    }

    if (strstr(cmd, "rm ") || strstr(cmd, "mkfs") || strstr(cmd, "mv ") || strstr(cmd, "cp ") ||
        strstr(cmd, "chmod ") || strstr(cmd, "chown") || strstr(cmd, ">") || strstr(cmd, ">>") ||
        strstr(cmd, "|") || strstr(cmd, "&") || strstr(cmd, ";"))
        return 1;

    if (strstr(cmd, "wget") || strstr(cmd, "curl") || strstr(cmd, "base64") || strstr(cmd, "nc ") ||
        strstr(cmd, " telnet"))
        return 1;
    return 0;
}
static int validate_template(const char *template) {
    if (strstr(template, "%s") == NULL || strstr(template, "%d") == NULL) {
        return 0;
    }

    char tmp[MAX_STRING];
    strncpy(tmp, template, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *cmd_start = tmp;
    if (tmp[0] == '"' || tmp[0] == '\'') {
        char quote = tmp[0];
        cmd_start++;
        char *end_quote = strchr(cmd_start, quote);
        if (end_quote)
            *end_quote = '\0';
    } else {
        char *space = strchr(tmp, ' ');
        if (space)
            *space = '\0';
    }

    if (strlen(cmd_start) > 0) {
        if (access(cmd_start, X_OK) != 0) {
            fprintf(stderr,
                    "[AutoBan] SECURITY ERROR: Lệnh thực thi '%s' không tồn tại hoặc không có "
                    "quyền Execute.\n",
                    cmd_start);
            return 0;
        }
    }

    return 1;
}

static void handle_global_block_cmd(AppConfig *config, const char *val) {
    if (is_dangerous_cmd(val)) {
        fprintf(stderr, "[AutoBan] SECURITY WARNING: Lệnh block_cmd cấu hình toàn cầu có chứa "
                        "pattern nguy hiểm. Bỏ qua.\n");
    } else if (validate_template(val)) {
        strncpy(config->block_cmd, val, MAX_STRING - 1);
        config->block_cmd[MAX_STRING - 1] = '\0';
    } else {
        fprintf(stderr,
                "[AutoBan] WARNING: Lệnh block_cmd thiếu %%s, %%d hoặc định dạng sai. Bỏ qua.\n");
    }
}

static void handle_global_block_cmd_v6(AppConfig *config, const char *val) {
    if (is_dangerous_cmd(val)) {
        fprintf(stderr, "[AutoBan] SECURITY WARNING: Lệnh block_cmd_v6 cấu hình toàn cầu có chứa "
                        "pattern nguy hiểm. Bỏ qua.\n");
    } else if (validate_template(val)) {
        strncpy(config->block_cmd_v6, val, MAX_STRING - 1);
        config->block_cmd_v6[MAX_STRING - 1] = '\0';
    } else {
        fprintf(
            stderr,
            "[AutoBan] WARNING: Lệnh block_cmd_v6 thiếu %%s, %%d hoặc định dạng sai. Bỏ qua.\n");
    }
}

static void handle_global_journal(AppConfig *config, const char *val) {
    char val_copy[1024];
    strncpy(val_copy, val, sizeof(val_copy) - 1);
    val_copy[sizeof(val_copy) - 1] = '\0';
    char *token = strtok(val_copy, ",");
    while (token != NULL && config->journal_match_count < MAX_JOURNAL_MATCHES) {
        while (*token == ' ')
            token++;
        strncpy(config->journal_matches[config->journal_match_count], token, MAX_STRING - 1);
        config->journal_matches[config->journal_match_count][MAX_STRING - 1] = '\0';
        config->journal_match_count++;
        token = strtok(NULL, ",");
    }
}

static const ConfigHandler global_handlers[] = {
    {"permanent_ban_cmd", TYPE_STRING, offsetof(AppConfig, permanent_ban_cmd), MAX_STRING, NULL},
    {"permanent_ban_cmd_v6", TYPE_STRING, offsetof(AppConfig, permanent_ban_cmd_v6), MAX_STRING,
     NULL},
    {"min_ban_threshold", TYPE_INT, offsetof(AppConfig, min_ban_threshold), 0, NULL},
    {"max_ban_retries", TYPE_INT, offsetof(AppConfig, max_ban_retries), 0, NULL},
    {"ban_time_window", TYPE_INT, offsetof(AppConfig, ban_time_window), 0, NULL},
    {"default_ban_time", TYPE_INT, offsetof(AppConfig, default_ban_time), 0, NULL},
    {"cache_size", TYPE_INT, offsetof(AppConfig, cache_size), 0, NULL},
    {"max_blocks_per_minute", TYPE_INT, offsetof(AppConfig, max_blocks_per_minute), 0, NULL},
    {"log_pattern", TYPE_STRING, offsetof(AppConfig, log_pattern), MAX_STRING, NULL},
    {"ip_group_idx", TYPE_INT, offsetof(AppConfig, ip_group_idx), 0, NULL},
    {"server_group_idx", TYPE_INT, offsetof(AppConfig, server_group_idx), 0, NULL},
    {"control_socket", TYPE_STRING, offsetof(AppConfig, control_socket_path), MAX_PATH, NULL},
    {"block_cmd", TYPE_CUSTOM, 0, 0, handle_global_block_cmd},
    {"block_cmd_v6", TYPE_CUSTOM, 0, 0, handle_global_block_cmd_v6},
    {"journal_match", TYPE_CUSTOM, 0, 0, handle_global_journal},
    {NULL, 0, 0, 0, NULL}};

static const ConfigHandler redis_handlers[] = {
    {"enabled", TYPE_INT, offsetof(AppConfig, redis_enabled), 0, NULL},
    {"host", TYPE_STRING, offsetof(AppConfig, redis_host), MAX_STRING, NULL},
    {"port", TYPE_INT, offsetof(AppConfig, redis_port), 0, NULL},
    {"password", TYPE_STRING, offsetof(AppConfig, redis_password), MAX_STRING, NULL},
    {"channel", TYPE_STRING, offsetof(AppConfig, redis_channel), MAX_STRING, NULL},
    {NULL, 0, 0, 0, NULL}};

static const ConfigHandler tele_handlers[] = {
    {"enabled", TYPE_INT, offsetof(AppConfig, tele_enabled), 0, NULL},
    {"token", TYPE_STRING, offsetof(AppConfig, tele_token), MAX_STRING, NULL},
    {"chat_id", TYPE_STRING, offsetof(AppConfig, tele_chat_id), MAX_STRING, NULL},
    {NULL, 0, 0, 0, NULL}};

static void process_handlers(AppConfig *config, const ConfigHandler *handlers, const char *key,
                             const char *val) {
    for (int i = 0; handlers[i].key != NULL; i++) {
        if (strcmp(key, handlers[i].key) == 0) {
            if (handlers[i].type == TYPE_INT) {
                int *ptr = (int *)((char *)config + handlers[i].offset);
                *ptr = atoi(val);
            } else if (handlers[i].type == TYPE_STRING) {
                char *ptr = (char *)((char *)config + handlers[i].offset);
                strncpy(ptr, val, handlers[i].max_len - 1);
                ptr[handlers[i].max_len - 1] = '\0';
            } else if (handlers[i].type == TYPE_CUSTOM && handlers[i].custom_handler) {
                handlers[i].custom_handler(config, val);
            }
            break;
        }
    }
}

static void handle_site_block_cmd(SiteConfig *config, const char *server_name, const char *val) {
    if (is_dangerous_cmd(val)) {
        fprintf(stderr,
                "[AutoBan] SECURITY WARNING: Lệnh block_cmd của site %s chứa từ ngữ cấm. Bỏ qua.\n",
                server_name);
    } else if (validate_template(val)) {
        strncpy(config->block_cmd, val, MAX_STRING - 1);
        config->block_cmd[MAX_STRING - 1] = '\0';
    } else {
        fprintf(stderr,
                "[AutoBan] WARNING: Lệnh block_cmd site %s thiếu %%s, %%d hoặc định dạng sai. Bỏ "
                "qua.\n",
                server_name);
    }
}

static void handle_site_block_cmd_v6(SiteConfig *config, const char *server_name, const char *val) {
    if (is_dangerous_cmd(val)) {
        fprintf(
            stderr,
            "[AutoBan] SECURITY WARNING: Lệnh block_cmd_v6 của site %s chứa từ ngữ cấm. Bỏ qua.\n",
            server_name);
    } else if (validate_template(val)) {
        strncpy(config->block_cmd_v6, val, MAX_STRING - 1);
        config->block_cmd_v6[MAX_STRING - 1] = '\0';
    } else {
        fprintf(stderr,
                "[AutoBan] WARNING: Lệnh block_cmd_v6 site %s thiếu %%s, %%d hoặc định dạng sai. "
                "Bỏ qua.\n",
                server_name);
    }
}

static const SiteConfigHandler site_handlers[] = {
    {"ban_time", TYPE_INT, offsetof(SiteConfig, ban_time_seconds), 0, NULL},
    {"block_cmd", TYPE_CUSTOM, 0, 0, handle_site_block_cmd},
    {"block_cmd_v6", TYPE_CUSTOM, 0, 0, handle_site_block_cmd_v6},
    {NULL, 0, 0, 0, NULL}};

static void process_site_handlers(SiteConfig *config, const char *server_name,
                                  const SiteConfigHandler *handlers, const char *key,
                                  const char *val) {
    for (int i = 0; handlers[i].key != NULL; i++) {
        if (strcmp(key, handlers[i].key) == 0) {
            if (handlers[i].type == TYPE_INT) {
                int *ptr = (int *)((char *)config + handlers[i].offset);
                *ptr = atoi(val);
            } else if (handlers[i].type == TYPE_STRING) {
                char *ptr = (char *)((char *)config + handlers[i].offset);
                strncpy(ptr, val, handlers[i].max_len - 1);
                ptr[handlers[i].max_len - 1] = '\0';
            } else if (handlers[i].type == TYPE_CUSTOM && handlers[i].custom_handler) {
                handlers[i].custom_handler(config, server_name, val);
            }
            break;
        }
    }
}

static void set_default_config(AppConfig *config) {
    strcpy(config->block_cmd, "/usr/sbin/ipset add autoban_list %s timeout %d");
    strcpy(config->block_cmd_v6, "/usr/sbin/ipset add autoban_list_v6 %s timeout %d");
    config->default_ban_time = 3600;
    config->cache_size = 256;
    config->max_blocks_per_minute = 1000;
    config->regex_compiled = 0;
    config->log_pattern[0] = '\0';
    config->ip_group_idx = 1;
    config->server_group_idx = 2;
    config->journal_match_count = 0;
    strncpy(config->control_socket_path, "/run/autoban/autoban.sock", MAX_PATH - 1);
    config->control_socket_path[MAX_PATH - 1] = '\0';

    config->min_ban_threshold = 1;
    config->max_ban_retries = 3;
    config->ban_time_window = 86400;
    config->permanent_ban_cmd[0] = '\0';
    config->permanent_ban_cmd_v6[0] = '\0';
    config->redis_enabled = 0;
    config->redis_host[0] = '\0';
    config->redis_port = 6379;
    config->redis_password[0] = '\0';
    strcpy(config->redis_channel, "autoban_sync");

    config->tele_enabled = 0;
    config->tele_token[0] = '\0';
    config->tele_chat_id[0] = '\0';
}

static void parse_whitelist_line(AppConfig *config, const char *val) {
    if (config->whitelist_count >= config->whitelist_capacity) {
        config->whitelist_capacity *= 2;
        CidrEntry *new_whitelist =
            realloc(config->whitelist, config->whitelist_capacity * sizeof(CidrEntry));
        if (!new_whitelist) {
            config->whitelist_capacity /= 2;
            return;
        }
        config->whitelist = new_whitelist;
    }

    char ip_buf[MAX_STRING];
    strncpy(ip_buf, val, MAX_STRING - 1);
    ip_buf[MAX_STRING - 1] = '\0';
    int prefix = -1;
    char *slash = strchr(ip_buf, '/');
    if (slash) {
        *slash = '\0';
        prefix = atoi(slash + 1);
    }

    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, ip_buf, &addr4) == 1) {
        config->whitelist[config->whitelist_count].family = AF_INET;
        config->whitelist[config->whitelist_count].addr.v4 = addr4;
        config->whitelist[config->whitelist_count].prefix =
            (prefix >= 0 && prefix <= 32) ? prefix : 32;
        config->whitelist_count++;
    } else if (inet_pton(AF_INET6, ip_buf, &addr6) == 1) {
        config->whitelist[config->whitelist_count].family = AF_INET6;
        config->whitelist[config->whitelist_count].addr.v6 = addr6;
        config->whitelist[config->whitelist_count].prefix =
            (prefix >= 0 && prefix <= 128) ? prefix : 128;
        config->whitelist_count++;
    } else {
        fprintf(stderr, "[AutoBan] Cảnh báo: Định dạng IP Whitelist '%s' không hợp lệ.\n", val);
    }
}

static void parse_site_config(AppConfig *config, const char *server_name, const char *key,
                              const char *val) {
    int site_idx = -1;
    for (int i = 0; i < config->site_count; i++) {
        if (strcmp(config->sites[i].server_name, server_name) == 0) {
            site_idx = i;
            break;
        }
    }

    if (site_idx == -1) {
        if (config->site_count >= config->site_capacity) {
            config->site_capacity *= 2;
            SiteConfig *new_sites =
                realloc(config->sites, config->site_capacity * sizeof(SiteConfig));
            if (!new_sites) {
                config->site_capacity /= 2;
                return;
            }
            config->sites = new_sites;
        }
        site_idx = config->site_count++;
        strncpy(config->sites[site_idx].server_name, server_name, MAX_STRING - 1);
        config->sites[site_idx].server_name[MAX_STRING - 1] = '\0';
        config->sites[site_idx].ban_time_seconds = config->default_ban_time;
        config->sites[site_idx].block_cmd[0] = '\0';
        config->sites[site_idx].block_cmd_v6[0] = '\0';
    }

    process_site_handlers(&config->sites[site_idx], server_name, site_handlers, key, val);
}

int load_config(const char *filename, AppConfig *config) {
    // [CWE-377] Use file locking to prevent reading a partially-written config during reload
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_SH) < 0) {
        close(fd);
        return -1;
    }
    FILE *f = fdopen(fd, "r");
    if (!f) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    set_default_config(config);

    config->site_count = 0;
    config->site_capacity = INITIAL_CAPACITY;
    config->sites = malloc(config->site_capacity * sizeof(SiteConfig));
    if (!config->sites) {
        fclose(f);
        return -1;
    }

    config->whitelist_count = 0;
    config->whitelist_capacity = INITIAL_CAPACITY;
    config->whitelist = malloc(config->whitelist_capacity * sizeof(CidrEntry));
    if (!config->whitelist) {
        free(config->sites);
        config->sites = NULL;
        fclose(f);
        return -1;
    }

    char line[1024];
    char current_section[MAX_STRING] = "";

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r')
            continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, p + 1, MAX_STRING - 1);
                current_section[MAX_STRING - 1] = '\0';
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = p;
            char *val = eq + 1;
            char *hash = strchr(val, '#');
            if (hash)
                *hash = '\0';
            val[strcspn(val, "\r\n")] = '\0';
            while (*val == ' ' || *val == '\t')
                val++;

            char *key_end = key + strlen(key) - 1;
            while (key_end > key && (*key_end == ' ' || *key_end == '\t'))
                *key_end-- = '\0';

            if (strcmp(current_section, "global") == 0) {
                process_handlers(config, global_handlers, key, val);
            } else if (strcmp(current_section, "redis") == 0) {
                process_handlers(config, redis_handlers, key, val);
            } else if (strcmp(current_section, "telegram") == 0) {
                process_handlers(config, tele_handlers, key, val);
            } else if (strcmp(current_section, "whitelist") == 0) {
                if (strcmp(key, "allow") == 0)
                    parse_whitelist_line(config, val);
            } else if (strncmp(current_section, "website:", 8) == 0) {
                parse_site_config(config, current_section + 8, key, val);
            }
        }
    }
    fclose(f);

    if (strlen(config->log_pattern) > 0) {
        int ret = regcomp(&config->log_regex, config->log_pattern, REG_EXTENDED);
        if (ret == 0) {
            config->regex_compiled = 1;
            printf("[AutoBan] Đã biên dịch POSIX Regex thành công: %s\n", config->log_pattern);
        } else {
            char errbuf[256];
            regerror(ret, &config->log_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "[AutoBan] ERROR: Lỗi cú pháp Regex Pattern '%s' - %s\n",
                    config->log_pattern, errbuf);
        }
    }

    return 0;
}

int is_whitelisted(AppConfig *config, const char *ip) {
    if (!config || config->whitelist_count <= 0)
        return 0;

    struct in_addr addr4;
    struct in6_addr addr6;
    int is_v4 = (inet_pton(AF_INET, ip, &addr4) == 1);
    int is_v6 = (!is_v4 && inet_pton(AF_INET6, ip, &addr6) == 1);

    if (!is_v4 && !is_v6)
        return 0;

    for (int i = 0; i < config->whitelist_count; i++) {
        CidrEntry *entry = &config->whitelist[i];

        if (is_v4 && entry->family == AF_INET) {
            uint32_t mask = (entry->prefix == 0) ? 0 : (~0U << (32 - entry->prefix));
            mask = htonl(mask);
            if ((addr4.s_addr & mask) == (entry->addr.v4.s_addr & mask)) {
                return 1;
            }
        } else if (is_v6 && entry->family == AF_INET6) {
            int match = 1;
            int prefix = entry->prefix;
            for (int j = 0; j < 16; j++) {
                if (prefix >= 8) {
                    if (addr6.s6_addr[j] != entry->addr.v6.s6_addr[j]) {
                        match = 0;
                        break;
                    }
                    prefix -= 8;
                } else if (prefix > 0) {
                    unsigned char mask = (unsigned char)(0xFF00 >> prefix);
                    if ((addr6.s6_addr[j] & mask) != (entry->addr.v6.s6_addr[j] & mask)) {
                        match = 0;
                        break;
                    }
                    prefix = 0;
                } else {
                    break;
                }
            }
            if (match)
                return 1;
        }
    }
    return 0;
}

int get_ban_time_for_site(AppConfig *config, const char *server_name) {
    for (int i = 0; i < config->site_count; i++) {
        if (strcmp(config->sites[i].server_name, server_name) == 0) {
            return config->sites[i].ban_time_seconds;
        }
    }
    return config->default_ban_time;
}

const char *get_block_cmd_for_site(AppConfig *config, const char *server_name) {
    for (int i = 0; i < config->site_count; i++) {
        if (strcmp(config->sites[i].server_name, server_name) == 0) {
            if (strlen(config->sites[i].block_cmd) > 0) {
                return config->sites[i].block_cmd;
            }
            break;
        }
    }
    return config->block_cmd;
}

const char *get_block_cmd_v6_for_site(AppConfig *config, const char *server_name) {
    for (int i = 0; i < config->site_count; i++) {
        if (strcmp(config->sites[i].server_name, server_name) == 0) {
            if (strlen(config->sites[i].block_cmd_v6) > 0) {
                return config->sites[i].block_cmd_v6;
            }
            break;
        }
    }
    return config->block_cmd_v6;
}

void free_config(AppConfig *config) {
    if (config->sites)
        free(config->sites);
    if (config->whitelist)
        free(config->whitelist);

    if (config->regex_compiled) {
        regfree(&config->log_regex);
        config->regex_compiled = 0;
    }
}
