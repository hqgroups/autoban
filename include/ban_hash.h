#ifndef AUTOBAN_BAN_HASH_H
#define AUTOBAN_BAN_HASH_H

/* Multi-level ban: đếm số lần vi phạm trong window (giây). */

/* Trả về số lần vi phạm của IP (sau khi đã +1). window = ban_time_window (giây). */
int ban_record_and_get_count(const char *ip, int window);

/* Xóa các entry cũ hơn window giây (gọi định kỳ từ main). */
void ban_cleanup(int window);

#endif /* AUTOBAN_BAN_HASH_H */
