// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "addr.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>

#include "log.h"
#include "util.h"

constexpr const char* kInterfaceIdPrefix = "%";
constexpr const char* kAny = "ANY";
constexpr const char* kAny6 = "ANY6";
constexpr const char* kAnyLower = "any";
constexpr const char* kAny6Lower = "any6";
constexpr const char* kNull = "NULL";
constexpr const char* kNullLower = "null";

namespace {
inline bool IsAny4(const std::string& str) { return str == kAny || str == kAnyLower; }

inline bool IsAny6(const std::string& str) { return str == kAny6 || str == kAny6Lower; }

inline bool IsNull(const std::string& str) { return str == kNull || str == kNullLower; }
}  // namespace

bool InAddr::Set(const std::string& ip_str_arg) {
  Reset();

  if (ip_str_arg.empty()) {
    LOG(ERROR) << "Error: Empty-string given to create InAddr!";
    return false;
  }

  std::string ip_str;
  if (ip_str_arg[0] == '[') {
    auto e = ip_str_arg.find_first_of(']');
    if (e == std::string::npos) {
      LOG(ERROR) << "Error-Given InAddr ip_str_arg='" << ip_str_arg
                 << "' is not a valid interface ID or IP - "
                 << "missing closing address bracket ']'!";
      return false;
    }
    ip_str = ip_str_arg.substr(1, e - 1);
    LOG(ERROR) << "ip_str='" << ip_str << " e=" << e;
  } else {
    ip_str = ip_str_arg;
  }

  if (IsAny4(ip_str)) {
    family_ = AF_INET;
    addr_.addr4_.s_addr = htonl(INADDR_ANY);
  } else if (IsAny6(ip_str)) {
    family_ = AF_INET6;
    addr_.addr6_ = in6addr_any;
  } else if (IsNull(ip_str)) {
    Reset();
  } else if (inet_pton(AF_INET, ip_str.c_str(), &addr_.addr4_) == 1) {
    family_ = AF_INET;
  } else if (inet_pton(AF_INET6, ip_str.c_str(), &addr_.addr6_) == 1) {
    family_ = AF_INET6;
  } else {
    LOG(ERROR) << "Error-Given InAddr ip_str_arg='" << ip_str_arg
               << "' is not a valid interface ID or IP!";
    return false;
  }
  // Store the original string.
  ip_str_ = ip_str;
  return true;
}

bool InAddr::Set(const void* addr, int addr_len) {
  Reset();
  char ip_str[64];
  if (addr_len == sizeof(addr_.addr4_)) {
    family_ = AF_INET;
    addr_.addr4_ = *static_cast<const struct in_addr*>(addr);
  } else if (addr_len == sizeof(addr_.addr6_)) {
    family_ = AF_INET6;
    addr_.addr6_ = *static_cast<const struct in6_addr*>(addr);
  } else {
    LOG(ERROR) << "Error-given in-addr is not valid!";
    return false;
  }
  if (!inet_ntop(family_, addr, ip_str, sizeof(ip_str))) {
    LOG(ERROR) << "Error-inet_ntop failed-"
               << "[" << errno << "]" << strerror(errno);
    return false;
  }

  ip_str_.assign(ip_str);
  return true;
}

std::string InAddr::Name() {
  std::string name;
  switch (family_) {
    case AF_INET:
      return addr_.addr4_.s_addr == INADDR_ANY ? std::string(kAny) : ip_str_;
    case AF_INET6:
      for (unsigned i = 0; i < sizeof(addr_.addr6_); i++) {
        if (addr_.addr6_.s6_addr[i] != 0) {
          return std::string("[") + ip_str_ + "]";
        }
      }
      return std::string(kAny6);
    case AF_UNSPEC:
      return std::string("<unspec>");
    default:
      return std::string("<unknown>");
  }
}

bool LocalIfAddr::Set(const std::string& ip_id_str) {
  Reset();
  if (ip_id_str.empty()) {
    LOG(ERROR) << "Error: Empty string given to create LocalIfAddr!";
    return false;
  }

  if (IsAny4(ip_id_str)) {
    id_ = 0;
    return in_addr_.Set(kAny);
  }

  // [<IP>][%ID]
  std::string ip_str, id_str;
  if (ip_id_str[0] == kInterfaceIdPrefix[0]) {
    id_str = ip_id_str.substr(1);
  } else {
    auto e = ip_id_str.find_first_of(kInterfaceIdPrefix[0]);
    if (e != std::string::npos) {
      id_str = ip_id_str.substr(e + 1);
    }
    ip_str = ip_id_str.substr(0, e);
  }

  if (!ip_str.empty()) {
    if (!in_addr_.Set(ip_str)) {
      return false;
    }
  }
  if (!id_str.empty()) {
    int id;
    if (!str2int(id_str, &id) || id < 0) {
      LOG(ERROR) << "Error: Invalid interface ID='" << id_str << "'!";
      return false;
    }
    id_ = id;
  }

  return true;
}

bool LocalIfAddr::Set(const void* addr, int addr_len) {
  Reset();
  return in_addr_.Set(addr, addr_len);
}

