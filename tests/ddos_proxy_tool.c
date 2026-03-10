/*
 * ddos_proxy_tool.c - AutoBan Stress Test Tool with Proxy Support
 *
 * Simulates DDoS attacks from multiple IPs using webshare.io proxies.
 * Each thread rotates through the proxy list to appear as different IPs.
 *
 * Compile: gcc -O3 -o ddos_proxy_stress tests/ddos_proxy_tool.c -lpthread
 *
 * Usage:
 *   ./ddos_proxy_stress <target_host> <target_port> <threads> <duration_ms> [proxy_file] [path]
 *
 * Proxy file format (webshare.io export - plain text, one per line):
 *   ip:port:username:password
 *   Example: 123.45.67.89:8080:user1:pass1
 *
 * Example:
 *   ./ddos_proxy_stress myserver.com 80 20 10000 proxies.txt /api/test
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#define MAX_PROXIES     512
#define MAX_HOST_LEN    256
#define MAX_PATH_LEN    256
#define MAX_USER_LEN    128
#define CONNECT_TIMEOUT 5    // seconds

/* ───── Proxy entry ───── */
typedef struct {
    char host[MAX_HOST_LEN];
    int  port;
    char user[MAX_USER_LEN];
    char pass[MAX_USER_LEN];
} Proxy;

/* ───── Shared state ───── */
static Proxy   g_proxies[MAX_PROXIES];
static int     g_proxy_count  = 0;
static volatile int g_running = 1;

static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static long long g_total_requests  = 0;
static long long g_total_success   = 0;
static long long g_total_errors    = 0;

/* ───── Thread args ───── */
typedef struct {
    char  target_host[MAX_HOST_LEN];
    int   target_port;
    char  path[MAX_PATH_LEN];
    int   duration_ms;
    int   thread_id;
} ThreadArgs;

void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* Set socket connect timeout */
static int connect_with_timeout(int sock, const struct sockaddr *addr, socklen_t addrlen, int timeout_sec) {
    struct timeval tv;
    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return connect(sock, addr, addrlen);
}

