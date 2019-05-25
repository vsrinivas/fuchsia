// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"

#include <arpa/inet.h>
#include <string.h>

#include <ostream>

namespace overnet {

#ifndef __Fuchsia__
Optional<IpAddr> IpAddr::Unix(const std::string& name) {
  IpAddr out;
  if (name.length() >= sizeof(out.unix.sun_path)) {
    return Nothing;
  }
  memset(&out, 0, sizeof(out));
  out.unix.sun_family = AF_UNIX;
  memcpy(out.unix.sun_path, name.data(), name.length());
  return out;
}
#endif

IpAddr IpAddr::AnyIpv4() {
  IpAddr out;
  memset(&out, 0, sizeof(out));
  out.ipv4.sin_family = AF_INET;
  return out;
}

IpAddr IpAddr::AnyIpv6() {
  IpAddr out;
  memset(&out, 0, sizeof(out));
  out.ipv6.sin6_family = AF_INET6;
  return out;
}

socklen_t IpAddr::length() const {
  switch (addr.sa_family) {
    case AF_INET:
      return sizeof(ipv4);
    case AF_INET6:
      return sizeof(ipv6);
#ifndef __Fuchsia__
    case AF_UNIX:
      return SUN_LEN(&unix);
#endif
    default:
      return sizeof(*this);
  }
}

Optional<IpAddr> IpAddr::WithPort(uint16_t port) const {
  IpAddr out = *this;
  switch (out.addr.sa_family) {
    case AF_INET:
      out.ipv4.sin_port = htons(port);
      return out;
    case AF_INET6:
      out.ipv6.sin6_port = htons(port);
      return out;
    default:
      return Nothing;
  }
}

Optional<IpAddr> IpAddr::AsIpv6() const {
  switch (addr.sa_family) {
    case AF_INET6:
      return *this;
    default:
      return Nothing;
    case AF_INET: {
      auto out = AnyIpv6();
      out.ipv6.sin6_port = ipv4.sin_port;
      out.ipv6.sin6_addr.s6_addr[10] = 0xff;
      out.ipv6.sin6_addr.s6_addr[11] = 0xff;
      memcpy(out.ipv6.sin6_addr.s6_addr + 12, &ipv4.sin_addr,
             sizeof(ipv4.sin_addr));
      return out;
    }
  }
}

std::ostream& operator<<(std::ostream& out, IpAddr addr) {
  char dst[512];
  switch (addr.addr.sa_family) {
    case AF_INET:
      inet_ntop(AF_INET, &addr.ipv4.sin_addr, dst, sizeof(dst));
      return out << dst << ":" << ntohs(addr.ipv4.sin_port);
    case AF_INET6:
      inet_ntop(AF_INET6, &addr.ipv6.sin6_addr, dst, sizeof(dst));
      return out << dst << "." << ntohs(addr.ipv6.sin6_port);
#ifndef __Fuchsia__
    case AF_UNIX:
      return out << addr.unix.sun_path;
#endif
    default:
      return out << "<<unknown address family " << addr.addr.sa_family << ">>";
  }
}

size_t HashIpAddr::operator()(const IpAddr& addr) const {
  size_t out = 0;
  auto add_value = [&out](auto x) {
    const char* p = reinterpret_cast<const char*>(&x);
    const char* end = reinterpret_cast<const char*>(1 + &x);
    while (p != end) {
      out = 257 * out + *p++;
    }
  };
  switch (addr.addr.sa_family) {
    case AF_INET:
      add_value(addr.ipv4.sin_addr);
      add_value(addr.ipv4.sin_port);
      break;
    case AF_INET6:
      add_value(addr.ipv6.sin6_addr);
      add_value(addr.ipv6.sin6_port);
      break;
#ifndef __Fuchsia__
    case AF_UNIX:
      add_value(addr.unix.sun_path);
      break;
#endif
  }
  return out;
}

bool EqIpAddr::operator()(const IpAddr& a, const IpAddr& b) const {
  if (a.addr.sa_family == b.addr.sa_family) {
    switch (a.addr.sa_family) {
      case AF_INET:
        return a.ipv4.sin_port == b.ipv4.sin_port &&
               0 == memcmp(&a.ipv4.sin_addr, &b.ipv4.sin_addr,
                           sizeof(a.ipv4.sin_addr));
      case AF_INET6:
        return a.ipv6.sin6_port == b.ipv6.sin6_port &&
               0 == memcmp(&a.ipv6.sin6_addr, &b.ipv6.sin6_addr,
                           sizeof(a.ipv6.sin6_addr));
#ifndef __Fuchsia__
      case AF_UNIX:
        return 0 == strcmp(a.unix.sun_path, b.unix.sun_path);
#endif
    }
  }
  if (auto a6 = a.AsIpv6(); a6.has_value()) {
    if (auto b6 = b.AsIpv6(); b6.has_value()) {
      return a6 == b6;
    }
  }
  return false;
}

}  // namespace overnet
