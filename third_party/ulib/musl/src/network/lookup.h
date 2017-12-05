#pragma once

#include <stddef.h>
#include <stdint.h>

struct address {
    int family;
    unsigned scopeid;
    uint8_t addr[16];
    int sortkey;
};

struct service {
    uint16_t port;
    unsigned char proto, socktype;
};

#define MAXNS 3

struct resolvconf {
    struct address ns[MAXNS];
    unsigned nns, attempts, ndots;
    unsigned timeout;
};

#define MAXSERVS 2

int __lookup_serv(struct service buf[static MAXSERVS], const char* name, int proto, int socktype,
                  int flags);
int __lookup_ipliteral(struct address buf[static 1], const char* name, int family);

int __get_resolv_conf(struct resolvconf*, char*, size_t);
