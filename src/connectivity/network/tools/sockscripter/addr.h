// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_
#define SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_

#include <netinet/in.h>

#include <string>

// Helper class to manage an interface address.
class InAddr {
 public:
  InAddr() { Reset(); }

  void Reset() { family_ = AF_UNSPEC; }

  sa_family_t GetFamily() const { return family_; }

  bool IsSet() { return family_ != AF_UNSPEC; }

  bool IsAddr4() { return family_ == AF_INET; }

  bool IsAddr6() { return family_ == AF_INET6; }

  struct in_addr GetAddr4() {
    return addr_.addr4_;
  }

  struct in6_addr GetAddr6() {
    return addr_.addr6_;
  }

  // Parse the given string argument to contain an IP address (v4 or v6), and store it.
  bool Set(const std::string& ip_str_arg);
  bool Set(const void* addr, int addr_len);
  std::string Name();

 private:
  std::string ip_str_;
  sa_family_t family_;
  // Tagged union storing an address, using family_ as the tag.
  union {
    struct in_addr addr4_;
    struct in6_addr addr6_;
  } addr_;
};

// Helper that implements a local interface address that consists of an IP, an
// ID, or both.
class LocalIfAddr {
 public:
  LocalIfAddr() { Reset(); }

  void Reset() {
    in_addr_.Reset();
    id_ = 0;
  }

  bool IsSet() { return in_addr_.IsSet() || HasId(); }

  bool HasAddr4() { return in_addr_.IsAddr4(); }

  bool HasAddr6() { return in_addr_.IsAddr6(); }

  bool HasId() { return id_ > 0; }

  int GetId() { return id_; }

  struct in_addr GetAddr4() {
    return in_addr_.GetAddr4();
  }

  struct in6_addr GetAddr6() {
    return in_addr_.GetAddr6();
  }

  bool Set(const std::string& ip_id_str);

  bool Set(const void* addr, int addr_len);

  std::string Name();

 private:
  int id_;
  InAddr in_addr_;
};

// Helper class to store a socket address (address + port) and fill in a
// provided sockaddr struct.
class SockAddrIn {
 public:
  SockAddrIn() = default;

  std::string Name() { return addr_.Name() + ":" + std::to_string(port_); }

  bool Set(const std::string& ip_port_str);

  bool Set(const struct sockaddr* addr, socklen_t addr_len);

  // Fills the provided socket address structure and returns its new size.
  bool Fill(struct sockaddr* sockaddr, int* sockaddr_len, bool allow_unspec = false);

 private:
  InAddr addr_;
  uint16_t port_;
};

#endif  // SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_ADDR_H_
