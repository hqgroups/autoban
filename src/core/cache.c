#define _POSIX_C_SOURCE 200809L
#include "cache.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

typedef struct {
    char ip[IP_BUF_SIZE];
    char server_name[SERVER_NAME_BUF_SIZE];
    time_t last_block_time;
} BlockCacheEntry;

static BlockCacheEntry *cache = NULL;
static int cache_size = 0;
static int cache_idx = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static int metrics_total_blocks = 0;
static int metrics_cache_hits = 0;

int cache_init(int size) {
    if (size <= 0) return -1;
    pthread_mutex_lock(&cache_mutex);
    if (cache) {
        pthread_mutex_unlock(&cache_mutex);
        return -1;
    }
    cache = malloc((size_t)size * sizeof(BlockCacheEntry));
    if (!cache) {
        pthread_mutex_unlock(&cache_mutex);
        return -1;
    }
    memset(cache, 0, (size_t)size * sizeof(BlockCacheEntry));
    cache_size = size;
    cache_idx = 0;
    pthread_mutex_unlock(&cache_mutex);
    return 0;
}

int cache_resize(int new_size) {
    if (new_size <= 0) return 0;
    pthread_mutex_lock(&cache_mutex);
    BlockCacheEntry *new_cache = realloc(cache, (size_t)new_size * sizeof(BlockCacheEntry));
    if (!new_cache) {
        pthread_mutex_unlock(&cache_mutex);
        return 0;
    }
    cache = new_cache;
    if (new_size > cache_size) {
        memset(cache + cache_size, 0, (size_t)(new_size - cache_size) * sizeof(BlockCacheEntry));
    }
    cache_size = new_size;
    if (cache_idx >= cache_size) cache_idx = 0;
    pthread_mutex_unlock(&cache_mutex);
    return 1;
}

void cache_free(void) {
    pthread_mutex_lock(&cache_mutex);
    free(cache);
    cache = NULL;
    cache_size = 0;
    cache_idx = 0;
    pthread_mutex_unlock(&cache_mutex);
}

int cache_is_recently_blocked(const char *ip, const char *server_name) {
    if (!cache) return 0;
    time_t now = time(NULL);
    int result = 0;
    pthread_mutex_lock(&cache_mutex);
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].last_block_time > 0 &&
            strcmp(cache[i].ip, ip) == 0 &&
            strcmp(cache[i].server_name, server_name) == 0 &&
            (now - cache[i].last_block_time) <= DEBOUNCE_INTERVAL_SEC) {
            result = 1;
            break;
        }
    }
    pthread_mutex_unlock(&cache_mutex);
    return result;
}

void cache_mark_blocked(const char *ip, const char *server_name) {
    if (!cache) return;
    pthread_mutex_lock(&cache_mutex);
    strncpy(cache[cache_idx].ip, ip, IP_BUF_SIZE - 1);
    cache[cache_idx].ip[IP_BUF_SIZE - 1] = '\0';
    strncpy(cache[cache_idx].server_name, server_name, SERVER_NAME_BUF_SIZE - 1);
    cache[cache_idx].server_name[SERVER_NAME_BUF_SIZE - 1] = '\0';
    cache[cache_idx].last_block_time = time(NULL);
    cache_idx = (cache_idx + 1) % cache_size;
    __atomic_fetch_add(&metrics_total_blocks, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&cache_mutex);
}

void cache_flush(void) {
    if (!cache) return;
    pthread_mutex_lock(&cache_mutex);
    memset(cache, 0, (size_t)cache_size * sizeof(BlockCacheEntry));
    cache_idx = 0;
    pthread_mutex_unlock(&cache_mutex);
}

int cache_get_size(void) {
    return cache_size;
}

int cache_get_metrics_total_blocks(void) {
    return __atomic_load_n(&metrics_total_blocks, __ATOMIC_RELAXED);
}

int cache_get_metrics_cache_hits(void) {
    return __atomic_load_n(&metrics_cache_hits, __ATOMIC_RELAXED);
}

void cache_inc_cache_hits(void) {
    __atomic_fetch_add(&metrics_cache_hits, 1, __ATOMIC_RELAXED);
}
