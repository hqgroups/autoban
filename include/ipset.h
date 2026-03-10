#ifndef AUTOBAN_IPSET_H
#define AUTOBAN_IPSET_H

/* Tạo ipset autoban_list (IPv4) và autoban_list_v6 (IPv6) nếu chưa tồn tại. */
void ipset_init(void);

#endif /* AUTOBAN_IPSET_H */
