#define _POSIX_C_SOURCE 200809L
#include "ipset.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_ipset(const char *ipset_bin, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execv(ipset_bin, argv);
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void create_ipset(const char *ipset_bin, const char *list_name, int is_v6) {
    int r;
    if (is_v6) {
        char *argv[] = {(char *)ipset_bin, (char *)"create",
                        (char *)list_name, (char *)"hash:ip",
                        (char *)"family",  (char *)"inet6",
                        (char *)"timeout", (char *)"3600",
                        (char *)"-exist",  NULL};
        r = run_ipset(ipset_bin, argv);
    } else {
        char *argv[] = {(char *)ipset_bin, (char *)"create", (char *)list_name, (char *)"hash:ip",
                        (char *)"timeout", (char *)"3600",   (char *)"-exist",  NULL};
        r = run_ipset(ipset_bin, argv);
    }

    if (r == 0) {
        printf("[AutoBan] Khởi tạo ipset %s thành công.\n", is_v6 ? "IPv6" : "IPv4");
    } else {
        fprintf(stderr, "[AutoBan] ERROR: Không thể tạo ipset %s (exit %d).\n",
                is_v6 ? "IPv6" : "IPv4", r);
    }
}

void ipset_init(void) {
    const char *ipset_bin = "/usr/sbin/ipset";
    if (access(ipset_bin, X_OK) != 0) {
        fprintf(
            stderr,
            "[AutoBan] WARNING: %s không tồn tại hoặc không thực thi được. Bỏ qua ipset_init.\n",
            ipset_bin);
        return;
    }

    create_ipset(ipset_bin, "autoban_list", 0);
    create_ipset(ipset_bin, "autoban_list_v6", 1);
}
