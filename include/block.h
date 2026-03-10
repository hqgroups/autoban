#ifndef AUTOBAN_BLOCK_H
#define AUTOBAN_BLOCK_H

#include "config.h"

/* Xác thực IP chuẩn IPv4/IPv6 (tránh command injection). */
int block_is_valid_ip(const char *ip);

/* Chuẩn bị job và đẩy vào queue. is_synced: 0 = từ log local, 1 = từ Redis sync. */
void block_ip(AppConfig *config, const char *ip, const char *server_name, int is_synced);

#endif /* AUTOBAN_BLOCK_H */
