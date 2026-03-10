#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <pthread.h>
#include <string.h>

static BlockJob job_queue[MAX_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static int queue_count = 0;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

void queue_init(void) {
    (void)0;
}

int queue_enqueue(const BlockJob *job) {
    pthread_mutex_lock(&queue_mutex);
    if (queue_count >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }
    memcpy(&job_queue[queue_tail], job, sizeof(BlockJob));
    queue_tail = (queue_tail + 1) % MAX_QUEUE_SIZE;
    queue_count++;
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return 1;
}

int queue_dequeue_blocking(BlockJob *job_out, volatile int *shutdown) {
    pthread_mutex_lock(&queue_mutex);
    while (queue_count == 0 && (shutdown == NULL || !*shutdown)) {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    if (shutdown && *shutdown && queue_count == 0) {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }
    memcpy(job_out, &job_queue[queue_head], sizeof(BlockJob));
    queue_head = (queue_head + 1) % MAX_QUEUE_SIZE;
    queue_count--;
    pthread_mutex_unlock(&queue_mutex);
    return 1;
}

int queue_get_count(void) {
    int n;
    pthread_mutex_lock(&queue_mutex);
    n = queue_count;
    pthread_mutex_unlock(&queue_mutex);
    return n;
}

void queue_wake_waiters(void) {
    pthread_cond_broadcast(&queue_cond);
}
