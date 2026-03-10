# AutoBan - IP Blocker Daemon (Nginx + Systemd Journald + Ipset/Nftables)

Dự án này là một dịch vụ Daemon (`autoban`) được viết bằng ngôn ngữ lập trình C cho Linux, theo phân tích kiến trúc từ sơ đồ luồng hệ thống.

## 📁 Cấu trúc mã nguồn (sau tái cấu trúc)

- **src/main.c** — Điểm vào, signal, vòng lặp journal, reload/shutdown.
- **src/config.c / config.h** — Đọc cấu hình INI, whitelist, site-specific.
- **src/queue.c / queue.h** — Hàng đợi job block (enqueue/dequeue).
- **src/cache.c / cache.h** — Debounce cache + metrics.
- **src/ban_hash.c / ban_hash.h** — Đếm vi phạm theo IP (multi-level ban).
- **src/ipset.c / ipset.h** — Khởi tạo ipset IPv4/IPv6.
- **src/block.c / block.h** — `block_ip()`: validate, whitelist, queue job.
- **src/control_socket.c / control_socket.h** — Unix socket: status, reload, flush.
- **src/redis_sync.c / redis_sync.h** — Redis subscriber (đồng bộ block).
- **src/worker.c / worker.h** — Worker thread: thực thi lệnh, cache, publish Redis.
- **src/journal.c / journal.h** — Parse log Nginx/regex, gọi `block_ip`.
- **src/common.h** — Hằng số, `AutobanContext` (truyền vào thread).

Chi tiết đánh giá và hướng dẫn tái cấu trúc: **REVIEW.md**.

---

## 🎯 Phân Tích Kiến Trúc Yêu Cầu

1. **Traffic Flow**: 
   `Internet` ──────> `Nftables (Ipset)` ──────> `Nginx (Rate Limit)` ──────> `FPM` ──────> `App`
2. **Log Logging via Syslog**:
   - Nginx cấu hình gửi log trực tiếp tới `syslog` `/dev/log` thay vì chỉ ghi ra `error.log`.
   - Kết quả: Log được gửi đẩy vào **Systemd Journald** (`sd-journal`).
3. **C Program Event Listener**:
   - Chương trình C sẽ sử dụng `<systemd/sd-journal.h>` để subscribe (theo dõi) `sd-systemd` lấy log từ `nginx` theo thời gian thực (Real-time Streaming).
   - Parse các dòng log báo lỗi chặn tỷ lệ (Rate Limiting). VD: log có cụm `limiting requests`.
4. **Action via Ipset & Nftables**:
   - Khi phát hiện một IP spam/DDOS từ log, C Program sẽ trích xuất IP (Client IP) và Server block (Domain/Website).
   - Tự động thực thi lệnh do người dùng định nghĩa để block IP thông qua công cụ `ipset` (hoặc `nftables`).
5. **Cấu Hình Cấp Phân Giải Cao (Configurable)**:
   - Các quản trị viên có cấu hình linh hoạt (block IP trong bao lâu).
   - Hỗ trợ tuỳ chỉnh thời gian block **linh động theo các website riêng biệt**, hoặc dùng cấu hình mặc định (default).

---

## 🚀 Hướng Dẫn Cài Đặt và Biên Dịch Hệ Thống

### 1. Cài đặt các thư viện phụ thuộc (`libsystemd`)
Do hệ thống được build dựa trên Systemd APIs, bạn sẽ cần `libsystemd-dev`.
```bash
# Ubuntu / Debian
sudo apt install build-essential libsystemd-dev libhiredis-dev ipset

# RHEL / CentOS / Fedora
sudo dnf install gcc systemd-devel hiredis-devel ipset
```

### 2. Biên dịch Core Service (C Program)
```bash
# Chạy mã lệnh Makefile để biên dịch ra file autoban ngay lập tức
make

# Cài đặt file cấu hình, thực thi và khởi tạo service systemd
sudo make install
```

