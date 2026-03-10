#ifndef AUTOBAN_REDIS_SYNC_H
#define AUTOBAN_REDIS_SYNC_H

#include "common.h"

/* Thread subscribe Redis channel, gọi block_ip cho mỗi IP nhận được. arg = AutobanContext*. */
void *redis_subscriber_thread(void *arg);

#endif /* AUTOBAN_REDIS_SYNC_H */
