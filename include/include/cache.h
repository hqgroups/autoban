#ifndef AUTOBAN_CACHE_H
#define AUTOBAN_CACHE_H

/* Debounce cache: tránh spam block cùng IP+server trong DEBOUNCE_INTERVAL_SEC. */

/* Cấp phát cache với kích thước size. Gọi lại khi reload config đổi cache_size. */
int cache_init(int size);

/* Thay đổi kích thước cache (realloc). Trả về 0 nếu lỗi, 1 nếu thành công. */
int cache_resize(int new_size);

void cache_free(void);

/* Trả về 1 nếu IP+server_name vừa bị block trong DEBOUNCE_INTERVAL_SEC. */
int cache_is_recently_blocked(const char *ip, const char *server_name);

/* Ghi nhận IP+server_name vừa được block (tăng metrics total_blocks). */
void cache_mark_blocked(const char *ip, const char *server_name);

/* Xóa toàn bộ cache (lệnh flush qua control socket). */
void cache_flush(void);

/* Số entry hiện dùng (cho resize). */
int cache_get_size(void);

/* Metrics nguyên tử. */
int cache_get_metrics_total_blocks(void);
int cache_get_metrics_cache_hits(void);
void cache_inc_cache_hits(void);

#endif /* AUTOBAN_CACHE_H */