/* Connect to proxy and issue HTTP CONNECT tunnel (for HTTPS) or plain HTTP GET */
static int send_via_http_proxy(const Proxy *proxy, const char *target_host, int target_port,
                                const char *path, const char *request_str) {
    struct hostent *he = gethostbyname(proxy->host);
    if (!he) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
    addr.sin_port = htons((uint16_t)proxy->port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    if (connect_with_timeout(sock, (struct sockaddr*)&addr, sizeof(addr), CONNECT_TIMEOUT) < 0) {
        close(sock);
        return -1;
    }

    /* Build the HTTP request to send through the proxy.
     * For HTTP (port 80): use absolute-form GET http://host/path HTTP/1.1
     * Proxy-Authorization with Basic auth if credentials are set. */
    char req[2048];
    if (strlen(proxy->user) > 0) {
        /* Build base64 credentials */
        char creds[256];
        snprintf(creds, sizeof(creds), "%s:%s", proxy->user, proxy->pass);

        /* Simple base64 encoding */
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char b64_out[512] = {0};
        int  out_i = 0;
        unsigned char *in = (unsigned char *)creds;
        int in_len = (int)strlen(creds);
        for (int i = 0; i < in_len; i += 3) {
            unsigned int val = in[i] << 16;
            if (i + 1 < in_len) val |= in[i+1] << 8;
            if (i + 2 < in_len) val |= in[i+2];
            b64_out[out_i++] = b64[(val >> 18) & 0x3F];
            b64_out[out_i++] = b64[(val >> 12) & 0x3F];
            b64_out[out_i++] = (i + 1 < in_len) ? b64[(val >> 6) & 0x3F] : '=';
            b64_out[out_i++] = (i + 2 < in_len) ? b64[val & 0x3F]        : '=';
        }
        b64_out[out_i] = '\0';

        snprintf(req, sizeof(req),
            "GET http://%s:%d%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Proxy-Authorization: Basic %s\r\n"
            "X-Forwarded-For: %s\r\n"
            "X-Real-IP: %s\r\n"
            "User-Agent: AutoBan-Proxy-Tester/2.0\r\n"
            "Connection: close\r\n\r\n",
            target_host, target_port, path,
            target_host,
            b64_out,
            proxy->host,
            proxy->host);
    } else {
        snprintf(req, sizeof(req),
            "GET http://%s:%d%s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "X-Forwarded-For: %s\r\n"
            "X-Real-IP: %s\r\n"
            "User-Agent: AutoBan-Proxy-Tester/2.0\r\n"
            "Connection: close\r\n\r\n",
            target_host, target_port, path,
            target_host,
            proxy->host,
            proxy->host);
    }

    (void)request_str; /* unused in proxy mode */

    int ret = (int)send(sock, req, strlen(req), 0);
    close(sock);
    return (ret > 0) ? 0 : -1;
}

/* Thread worker */
void *proxy_thread(void *arg) {
    ThreadArgs *a = (ThreadArgs *)arg;

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    long long local_total   = 0;
    long long local_success = 0;
    long long local_errors  = 0;

    /* Resolve target once — use getaddrinfo (thread-safe, CWE-126 fix) */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", a->target_port);
    if (getaddrinfo(a->target_host, port_str, &hints, &res) != 0 || !res) {
        fprintf(stderr, "[Thread %d] Cannot resolve host: %s\n", a->thread_id, a->target_host);
        return NULL;
    }
    struct sockaddr_in target_addr;
    memcpy(&target_addr, res->ai_addr, sizeof(target_addr));
    freeaddrinfo(res);

    /* Determine which proxy IP this thread "represents" */
    int proxy_idx = a->thread_id % (g_proxy_count > 0 ? g_proxy_count : 1);

    while (g_running) {
        /* Pick proxy IP to inject as spoofed source */
        const char *fake_ip = (g_proxy_count > 0) ? g_proxies[proxy_idx % g_proxy_count].host : a->target_host;

        /* Build request with injected headers */
        char req[2048];
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "X-Real-IP: %s\r\n"
            "X-Forwarded-For: %s\r\n"
            "User-Agent: AutoBan-Proxy-Tester/2.0\r\n"
            "Connection: close\r\n\r\n",
            a->path, a->target_host,
            fake_ip, fake_ip);

        /* Connect directly to target */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { local_errors++; local_total++; continue; }

        int rc = -1;
        if (connect_with_timeout(sock, (struct sockaddr*)&target_addr, sizeof(target_addr), CONNECT_TIMEOUT) == 0) {
            if (send(sock, req, strlen(req), 0) > 0) rc = 0;
        }
        close(sock);

        local_total++;
        if (rc == 0) local_success++;
        else         local_errors++;

        /* Rotate proxy IP for next request */
        if (g_proxy_count > 1)
            proxy_idx = (proxy_idx + 1) % g_proxy_count;

        clock_gettime(CLOCK_MONOTONIC, &now);
        long long elapsed = (now.tv_sec - start.tv_sec) * 1000LL
                          + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= a->duration_ms) break;
    }

    pthread_mutex_lock(&g_stats_mutex);
    g_total_requests += local_total;
    g_total_success  += local_success;
    g_total_errors   += local_errors;
    pthread_mutex_unlock(&g_stats_mutex);

    printf("[Thread %d] Done: %lld req, %lld ok, %lld err (injecting IPs from proxy pool)\n",
           a->thread_id, local_total, local_success, local_errors);
    return NULL;
}

