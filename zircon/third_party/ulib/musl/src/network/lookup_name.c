#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lookup.h"
#include "stdio_impl.h"

static int is_valid_hostname(const char* host) {
  const unsigned char* s;
  if (strnlen(host, 255) - 1 >= 254 || mbstowcs(0, host, 0) == -1)
    return 0;
  for (s = (void*)host; *s >= 0x80 || *s == '.' || *s == '-' || isalnum(*s); s++)
    ;
  return !*s;
}

static int name_from_null(struct address buf[static 2], const char* name, int family, int flags) {
  int cnt = 0;
  if (name)
    return 0;
  if (flags & AI_PASSIVE) {
    if (family != AF_INET6)
      buf[cnt++] = (struct address){.family = AF_INET};
    if (family != AF_INET)
      buf[cnt++] = (struct address){.family = AF_INET6};
  } else {
    if (family != AF_INET6)
      buf[cnt++] = (struct address){.family = AF_INET, .addr = {127, 0, 0, 1}};
    if (family != AF_INET)
      buf[cnt++] = (struct address){.family = AF_INET6, .addr = {[15] = 1}};
  }
  return cnt;
}

static int name_from_numeric(struct address buf[static 1], const char* name, int family) {
  return __lookup_ipliteral(buf, name, family);
}

static int _getaddrinfo_from_dns_stub(struct address buf[static MAXADDRS], char canon[static 256],
                                      const char* name, int family) {
  return -1;
}

weak_alias(_getaddrinfo_from_dns_stub, _getaddrinfo_from_dns);

static const struct policy {
  unsigned char addr[16];
  unsigned char len, mask;
  unsigned char prec, label;
} defpolicy[] = {
    {"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1", 15, 0xff, 50, 0},
    {"\0\0\0\0\0\0\0\0\0\0\xff\xff", 11, 0xff, 35, 4},
    {"\x20\2", 1, 0xff, 30, 2},
    {"\x20\1", 3, 0xff, 5, 5},
    {"\xfc", 0, 0xfe, 3, 13},
#if 0
    /* These are deprecated and/or returned to the address
     * pool, so despite the RFC, treating them as special
     * is probably wrong. */
    { "", 11, 0xff, 1, 3 },
    { "\xfe\xc0", 1, 0xc0, 1, 11 },
    { "\x3f\xfe", 1, 0xff, 1, 12 },
#endif
    /* Last rule must match all addresses to stop loop. */
    {"", 0, 0, 40, 1},
};

static const struct policy* policyof(const struct in6_addr* a) {
  int i;
  for (i = 0;; i++) {
    if (memcmp(a->s6_addr, defpolicy[i].addr, defpolicy[i].len))
      continue;
    if ((a->s6_addr[defpolicy[i].len] & defpolicy[i].mask) != defpolicy[i].addr[defpolicy[i].len])
      continue;
    return defpolicy + i;
  }
}

static int labelof(const struct in6_addr* a) { return policyof(a)->label; }

static int scopeof(const struct in6_addr* a) {
  if (IN6_IS_ADDR_MULTICAST(a))
    return a->s6_addr[1] & 15;
  if (IN6_IS_ADDR_LINKLOCAL(a))
    return 2;
  if (IN6_IS_ADDR_LOOPBACK(a))
    return 2;
  if (IN6_IS_ADDR_SITELOCAL(a))
    return 5;
  return 14;
}

static int prefixmatch(const struct in6_addr* s, const struct in6_addr* d) {
  /* FIXME: The common prefix length should be limited to no greater
   * than the nominal length of the prefix portion of the source
   * address. However the definition of the source prefix length is
   * not clear and thus this limiting is not yet implemented. */
  unsigned i;
  for (i = 0; i < 128 && !((s->s6_addr[i / 8] ^ d->s6_addr[i / 8]) & (128 >> (i % 8))); i++)
    ;
  return i;
}

#define DAS_USABLE 0x40000000
#define DAS_MATCHINGSCOPE 0x20000000
#define DAS_MATCHINGLABEL 0x10000000
#define DAS_PREC_SHIFT 20
#define DAS_SCOPE_SHIFT 16
#define DAS_PREFIX_SHIFT 8
#define DAS_ORDER_SHIFT 0

static int addrcmp(const void* _a, const void* _b) {
  const struct address *a = _a, *b = _b;
  return b->sortkey - a->sortkey;
}

