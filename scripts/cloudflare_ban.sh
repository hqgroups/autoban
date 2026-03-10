#!/bin/bash
# AutoBan - Cloudflare WAF Block Script
# Tích hợp ngăn chặn tấn công nhiều lớp (Đẩy rác IP lên Edge Cloudflare)
# System: AutoBan v2.0
# Usage: ./cloudflare_ban.sh <IP_ADDRESS>

IP=$1

if [ -z "$IP" ]; then
    echo "Usage: $0 <IP_ADDRESS>"
    exit 1
fi

# --- CLOUDFLARE CONFIGURATION ---
# Bạn có thể cấp quyền Zone > Firewall > Edit cho Token này
CF_API_TOKEN="YOUR_CF_API_TOKEN_HERE"
CF_ZONE_ID="YOUR_CF_ZONE_ID_HERE"
# --------------------------------

if [ "$CF_API_TOKEN" == "YOUR_CF_API_TOKEN_HERE" ]; then
    echo "[AutoBan CF] ERROR: Vui lòng sửa cấu hình CF_API_TOKEN trong $(basename $0)." >&2
    exit 1
fi

# Cảm biến dạng thức IP (IPv4 vs IPv6 target format required by CF)
if [[ "$IP" == *":"* ]]; then
    TARGET_MODE="ip6"
else
    TARGET_MODE="ip"
fi

# Gọi Cloudflare V4 Zone-Level Access Rules API
RESPONSE=$(curl -s -X POST "https://api.cloudflare.com/client/v4/zones/${CF_ZONE_ID}/firewall/access_rules/rules" \
     -H "Authorization: Bearer ${CF_API_TOKEN}" \
     -H "Content-Type: application/json" \
     --data "{
  \"mode\": \"block\",
  \"configuration\": {
    \"target\": \"${TARGET_MODE}\",
    \"value\": \"${IP}\"
  },
  \"notes\": \"Banned by AutoBan Daemon (Multi-Level Strike)\"
}")

# Kiểm chứng phản hồi
if echo "$RESPONSE" | grep -q '"success":true'; then
    echo "[AutoBan CF] Đã gửi API đẩy IP $IP lên viền Edge Cloudflare thành công."
else
    # Lỗi trùng lặp hoặc Token Invalid
    echo "[AutoBan CF] ERROR: Không thể chặn $IP trên Cloudflare WAF: $RESPONSE" >&2
    exit 1
fi