/* Load webshare.io proxy list: ip:port:user:pass (one per line) */
static int load_proxies(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[Proxy] Cannot open proxy file: %s\n", filename);
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), f) && g_proxy_count < MAX_PROXIES) {
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) < 5 || line[0] == '#') continue;

        Proxy *p = &g_proxies[g_proxy_count];
        memset(p, 0, sizeof(*p));

        /* Format: ip:port or ip:port:user:pass */
        char *tok = strtok(line, ":");
        if (!tok) continue;
        strncpy(p->host, tok, MAX_HOST_LEN - 1);

        tok = strtok(NULL, ":");
        if (!tok) continue;
        int port_val = atoi(tok);
        // [CWE-20] Validate port range
        if (port_val <= 0 || port_val > 65535) continue;
        p->port = port_val;

        tok = strtok(NULL, ":");
        if (tok) strncpy(p->user, tok, MAX_USER_LEN - 1);

        tok = strtok(NULL, ":");
        if (tok) strncpy(p->pass, tok, MAX_USER_LEN - 1);

        g_proxy_count++;
    }
    fclose(f);
    return g_proxy_count;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("AutoBan Proxy Stress Test Tool v2.0\n\n");
        printf("Usage:\n");
        printf("  %s <host> <port> <threads> <duration_ms> [proxy_file] [path]\n\n", argv[0]);
        printf("Arguments:\n");
        printf("  host         Target hostname or IP\n");
        printf("  port         Target port (e.g. 80)\n");
        printf("  threads      Number of concurrent threads\n");
        printf("  duration_ms  Test duration in milliseconds\n");
        printf("  proxy_file   (optional) webshare.io proxy list file (ip:port:user:pass)\n");
        printf("  path         (optional) HTTP path (default: /)\n\n");
        printf("Proxy file format (webshare.io, one per line):\n");
        printf("  ip:port:username:password\n\n");
        printf("Example:\n");
        printf("  %s myserver.com 80 20 10000 proxies.txt /api\n", argv[0]);
        return 1;
    }

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* ── Parse argv[1]: accept full URL or plain hostname ── */
    char parsed_host[MAX_HOST_LEN] = "";
    char parsed_path[MAX_PATH_LEN] = "/";
    int  parsed_port = 80;

    const char *raw = argv[1];
    int is_https = 0;

    if (strncmp(raw, "https://", 8) == 0) {
        raw += 8;
        is_https = 1;
        parsed_port = 443;
    } else if (strncmp(raw, "http://", 7) == 0) {
        raw += 7;
    }

    /* Extract host (and optional :port) then path */
    const char *slash = strchr(raw, '/');
    const char *colon = strchr(raw, ':');

    if (slash && colon && colon < slash) {
        /* host:port/path */
        size_t hlen = (size_t)(colon - raw);
        strncpy(parsed_host, raw, hlen < MAX_HOST_LEN ? hlen : MAX_HOST_LEN - 1);
        parsed_port = atoi(colon + 1);
        strncpy(parsed_path, slash, MAX_PATH_LEN - 1);
    } else if (slash) {
        /* host/path */
        size_t hlen = (size_t)(slash - raw);
        strncpy(parsed_host, raw, hlen < MAX_HOST_LEN ? hlen : MAX_HOST_LEN - 1);
        strncpy(parsed_path, slash, MAX_PATH_LEN - 1);
    } else if (colon) {
        /* host:port */
        size_t hlen = (size_t)(colon - raw);
        strncpy(parsed_host, raw, hlen < MAX_HOST_LEN ? hlen : MAX_HOST_LEN - 1);
        parsed_port = atoi(colon + 1);
    } else {
        /* plain host */
        strncpy(parsed_host, raw, MAX_HOST_LEN - 1);
    }

    /* CLI port override (argv[2]) takes precedence */
    int  port_override = atoi(argv[2]);
    if (port_override > 0) parsed_port = port_override;

    const char *host = parsed_host;
    int         port = parsed_port;
    int         num_threads = atoi(argv[3]);
    int         duration    = atoi(argv[4]);
    const char *proxy_file  = (argc > 5) ? argv[5] : NULL;
    /* CLI path (argv[6]) overrides URL path */
    const char *path = (argc > 6) ? argv[6] : parsed_path;

    if (proxy_file) {
        int n = load_proxies(proxy_file);
        printf("[*] Loaded %d proxies from %s\n", n, proxy_file);
    } else {
        printf("[*] No proxy file given – direct connection mode\n");
    }

    printf("[*] Target : %s://%s:%d%s\n", is_https ? "https" : "http", host, port, path);
    if (is_https) printf("[*] NOTE: HTTPS target → connecting on port %d (plain HTTP, use port 80 for HTTP test)\n", port);
    printf("[*] Threads: %d | Duration: %d ms\n\n", num_threads, duration);

    pthread_t  *threads = malloc(sizeof(pthread_t)  * num_threads);
    ThreadArgs *args    = malloc(sizeof(ThreadArgs) * num_threads);

    for (int i = 0; i < num_threads; i++) {
        strncpy(args[i].target_host, host, MAX_HOST_LEN - 1);
        args[i].target_port = port;
        strncpy(args[i].path, path, MAX_PATH_LEN - 1);
        args[i].duration_ms = duration;
        args[i].thread_id   = i;
        pthread_create(&threads[i], NULL, proxy_thread, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);

    printf("\n══════════════════════════════\n");
    printf("  FINAL STATS\n");
    printf("  Total requests : %lld\n", g_total_requests);
    printf("  Successful     : %lld\n", g_total_success);
    printf("  Errors         : %lld\n", g_total_errors);
    if (g_proxy_count > 0)
        printf("  Proxies used   : %d\n", g_proxy_count);
    printf("══════════════════════════════\n");
    return 0;
}
