#!/bin/bash
# scripts/auto_test.sh
# Kịch bản test tự động cho AutoBan bên trong Docker

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo "--- [1/4] Xoá trắng Ipset và Cache ---"
ipset flush autoban_list 2>/dev/null || ipset create autoban_list hash:ip timeout 3600
echo "status" | socat - UNIX-CONNECT:/run/autoban/autoban.sock | grep -q "status" && echo "flush" | socat - UNIX-CONNECT:/run/autoban/autoban.sock
echo -e "${GREEN}Done.${NC}"

echo -e "\n--- [2/4] Bắt đầu gửi request spam (Rate Limit: 1r/s) ---"
for i in {1..10}
do
   # Gửi request tới chính mình (localhost) bên trong container
   curl -s -I http://localhost > /dev/null
   echo -n "."
   sleep 0.1
done
echo -e "\n${GREEN}Spam hoàn tất.${NC}"

echo -e "\n--- [3/4] Đợi AutoBan xử lý log (5s) ---"
sleep 5

echo -e "\n--- [4/4] Kiểm tra kết quả trong Ipset ---"
BANNED_IPS=$(ipset list autoban_list | grep -E '^[0-9.]+')

if [ -z "$BANNED_IPS" ]; then
    echo -e "${RED}THẤT BẠI: Không có IP nào bị chặn trong autoban_list.${NC}"
    echo "Gợi ý: Kiểm tra 'docker logs autoban-test' để xem AutoBan có bắt được log không."
    exit 1
else
    echo -e "${GREEN}THÀNH CÔNG! Các IP sau đã bị chặn:${NC}"
    echo "$BANNED_IPS"
    exit 0
fi
