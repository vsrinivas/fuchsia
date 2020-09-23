// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "_cgo_export.h"
#include "dnssdfinder.h"

#ifdef __APPLE__
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/select.h>

static void dnsBrowseCallback(DNSServiceRef sdref, const DNSServiceFlags flags, uint32_t ifIndex,
                              DNSServiceErrorType errorCode, const char *replyName,
                              const char *replyType, const char *replyDomain, void *context) {
  browseCallbackGoFunc(errorCode, (char *)replyName, context);
}

static void dnsResolveCallback(DNSServiceRef sdref, DNSServiceFlags flags, uint32_t ifIndex,
                               DNSServiceErrorType errorCode, const char *fullname,
                               const struct sockaddr *address, uint32_t ttl, void *context) {
  char ip[INET6_ADDRSTRLEN];
  uint32_t zoneIdx = 0;
  switch (address->sa_family) {
    case AF_INET:
      inet_ntop(AF_INET, &(((struct sockaddr_in *)(address))->sin_addr), ip, INET6_ADDRSTRLEN);
      break;
    case AF_INET6:
      inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)(address))->sin6_addr), ip, INET6_ADDRSTRLEN);
      zoneIdx = ((struct sockaddr_in6 *)(address))->sin6_scope_id;
      break;
    default:
      break;
  }
  resolveCallbackGoFunc(errorCode, (char *)fullname, ip, zoneIdx, context);
}

int dnsBrowse(char *domain, DNSServiceRef *ref, void *ctx) {
  DNSServiceFlags flags = 0;
  return DNSServiceBrowse(ref, flags, 0, domain, NULL, dnsBrowseCallback, ctx);
}

int dnsProcessResults(DNSServiceRef ref) { return DNSServiceProcessResult(ref); }

int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds, int *err_out) {
  int fd = DNSServiceRefSockFD(ref);
  int nfds = fd + 1;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = timeout_milliseconds * 1000;
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  int result = select(nfds, &readfds, (fd_set *)NULL, (fd_set *)NULL, &tv);
  if (result > 0) {
    return FD_ISSET(fd, &readfds) > 0;
  } else if (result == 0) {
    return 0;
  } else {
    if (err_out != NULL) {
      *err_out = errno;
    }
    return -1;
  }
}

int dnsResolve(char *hostname, DNSServiceRef *ref, bool ipv4, bool ipv6, void *ctx) {
  DNSServiceFlags flags = 0;
  DNSServiceProtocol protocol = 0;
  if (ipv4) {
    protocol |= kDNSServiceProtocol_IPv4;
  }
  if (ipv6) {
    protocol |= kDNSServiceProtocol_IPv6;
  }
  // DNSServiceResolve returns only the hostname. On Fuchsia targets the mDNS
  // domain name and the hostname are identical (plus or minus a trailing
  // ".local"), so this skips straight to resolving the IP address.
  //
  // TODO(fxbug.dev/43928)
  return DNSServiceGetAddrInfo(ref, flags, 0, protocol, hostname, dnsResolveCallback, ctx);
}

int dnsAllocate(DNSServiceRef *ref) { return DNSServiceCreateConnection(ref); }

void dnsDeallocate(DNSServiceRef ref) { DNSServiceRefDeallocate(ref); }

#else
#include <stdio.h>

int dnsBrowse(char *domain, DNSServiceRef *ref, void *ctx) {
  fprintf(stderr, "dnsBrowse must be compiled and invoked on darwin\n");
  return -1;
}

int dnsResolve(char *hostname, DNSServiceRef *ref, bool ipv4, bool ipv6, void *ctx) {
  fprintf(stderr, "dnsResolve must be compiled and invoked on darwin\n");
  return -1;
}

int dnsProcessResults(DNSServiceRef ref) {
  fprintf(stderr, "dnsProcessResults must be compiled and invoked on darwin\n");
  return -1;
}

int dnsPollDaemon(DNSServiceRef ref, int timeout_milliseconds, int *err_out) {
  fprintf(stderr, "dnsPollDaemon must be compiled and invoked on darwin\n");
  return -1;
}

int dnsAllocate(DNSServiceRef *ref) {
  fprintf(stderr, "dnsAllocate must be compiled and invoked on darwin\n");
  return -1;
}

void dnsDeallocate(DNSServiceRef ref) {}
#endif
