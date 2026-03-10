#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "ban_hash.h"

int main() {
    printf("[AutoBan Test] Bắt đầu test ban_hash.c\n");
    
    // Thử một IP vi phạm 3 lần trong vòng 10 giây
    int w = 10;
    const char *ip = "192.168.1.100";
    
    int c1 = ban_record_and_get_count(ip, w);
    assert(c1 == 1);
    
    int c2 = ban_record_and_get_count(ip, w);
    assert(c2 == 2);
    
    int c3 = ban_record_and_get_count(ip, w);
    assert(c3 == 3);
    
    // Giả lập dọn rác, nằm ngoài window
    printf("   -> Chờ 2 giây để test Threshold Cleanup... (Giả lập expire)\n");
    sleep(2);
    
    // Lần này gọi test timeout cực ngắn (1 giây) -> sẽ bị reset thành 1
    int c4 = ban_record_and_get_count(ip, 1);
    assert(c4 == 1); // Đã reset vì quá cũ so với window=1s
    
    // Test Multi-IP không đè lên nhau
    const char *ip2 = "10.0.0.5";
    int c_ip2 = ban_record_and_get_count(ip2, w);
    assert(c_ip2 == 1);
    
    // c4 vẫn là 1, IP2 mới vô là 1
    int c_ip1 = ban_record_and_get_count(ip, 10); // Window lại 10
    assert(c_ip1 == 2);
    
    // Gọi lệnh cleanup (Memory leak test)
    ban_cleanup(0); // Dọn toàn bộ vì timeout 0
    
    printf("[AutoBan Test] ban_hash vượt qua toàn bộ Assertion!\n");
    return 0;
}
