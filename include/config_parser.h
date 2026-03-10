#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>
#include "config.h"

// Enum mô tả kiểu dữ liệu cần gán
typedef enum {
    TYPE_INT,
    TYPE_STRING,
    TYPE_CUSTOM
} ConfigType;

// Struct Binding cho cấu hình Toàn cục (Global/Redis)
typedef struct {
    const char *key;
    ConfigType type;
    size_t offset;
    size_t max_len;
    void (*custom_handler)(AppConfig *config, const char *val);
} ConfigHandler;

// Struct Binding chuyên dụng cho cấu hình Website
typedef struct {
    const char *key;
    ConfigType type;
    size_t offset;
    size_t max_len;
    void (*custom_handler)(SiteConfig *config, const char *server_name, const char *val);
} SiteConfigHandler;

#endif // CONFIG_PARSER_H
