# AutoBan — Architecture & Sequence Diagrams

---

## 1. Overall System Architecture

```mermaid
graph TB
    subgraph Internet
        A[Attacker / Client IPs]
    end

    subgraph Host Server
        subgraph Nginx
            N1[rate limit zone\n50r/m per IP]
            N2[error log → syslog]
        end

        subgraph AutoBan Daemon
            direction TB
            J[Journal Reader\nsd_journal_get_fd]
            C[Config Loader\nautoban.conf]
            B[block.c\nIP Validator + Cache]
            Q[Queue\nlock-free ring buffer]
            W1[Worker Thread 1]
            W2[Worker Thread 2]
            W3[Worker Thread 3]
            W4[Worker Thread 4]
            CS[Control Socket\n/run/autoban/autoban.sock]
        end

        subgraph Firewall
            IP4[ipset\nautoban_list]
            IP6[ipset\nautoban_list_v6]
            IPT[iptables / nftables\nDROP rule]
        end

        subgraph External
            R[(Redis\nPUB/SUB sync)]
            TG[Telegram Bot API\ncurl notification]
        end
    end

    A -->|HTTP requests| N1
    N1 -->|429 rate limited| N2
    N2 -->|syslog tag=nginx_error| J
    J -->|parsed IP + site| B
    B -->|whitelist check| C
    B -->|cache check| B
    B -->|enqueue job| Q
    Q -->|dequeue| W1 & W2 & W3 & W4
    W1 -->|execvp ipset| IP4
    W1 -->|execvp ipset| IP6
    IP4 --> IPT
    IP6 --> IPT
    W1 -->|PUBLISH| R
    W1 -->|curl POST| TG
    CS -->|status / reload / flush| B
    IPT -->|DROP| A
```

---

## 2. Sequence Diagram — IP Ban Flow

```mermaid
sequenceDiagram
    participant ATK as Attacker IP
    participant NGX as Nginx
    participant SYS as Syslog / Journald
    participant JRD as AutoBan Journal Reader
    participant BLK as block.c
    participant CACHE as RAM Cache
    participant WHTL as Whitelist
    participant QUEUE as Block Queue
    participant WRK as Worker Thread
    participant IPSET as ipset
    participant TG as Telegram Bot

    ATK->>NGX: Sends > 50 req/min
    NGX-->>ATK: 429 Too Many Requests
    NGX->>SYS: error log: limiting requests, client: 1.2.3.4

    SYS->>JRD: Journal entry (SYSLOG_IDENTIFIER=nginx_error)
    JRD->>BLK: block_ip("1.2.3.4", "_")

    BLK->>WHTL: is_whitelisted("1.2.3.4")?
    WHTL-->>BLK: No

    BLK->>CACHE: cache_is_recently_blocked("1.2.3.4")?
    CACHE-->>BLK: No (not yet blocked)

    BLK->>BLK: offense_count++ (ban_hash)
    BLK->>BLK: offense >= min_ban_threshold (3)?

    alt offense < threshold
        BLK-->>JRD: Log "vi phạm lần N, chờ ngưỡng"
    else offense >= threshold
        BLK->>CACHE: cache_mark_blocked() immediately
        BLK->>QUEUE: queue_enqueue(BlockJob)

        QUEUE->>WRK: queue_dequeue_blocking()
        WRK->>WRK: check_global_rate_limit()
        WRK->>IPSET: execvp("/usr/sbin/ipset add autoban_list 1.2.3.4 timeout 7200 -!")
        IPSET-->>WRK: success (exit 0)

        WRK->>TG: curl POST sendMessage\n"[AutoBan] IP Blocked: 1.2.3.4"
        TG-->>WRK: {"ok": true}

        Note over ATK,IPSET: All future packets from 1.2.3.4 → DROP
    end
```

---

## 3. Sequence Diagram — Reload Config

```mermaid
sequenceDiagram
    participant ADM as Admin
    participant SOC as Control Socket
    participant MAIN as main.c
    participant CFG as config.c
    participant WRK as Worker Threads

    ADM->>SOC: echo "reload" | socat - UNIX-CONNECT:/run/autoban/autoban.sock
    SOC->>MAIN: Sets reload_config_flag = 1
    MAIN->>MAIN: Detects flag in main loop
    MAIN->>CFG: load_config("/etc/autoban/autoban.conf")
    CFG->>CFG: Parse [global], [telegram], [redis], [whitelist], [website:*]
    CFG-->>MAIN: New AppConfig*
    MAIN->>WRK: Swap config pointer (mutex-protected)
    MAIN->>SOC: Respond "OK"
    SOC-->>ADM: OK
```

---

## 4. Sequence Diagram — DDoS Proxy Test Tool

```mermaid
sequenceDiagram
    participant TOOL as ddos_proxy_stress
    participant DNS as getaddrinfo()
    participant NGX as Nginx (Target)
    participant AB as AutoBan
    participant IPSET as ipset ban list

    TOOL->>TOOL: Load proxies.txt (10 proxy IPs)
    TOOL->>DNS: Resolve target host
    DNS-->>TOOL: Target IP address

    loop N threads × duration_ms
        TOOL->>NGX: GET / HTTP/1.1\nX-Real-IP: {proxy_ip}\nX-Forwarded-For: {proxy_ip}
        NGX->>NGX: real_ip_module reads X-Real-IP
        NGX->>NGX: Rate limit check per {proxy_ip}

        alt Under limit
            NGX-->>TOOL: 200 OK
        else Over 50r/m
            NGX-->>TOOL: 429 Too Many Requests
            NGX->>AB: syslog: limiting requests, client: {proxy_ip}
            AB->>AB: offense_count++ for {proxy_ip}
            AB->>IPSET: ban {proxy_ip} timeout 7200
        end

        TOOL->>TOOL: Rotate to next proxy IP
    end

    TOOL->>TOOL: Print FINAL STATS
```

---

## 5. Component Dependency Map

```mermaid
graph LR
    main.c --> config.c
    main.c --> journal.c
    main.c --> queue.c
    main.c --> worker.c
    main.c --> control_socket.c
    main.c --> redis_sync.c
    main.c --> cache.c

    journal.c --> block.c
    block.c --> cache.c
    block.c --> ban_hash.c
    block.c --> config.c
    block.c --> queue.c

    worker.c --> queue.c
    worker.c --> cache.c
    worker.c --> redis_sync.c
    worker.c -->|fork + execvp| ipset[(ipset)]
    worker.c -->|fork + curl| telegram[(Telegram API)]

    control_socket.c --> config.c
    control_socket.c --> cache.c

    redis_sync.c -->|PUBLISH| redis[(Redis)]
```
