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
#include <signal.h>

typedef struct {
    char host[256];
    int port;
    char path[256];
    int duration_ms;
} ThreadArgs;

static volatile int keep_running = 1;

void handle_signal(int sig) {
    keep_running = 0;
}

void* ddos_thread(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    struct sockaddr_in server_addr;
    struct hostent* server;

    server = gethostbyname(args->host);
    if (server == NULL) {
        perror("gethostbyname");
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(args->port);

    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: AutoBan-DDoS-Tester/1.0\r\n"
             "Connection: keep-alive\r\n\r\n",
             args->path, args->host);

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    long long count = 0;
    while (keep_running) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            send(sock, request, strlen(request), 0);
            count++;
        }
        close(sock);

        clock_gettime(CLOCK_MONOTONIC, &now);
        long long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= args->duration_ms) break;
        
        // No sleep = Maximum speed
    }

    printf("[Thread] Finished. Requests sent: %lld\n", count);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <host> <threads> <duration_ms> [path] [port]\n", argv[0]);
        printf("Example: %s localhost 50 5000 / 80\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_signal);

    char* host = argv[1];
    int num_threads = atoi(argv[2]);
    int duration = atoi(argv[3]);
    char* path = (argc > 4) ? argv[4] : "/";
    int port = (argc > 5) ? atoi(argv[5]) : 80;

    printf("Starting DDoS Test on %s:%d%s with %d threads for %d ms...\n", host, port, path, num_threads, duration);

    pthread_t* threads = malloc(sizeof(pthread_t) * num_threads);
    ThreadArgs* args = malloc(sizeof(ThreadArgs) * num_threads);

    for (int i = 0; i < num_threads; i++) {
        strncpy(args[i].host, host, 255);
        args[i].port = port;
        strncpy(args[i].path, path, 255);
        args[i].duration_ms = duration;
        pthread_create(&threads[i], NULL, ddos_thread, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(args);
    printf("DDoS Test completed.\n");
    return 0;
}
