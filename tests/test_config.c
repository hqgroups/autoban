#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "config.h"

int main() {
    printf("[AutoBan Test] Bắt đầu test config.c (Binary Search Whitelist)\n");
    
    AppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.whitelist_count = 3;
    cfg.whitelist = malloc(3 * sizeof(char *));
    cfg.whitelist[0] = strdup("1.1.1.1");
    cfg.whitelist[1] = strdup("8.8.8.8");
    cfg.whitelist[2] = strdup("10.0.0.1");
    
    // Sort array cho Binary Search O(logN) exactly like load_config does
    // Compare function from config.c needs to be simulated or we just sort standard
    int compare(const void *a, const void *b) {
        return strcmp(*(const char **)a, *(const char **)b);
    }
    qsort(cfg.whitelist, cfg.whitelist_count, sizeof(char *), compare);
    
    // Kiểm tra IP có trong Whitelist
    int r1 = is_whitelisted(&cfg, "8.8.8.8");
    assert(r1 == 1);
    
    int r2 = is_whitelisted(&cfg, "10.0.0.1");
    assert(r2 == 1);
    
    // Kiểm tra IP không có trong Whitelist
    int r3 = is_whitelisted(&cfg, "9.9.9.9");
    assert(r3 == 0);
    
    free(cfg.whitelist[0]);
    free(cfg.whitelist[1]);
    free(cfg.whitelist[2]);
    free(cfg.whitelist);
    
    printf("[AutoBan Test] config.c (Whitelist) vượt qua toàn bộ Assertion!\n");
    return 0;
}
