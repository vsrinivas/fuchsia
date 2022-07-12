// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_TEST_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_TEST_UTIL_H_

#include <iostream>

#include "udp_serde.h"

constexpr size_t kIPv4AddrLen = 4;
constexpr size_t kIPv6AddrLen = 16;

class AddrKind {
 public:
  enum class Kind {
    V4,
    V6,
  };

  explicit AddrKind(enum Kind kind) : kind_(kind) {}
  IpAddrType ToAddrType() const;

  constexpr Kind GetKind() const { return kind_; }

  constexpr size_t Len() const {
    switch (kind_) {
      case Kind::V4:
        return kIPv4AddrLen;
      case Kind::V6:
        return kIPv6AddrLen;
    }
  }

  constexpr const char* ToString() const {
    switch (kind_) {
      case Kind::V4:
        return "IPv4";
      case Kind::V6:
        return "IPv6";
    }
  }

 private:
  enum Kind kind_;
};

class span : public cpp20::span<const uint8_t> {
 public:
  span(const uint8_t* data, size_t size) : cpp20::span<const uint8_t>(data, size) {}

  template <std::size_t N>
  explicit constexpr span(element_type (&arr)[N]) noexcept : cpp20::span<const uint8_t>(arr) {}

  bool operator==(const span& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

  friend std::ostream& operator<<(std::ostream& out, const span& buf);
};

// Test class encapsulating send-path metadata for the AddrKind::Kind provided
// on construction.
class TestSendMsgMeta {
 public:
  explicit TestSendMsgMeta(AddrKind::Kind kind) : kind_(kind) {}
  static constexpr size_t kArenaCapacity = 512;

  // Returns a valid (for testing purposes) SendMsgMeta FIDL message allocated on the provided
  // arena constructed based on the encapsulated data. If `with_data` is `true`, the message will
  // be maximally filled with data; else, it will be minimally filled.
  fsocket::wire::SendMsgMeta Get(fidl::Arena<kArenaCapacity>& alloc, bool with_data = true) const;

  // Returns a pointer to the encapsulated address.
  const uint8_t* Addr() const;

  // Returns the length of the encapsulated address.
  size_t AddrLen() const;

  // Returns the `IpAddrType` of the encapsulated address.
  IpAddrType AddrType() const;

  // Returns the encapsulated port.
  uint16_t Port() const;

 private:
  AddrKind kind_;
};

// Returns a valid (for testing purposes) RecvMsgMeta and `from` address buffer
// of the provided AddrKind. If `with_data` is `true`, the returned `RecvMsgMeta`
// will be be maximally filled with data; else, it will be minimally filled.
std::pair<RecvMsgMeta, ConstBuffer> GetTestRecvMsgMeta(AddrKind::Kind kind, bool with_data = true);

#endif