### 3. Cấu hình Ipset (Tự động)
Từ phiên bản 1.2, AutoBan sẽ **tự động kiểm tra và khởi tạo** `ipset` tên là `autoban_list` nếu nó chưa tồn tại. Bạn chỉ cần cấu hình Firewall để sử dụng set này:
```bash
# Thêm rule vào iptables để drop IP nằm trong set này
sudo iptables -I INPUT -m set --match-set autoban_list src -j DROP
```

### 4. Điều khiển qua Unix Socket (v1.2+)
Dịch vụ mở một socket tại `/var/run/autoban.sock` để quản trị nhanh:
```bash
# Xem trạng thái hiện tại (Metrics)
echo "status" | sudo socat - UNIX-CONNECT:/var/run/autoban.sock

# Nạp lại cấu hình nóng
echo "reload" | sudo socat - UNIX-CONNECT:/var/run/autoban.sock

# Xóa trắng bộ nhớ đệm (Flush cache)
echo "flush" | sudo socat - UNIX-CONNECT:/var/run/autoban.sock
```

### 4. Cấu hình Nginx (`nginx/nginx_autoban.conf`)
Chèn config sau vào tệp tin giới hạn và syslog `error_log` (`nginx.conf`):
```nginx
# Rate Limit Rules
limit_req_zone $binary_remote_addr zone=api_limit:10m rate=1r/s;

# Đẩy Log Error Ra Journald
error_log syslog:server=unix:/dev/log,nohostname,tag=nginx_error error;
```

### 5. Start Service và Tận Hưởng
```bash
# Systemctl reload tự động được gọi trong lệnh make install
sudo systemctl enable autoban
sudo systemctl start autoban
```

---

## Cấu hình (Configuration)
Tệp cấu hình mẫu `/etc/autoban/autoban.conf`:

```ini
[global]
# Lệnh thực thi hệ thống (Hỗ trợ Dual-Stack IPv4 và IPv6)
# block_cmd = ipset add autoban_list %s timeout %d
# block_cmd_v6 = ipset add autoban_list_v6 %s timeout %d

# journal_match = nginx_error
```
## 🗂 Cấu Hình Ứng Dụng (`/etc/autoban/autoban.conf`)

Ứng dụng hỗ trợ chia thành các section. Ví dụ cấu hình nâng cao:
- `journal_match`: Danh sách các identifier cần theo dõi (phân tách bởi dấu phẩy).
- `log_pattern`: Regex để bắt IP và Domain.

### Hướng dẫn Regex (Advanced Parse)
AutoBan sử dụng POSIX Extended Regex. Bạn cần bắt ít nhất 2 group: **Group 1 là IP**, **Group 2 là Domain**.

**1. Nginx Default (limiting requests):**
`limiting requests,.*client: ([0-9.]+), server: ([a-z0-9.-]+)`

**2. Apache Mod_Security / RateLimit:**
`\[client ([0-9.]+)\] .* hostname ([a-z0-9.-]+)`

**3. Tùy biến Group Index:**
Nếu log format của bạn có IP ở Group 3 và Domain ở Group 1, hãy cấu hình như sau trong `autoban.conf`:
```ini
ip_group_idx = 3
server_group_idx = 1
```

**4. Custom Log Format:**
Nếu bạn dùng format riêng, hãy đảm bảo Regex capture đúng thứ tự nhóm.

## 🧠 Design Considerations (Lưu ý Thiết kế)

### Cơ chế "Instant Cache Marking" (Đánh dấu tức thì)
Trong AutoBan, IP sẽ được đánh dấu vào bộ đệm (Debounce Cache) **ngay sau khi lệnh fork() thành công**, thay vì đợi lệnh thực thi xong.
- **Tại sao?**: Để bảo vệ hệ thống khỏi việc bị treo Worker khi firewall script chạy chậm, đồng thời ngăn chặn ngay lập tức việc nạp chồng hàng nghìn Job giống hệt nhau vào hàng đợi nếu Log Spam xảy ra cực nhanh.
- **Đánh đổi**: Nếu lệnh thực thi firewall thất bại (ví dụ sai đường dẫn), IP đó sẽ bị bỏ qua trong 5 giây tới (không block được). Tuy nhiên, đây là sự đánh đổi cần thiết để ưu tiên **tính ổn định tuyệt đối** của Daemon dưới tải cực cao.

---
