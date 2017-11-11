#include "lookup.h"
#include "stdio_impl.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int __dns_parse(const unsigned char*, int, int (*)(void*, int, const void*, int, const void*),
                void*);
int __dn_expand(const unsigned char*, const unsigned char*, const unsigned char*, char*, int);
int __res_mkquery(int, const char*, int, int, const unsigned char*, int, const unsigned char*,
                  unsigned char*, int);
int __res_send(const unsigned char*, int, unsigned char*, int);

#define PTR_MAX (64 + sizeof ".in-addr.arpa")
#define RR_PTR 12

static char* itoa(char* p, unsigned x) {
    p += 3 * sizeof(int);
    *--p = 0;
    do {
        *--p = '0' + x % 10;
        x /= 10;
    } while (x);
    return p;
}

static void mkptr4(char* s, const unsigned char* ip) {
    sprintf(s, "%d.%d.%d.%d.in-addr.arpa", ip[3], ip[2], ip[1], ip[0]);
}

static void mkptr6(char* s, const unsigned char* ip) {
    static const char xdigits[] = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; i--) {
        *s++ = xdigits[ip[i] & 15];
        *s++ = '.';
        *s++ = xdigits[ip[i] >> 4];
        *s++ = '.';
    }
    strcpy(s, "ip6.arpa");
}

static int dns_parse_callback(void* c, int rr, const void* data, int len, const void* packet) {
    if (rr != RR_PTR)
        return 0;
    if (__dn_expand(packet, (const unsigned char*)packet + 512, data, c, 256) <= 0)
        *(char*)c = 0;
    return 0;
}

int getnameinfo(const struct sockaddr* restrict sa, socklen_t sl, char* restrict node,
                socklen_t nodelen, char* restrict serv, socklen_t servlen, int flags) {
    char ptr[PTR_MAX];
    char buf[256], num[3 * sizeof(int) + 1];
    int af = sa->sa_family;
    unsigned char* a;
    unsigned scopeid;

    switch (af) {
    case AF_INET:
        a = (void*)&((struct sockaddr_in*)sa)->sin_addr;
        if (sl < sizeof(struct sockaddr_in))
            return EAI_FAMILY;
        mkptr4(ptr, a);
        scopeid = 0;
        break;
    case AF_INET6:
        a = (void*)&((struct sockaddr_in6*)sa)->sin6_addr;
        if (sl < sizeof(struct sockaddr_in6))
            return EAI_FAMILY;
        if (memcmp(a, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12))
            mkptr6(ptr, a);
        else
            mkptr4(ptr, a + 12);
        scopeid = ((struct sockaddr_in6*)sa)->sin6_scope_id;
        break;
    default:
        return EAI_FAMILY;
    }

    if (node && nodelen) {
        buf[0] = 0;
        if (!*buf && !(flags & NI_NUMERICHOST)) {
            unsigned char query[18 + PTR_MAX], reply[512];
            int qlen = __res_mkquery(0, ptr, 1, RR_PTR, 0, 0, 0, query, sizeof query);
            int rlen = __res_send(query, qlen, reply, sizeof reply);
            buf[0] = 0;
            if (rlen > 0)
                __dns_parse(reply, rlen, dns_parse_callback, buf);
        }
        if (!*buf) {
            if (flags & NI_NAMEREQD)
                return EAI_NONAME;
            inet_ntop(af, a, buf, sizeof buf);
            if (scopeid) {
                char *p = 0, tmp[IF_NAMESIZE + 1];
                if (!(flags & NI_NUMERICSCOPE) &&
                    (IN6_IS_ADDR_LINKLOCAL(a) || IN6_IS_ADDR_MC_LINKLOCAL(a)))
                    p = if_indextoname(scopeid, tmp + 1);
                if (!p)
                    p = itoa(num, scopeid);
                *--p = '%';
                strcat(buf, p);
            }
        }
        if (strlen(buf) >= nodelen)
            return EAI_OVERFLOW;
        strcpy(node, buf);
    }

    if (serv && servlen) {
        char* p = buf;
        int port = ntohs(((struct sockaddr_in*)sa)->sin_port);
        buf[0] = 0;
        if (!*p)
            p = itoa(num, port);
        if (strlen(p) >= servlen)
            return EAI_OVERFLOW;
        strcpy(serv, p);
    }

    return 0;
}
