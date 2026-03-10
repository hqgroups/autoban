#ifndef NGINX_SYNC_H
#define NGINX_SYNC_H

#include "config.h"

/**
 * Xuất cấu hình Nginx Rate Limit dựa trên Project Config.
 * File sẽ được lưu tại /etc/nginx/conf.d/autoban_rates.conf
 */
void nginx_sync_generate_config(AppConfig *config);

#endif
