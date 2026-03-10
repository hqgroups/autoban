#define _POSIX_C_SOURCE 200809L
#include "ban_hash.h"
#include "common.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct BanEntry {
    char ip[IP_BUF_SIZE];
    int count;
    time_t first_seen;
    struct BanEntry *next;
} BanEntry;

static BanEntry *ban_hash_table[BAN_HASH_SIZE];
static pthread_mutex_t ban_hash_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int hash_ip(const char *ip) {
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*ip++))
        hash = ((hash << 5) + hash) + c;
    return hash % BAN_HASH_SIZE;
}

int ban_record_and_get_count(const char *ip, int window) {
    unsigned int idx = hash_ip(ip);
    time_t now = time(NULL);
    int count = 1;

    pthread_mutex_lock(&ban_hash_mutex);
    BanEntry *curr = ban_hash_table[idx];
    BanEntry *prev = NULL;
    while (curr) {
        if (strcmp(curr->ip, ip) == 0) {
            if (now - curr->first_seen > (time_t)window) {
                curr->count = 1;
                curr->first_seen = now;
            } else {
                curr->count++;
            }
            count = curr->count;
            pthread_mutex_unlock(&ban_hash_mutex);
            return count;
        }
        prev = curr;
        curr = curr->next;
    }

    BanEntry *new_entry = malloc(sizeof(BanEntry));
    if (new_entry) {
        strncpy(new_entry->ip, ip, IP_BUF_SIZE - 1);
        new_entry->ip[IP_BUF_SIZE - 1] = '\0';
        new_entry->count = 1;
        new_entry->first_seen = now;
        new_entry->next = NULL;
        if (prev)
            prev->next = new_entry;
        else
            ban_hash_table[idx] = new_entry;
    } else {
        pthread_mutex_unlock(&ban_hash_mutex);
        return 1;
    }
    pthread_mutex_unlock(&ban_hash_mutex);
    return count;
}

void ban_cleanup(int window) {
    time_t now = time(NULL);
    pthread_mutex_lock(&ban_hash_mutex);
    for (int i = 0; i < BAN_HASH_SIZE; i++) {
        BanEntry *curr = ban_hash_table[i];
        BanEntry *prev = NULL;
        while (curr) {
            if (now - curr->first_seen > (time_t)window) {
                BanEntry *temp = curr;
                if (prev)
                    prev->next = curr->next;
                else
                    ban_hash_table[i] = curr->next;
                curr = curr->next;
                free(temp);
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    }
    pthread_mutex_unlock(&ban_hash_mutex);
}
