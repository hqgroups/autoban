#ifndef AUTOBAN_WORKER_H
#define AUTOBAN_WORKER_H

#include "common.h"

/* Thread worker: dequeue job, exec block command, mark cache, Redis publish. arg = AutobanContext*. */
void *queue_worker_thread(void *arg);

#endif /* AUTOBAN_WORKER_H */
