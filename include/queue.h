#ifndef AUTOBAN_QUEUE_H
#define AUTOBAN_QUEUE_H

#include "common.h"

typedef struct {
    char cmd[CMD_BUF_SIZE];
    char ip[IP_BUF_SIZE];
    char server_name[SERVER_NAME_BUF_SIZE];
    int is_synced; /* 0 = Local, 1 = Received from Redis */
} BlockJob;

/* Khởi tạo queue (gọi một lần từ main). */
void queue_init(void);

/* Trả về 1 nếu enqueue thành công, 0 nếu queue đầy. */
int queue_enqueue(const BlockJob *job);

/* Chờ đến khi có job hoặc *shutdown != 0. Trả về 1 nếu lấy được job, 0 nếu shutdown. */
int queue_dequeue_blocking(BlockJob *job_out, volatile int *shutdown);

/* Số job hiện có trong queue. */
int queue_get_count(void);

/* Đánh thức các worker đang chờ. */
void queue_wake_waiters(void);

#endif /* AUTOBAN_QUEUE_H */
