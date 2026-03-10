#ifndef AUTOBAN_CONTROL_SOCKET_H
#define AUTOBAN_CONTROL_SOCKET_H

#include "common.h"

/* Thread xử lý Unix socket: status, reload, flush. arg = AutobanContext*. */
void *control_socket_thread(void *arg);

#endif /* AUTOBAN_CONTROL_SOCKET_H */
