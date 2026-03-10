#ifndef AUTOBAN_JOURNAL_H
#define AUTOBAN_JOURNAL_H

#include "config.h"

/* Parse dòng log (Nginx rate-limit hoặc regex custom), gọi block_ip nếu match. */
void parse_nginx_log(AppConfig *config, const char *message);

#endif /* AUTOBAN_JOURNAL_H */
