#ifndef CONFIG_H
#define CONFIG_H

#include <arpa/inet.h>
#include <regex.h>

#define MAX_STRING 512

// Khởi tạo sức chứa tĩnh ban đầu (Realloc khi tràng)
#define INITIAL_CAPACITY 10
#define MAX_JOURNAL_MATCHES 16
#define MAX_PATH 4096

// Struct mô tả cấu hình cho một website
typedef struct {
    char server_name[MAX_STRING];
    int ban_time_seconds;
    char block_cmd[MAX_STRING];    // Biểu thức đè nếu tồn tại
    char block_cmd_v6[MAX_STRING]; // Biểu thức đè cho IPv6
} SiteConfig;

typedef struct {
    int family; // AF_INET hoặc AF_INET6
    union {
        struct in_addr v4;
        struct in6_addr v6;
    } addr;
    int prefix; // V4: 0-32, V6: 0-128
} CidrEntry;

// Struct mô tả cấu hình chung cho toàn ứng dụng
typedef struct {
    char block_cmd[MAX_STRING];
    char block_cmd_v6[MAX_STRING];
    int default_ban_time;
    int cache_size;
    int max_blocks_per_minute;

    // Multi-Level Ban Parameters
    int min_ban_threshold;
    int max_ban_retries;
    int ban_time_window;
    char permanent_ban_cmd[MAX_STRING];
    char permanent_ban_cmd_v6[MAX_STRING];

    // Redis Distributed Sync
    int redis_enabled;
    char redis_host[MAX_STRING];
    int redis_port;
    char redis_password[MAX_STRING];
    char redis_channel[MAX_STRING];

    // Telegram Notifications
    int tele_enabled;
    char tele_token[MAX_STRING];
    char tele_chat_id[MAX_STRING];

    char log_pattern[MAX_STRING];
    regex_t log_regex;
    int regex_compiled;
    int ip_group_idx;
    int server_group_idx;

    char journal_matches[MAX_JOURNAL_MATCHES][MAX_STRING];
    int journal_match_count;
    char control_socket_path[MAX_PATH];

    SiteConfig *sites;
    int site_count;
    int site_capacity;

    CidrEntry *whitelist;
    int whitelist_count;
    int whitelist_capacity;
} AppConfig;

// Hàm tải file cấu hình vào bộ nhớ struct
int load_config(const char *filename, AppConfig *config);

// Lấy thời gian áp dụng cho một ứng dụng/website. Nếu k thấy thì trả về mặc định
int get_ban_time_for_site(AppConfig *config, const char *server_name);

// Lấy Block CMD Override tuỳ chọn.
const char *get_block_cmd_for_site(AppConfig *config, const char *server_name);
const char *get_block_cmd_v6_for_site(AppConfig *config, const char *server_name);

// Kiểm tra IP có trong whitelist không (Hỗ trợ theo Subnet O(N))
int is_whitelisted(AppConfig *config, const char *ip);

// Dọn dẹp con trỏ
void free_config(AppConfig *config);

#endif // CONFIG_H
