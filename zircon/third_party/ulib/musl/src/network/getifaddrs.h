#ifndef ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_NETWORK_GETIFADDRS_H_
#define ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_NETWORK_GETIFADDRS_H_

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "netlink.h"

#define IFADDRS_HASH_SIZE 64

/* getifaddrs() reports hardware addresses with PF_PACKET that implies
 * struct sockaddr_ll.  But e.g. Infiniband socket address length is
 * longer than sockaddr_ll.ssl_addr[8] can hold. Use this hack struct
 * to extend ssl_addr - callers should be able to still use it. */
struct sockaddr_ll_hack {
  unsigned short sll_family, sll_protocol;
  int sll_ifindex;
  unsigned short sll_hatype;
  unsigned char sll_pkttype;
  socklen_t sll_halen;
  unsigned char sll_addr[24];
};

union sockany {
  struct sockaddr sa;
  struct sockaddr_ll_hack ll;
  struct sockaddr_in v4;
  struct sockaddr_in6 v6;
};

struct ifaddrs_storage {
  struct ifaddrs ifa;
  struct ifaddrs_storage* hash_next;
  union sockany addr, netmask, ifu;
  unsigned int index;
  char name[IFNAMSIZ + 1];
};

struct ifaddrs_ctx {
  struct ifaddrs_storage* first;
  struct ifaddrs_storage* last;
  struct ifaddrs_storage* hash[IFADDRS_HASH_SIZE];
};

static inline void copy_addr(struct sockaddr** r, sa_family_t af, union sockany* sa, void* addr,
                             socklen_t addrlen, uint32_t ifindex) {
  uint8_t* dst;
  size_t len;

  switch (af) {
    case AF_INET:
      dst = (uint8_t*)&sa->v4.sin_addr;
      len = 4;
      break;
    case AF_INET6:
      dst = (uint8_t*)&sa->v6.sin6_addr;
      len = 16;
      if (IN6_IS_ADDR_LINKLOCAL(addr) || IN6_IS_ADDR_MC_LINKLOCAL(addr))
        sa->v6.sin6_scope_id = ifindex;
      break;
    default:
      return;
  }
  if (addrlen < len)
    return;
  sa->sa.sa_family = af;
  memcpy(dst, addr, len);
  *r = &sa->sa;
}

static inline void gen_netmask(struct sockaddr** r, sa_family_t af, union sockany* sa,
                               uint8_t prefixlen) {
  uint8_t addr[16] = {};
  size_t i;

  if (prefixlen > 8 * sizeof(addr))
    prefixlen = 8 * sizeof(addr);
  i = prefixlen / 8;
  memset(addr, 0xff, i);
  if (i < sizeof(addr))
    addr[i++] = (uint8_t)(0xff << (8 - (prefixlen % 8)));
  copy_addr(r, af, sa, addr, sizeof(addr), 0);
}

static inline void copy_lladdr(struct sockaddr** r, union sockany* sa, void* addr,
                               socklen_t addrlen, uint32_t ifindex, unsigned short hatype) {
  if (addrlen > sizeof(sa->ll.sll_addr))
    return;
  sa->ll.sll_family = AF_PACKET;
  sa->ll.sll_ifindex = ifindex;
  sa->ll.sll_hatype = hatype;
  sa->ll.sll_halen = addrlen;
  memcpy(sa->ll.sll_addr, addr, addrlen);
  *r = &sa->sa;
}

#endif  // ZIRCON_THIRD_PARTY_ULIB_MUSL_SRC_NETWORK_GETIFADDRS_H_
