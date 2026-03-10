#ifndef AUTOBAN_COMMON_H
#define AUTOBAN_COMMON_H

#include <pthread.h>
#include <signal.h>
#include "config.h"

/* Hằng số dùng chung giữa các module (có thể đưa vào config sau) */
#define WORKER_THREADS_DEFAULT  4
#define MAX_QUEUE_SIZE         10000
#define MAX_REGEX_GROUPS       16
#define DEBOUNCE_INTERVAL_SEC  5
#define BAN_HASH_SIZE          8191

#define CMD_BUF_SIZE           1024
#define IP_BUF_SIZE            64
#define SERVER_NAME_BUF_SIZE   256

#define AUTOBAN_VERSION "1.3"

/* Context truyền vào các thread (main khởi tạo, mỗi thread nhận con trỏ) */
typedef struct {
    AppConfig **config_ptr;
    pthread_mutex_t *config_mutex;
    volatile sig_atomic_t *running;
    volatile sig_atomic_t *reload_config_flag;
    volatile sig_atomic_t *dump_metrics_flag;
    volatile int *worker_shutdown;
} AutobanContext;

#endif /* AUTOBAN_COMMON_H */