int __lookup_name(struct address buf[static MAXADDRS], char canon[static 256], const char* name,
                  int family, int flags) {
  int cnt = 0, i, j;

  *canon = 0;
  if (name) {
    /* reject empty name and check len so it fits into temp bufs */
    size_t l = strnlen(name, 255);
    if (l - 1 >= 254)
      return EAI_NONAME;
    memcpy(canon, name, l + 1);
  }

  /* Procedurally, a request for v6 addresses with the v4-mapped
   * flag set is like a request for unspecified family, followed
   * by filtering of the results. */
  if (flags & AI_V4MAPPED) {
    if (family == AF_INET6)
      family = AF_UNSPEC;
    else
      flags -= AI_V4MAPPED;
  }

  /* Try each backend until there's at least one result. */
  cnt = name_from_null(buf, name, family, flags);
  if (!cnt)
    cnt = name_from_numeric(buf, name, family);
  if (!cnt && !(flags & AI_NUMERICHOST)) {
    cnt = _getaddrinfo_from_dns(buf, canon, name, family);
  }
  if (cnt <= 0)
    return cnt ? cnt : EAI_NONAME;

  /* Filter/transform results for v4-mapped lookup, if requested. */
  if (flags & AI_V4MAPPED) {
    if (!(flags & AI_ALL)) {
      /* If any v6 results exist, remove v4 results. */
      for (i = 0; i < cnt && buf[i].family != AF_INET6; i++)
        ;
      if (i < cnt) {
        for (j = 0; i < cnt; i++) {
          if (buf[i].family == AF_INET6)
            buf[j++] = buf[i];
        }
        cnt = i = j;
      }
    }
    /* Translate any remaining v4 results to v6 */
    for (i = 0; i < cnt; i++) {
      if (buf[i].family != AF_INET)
        continue;
      memcpy(buf[i].addr + 12, buf[i].addr, 4);
      memcpy(buf[i].addr, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12);
      buf[i].family = AF_INET6;
    }
  }

  /* No further processing is needed if there are fewer than 2
   * results or if there are only IPv4 results. */
  if (cnt < 2 || family == AF_INET)
    return cnt;
  for (i = 0; buf[i].family == AF_INET; i++)
    if (i == cnt)
      return cnt;

  /* The following implements a subset of RFC 3484/6724 destination
   * address selection by generating a single 31-bit sort key for
   * each address. Rules 3, 4, and 7 are omitted for having
   * excessive runtime and code size cost and dubious benefit.
   * So far the label/precedence table cannot be customized. */
  for (i = 0; i < cnt; i++) {
    int key = 0;
    struct sockaddr_in6 sa,
        da = {.sin6_family = AF_INET6, .sin6_scope_id = buf[i].scopeid, .sin6_port = 65535};
    if (buf[i].family == AF_INET6) {
      memcpy(da.sin6_addr.s6_addr, buf[i].addr, 16);
    } else {
      memcpy(da.sin6_addr.s6_addr, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12);
      memcpy(da.sin6_addr.s6_addr + 12, buf[i].addr, 4);
    }
    const struct policy* dpolicy = policyof(&da.sin6_addr);
    int dscope = scopeof(&da.sin6_addr);
    int dlabel = dpolicy->label;
    int dprec = dpolicy->prec;
    int prefixlen = 0;
    int fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (fd >= 0) {
      if (!connect(fd, (void*)&da, sizeof da)) {
        key |= DAS_USABLE;
        if (!getsockname(fd, (void*)&sa, &(socklen_t){sizeof sa})) {
          if (dscope == scopeof(&sa.sin6_addr))
            key |= DAS_MATCHINGSCOPE;
          if (dlabel == labelof(&sa.sin6_addr))
            key |= DAS_MATCHINGLABEL;
          prefixlen = prefixmatch(&sa.sin6_addr, &da.sin6_addr);
        }
      }
      close(fd);
    }
    key |= dprec << DAS_PREC_SHIFT;
    key |= (15 - dscope) << DAS_SCOPE_SHIFT;
    key |= prefixlen << DAS_PREFIX_SHIFT;
    key |= (MAXADDRS - i) << DAS_ORDER_SHIFT;
    buf[i].sortkey = key;
  }
  qsort(buf, cnt, sizeof *buf, addrcmp);

  return cnt;
}