std::string LocalIfAddr::Name() {
  if (!IsSet()) {
    return std::string("<unknown>");
  }

  std::string out;
  if (in_addr_.IsSet()) {
    out += in_addr_.Name();
  }
  if (HasId()) {
    out += std::string(kInterfaceIdPrefix) + std::to_string(id_);
  }
  return out;
}

bool SockAddrIn::Set(const std::string& ip_port_str) {
  if (ip_port_str.empty()) {
    LOG(ERROR) << "Error: Empty string given to create SockAddrIn!";
    return false;
  }

  if (ip_port_str == "null" || ip_port_str == "NULL") {
    port_ = 0;
    addr_.Reset();
    return true;
  }

  std::string ip_str;
  size_t col_pos = 0;
  if (ip_port_str[0] == '[') {
    auto addr_end_pos = ip_port_str.find_first_of(']');
    if (addr_end_pos == std::string::npos) {
      LOG(ERROR) << "Error-Cannot parse ip_port_str='" << ip_port_str
                 << "' for <ip>:<port> - missing address closing brack ']'!";
      return false;
    }
    ip_str = ip_port_str.substr(1, addr_end_pos - 1);
    col_pos = addr_end_pos + 1;
  } else {
    col_pos = ip_port_str.find_first_of(':');
    if (col_pos == std::string::npos) {
      LOG(ERROR) << "Error-Cannot parse ip_port_str='" << ip_port_str
                 << "' for <ip>:<port> - missing port!";
      return false;
    }
    if (ip_port_str.find_first_of(':', col_pos + 1) != std::string::npos) {
      LOG(ERROR) << "Error-Cannot parse ip_port_str='" << ip_port_str
                 << "' for <ip>:<port> - too many colons ':',"
                 << " use '[]' brackets around IPv6 addresses!";
      return false;
    }
    ip_str = ip_port_str.substr(0, col_pos);
  }

  if (!addr_.Set(ip_str)) {
    return false;
  }

  std::string port_str = ip_port_str.substr(col_pos + 1);
  if (port_str.empty()) {
    LOG(ERROR) << "Error-Cannot parse ip_port_str='" << ip_port_str
               << "' for <ip>:<port> - port_str is empty!";
  }

  int port;
  if (!str2int(port_str, &port) || port < 0) {
    LOG(ERROR) << "Error-Cannot parse port='" << port_str << "'!";
    return false;
  }
  port_ = static_cast<uint16_t>(port);
  return true;
}

bool SockAddrIn::Set(const struct sockaddr* addr, socklen_t addr_len) {
  if (addr->sa_family == AF_INET && addr_len == sizeof(struct sockaddr_in)) {
    auto* addr_in = reinterpret_cast<const struct sockaddr_in*>(addr);
    if (!addr_.Set(&addr_in->sin_addr, sizeof(struct in_addr))) {
      return false;
    }
    port_ = ntohs(addr_in->sin_port);
  } else if (addr->sa_family == AF_INET && addr_len == sizeof(struct sockaddr_in6)) {
    auto* addr_in = reinterpret_cast<const struct sockaddr_in6*>(addr);
    if (!addr_.Set(&addr_in->sin6_addr, sizeof(struct in6_addr))) {
      return false;
    }
    port_ = ntohs(addr_in->sin6_port);
  } else if (addr->sa_family == AF_UNSPEC) {
    port_ = 0;
    addr_.Reset();
  } else {
    LOG(ERROR) << "Error-Invalid sockaddr provided!";
    return false;
  }
  return true;
}

bool SockAddrIn::Fill(struct sockaddr* sockaddr, int* sockaddr_len, bool allow_unspec) {
  int actual_len;
  if (addr_.IsAddr4()) {
    actual_len = sizeof(struct sockaddr_in);
  } else if (addr_.IsAddr6()) {
    actual_len = sizeof(struct sockaddr_in6);
  } else if (allow_unspec && !addr_.IsSet()) {
    actual_len = sizeof(sa_family_t);
  } else {
    LOG(ERROR) << "Error-SockAddress object is not initialized!";
    return false;
  }
  if (*sockaddr_len < actual_len) {
    LOG(ERROR) << "Error-Sockaddr given is too small, need " << actual_len << " have "
               << *sockaddr_len;
    return false;
  }

  memset(sockaddr, 0, actual_len);
  if (addr_.IsAddr4()) {
    auto* addr_in = reinterpret_cast<struct sockaddr_in*>(sockaddr);
    addr_in->sin_family = addr_.GetFamily();
    addr_in->sin_port = htons(port_);
    addr_in->sin_addr = addr_.GetAddr4();
  } else if (addr_.IsAddr6()) {
    auto* addr_in = reinterpret_cast<struct sockaddr_in6*>(sockaddr);
    addr_in->sin6_family = addr_.GetFamily();
    addr_in->sin6_port = htons(port_);
    // addr->sin6_flowinfo
    addr_in->sin6_addr = addr_.GetAddr6();
    // addr->sin6_scope_id
  } else {
    sockaddr->sa_family = AF_UNSPEC;
  }

  *sockaddr_len = actual_len;
  return true;
}
