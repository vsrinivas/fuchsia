// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_TEST_UTIL_H_
#define SRC_CONNECTIVITY_NETWORK_NETSTACK_UDP_SERDE_UDP_SERDE_TEST_UTIL_H_

#include <iostream>

#include "udp_serde.h"

constexpr fidl::Array<uint8_t, 4> kIPv4Addr = {0x1, 0x2, 0x3, 0x4};
constexpr fidl::Array<uint8_t, 16> kIPv6Addr = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                                                0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
constexpr uint8_t kIpTtl = 44;
constexpr uint8_t kIpv6Hoplimit = 46;

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
        return std::size(kIPv4Addr);
      case Kind::V6:
        return std::size(kIPv6Addr);
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

  // Returns a valid (for testing purposes) fsockeet::wire::SendMsgMeta FIDL message allocated on
  // the provided arena and constructed based on the encapsulated data. If `with_data` is `true`,
  // the message will be maximally filled with data; else, it will be minimally filled.
  fsocket::wire::SendMsgMeta GetFidl(fidl::Arena<kArenaCapacity>& alloc,
                                     bool with_data = true) const;

  // Returns a valid (for testing purposes) SendMsgMeta C struct constructed based on the
  // encapsulated data. The message will be maximally filled with data.
  SendMsgMeta GetCStruct() const;

  // Returns a pointer to the encapsulated address.
  const uint8_t* Addr() const;

  // Returns the length of the encapsulated address.
  size_t AddrLen() const;

  // Returns the `IpAddrType` of the encapsulated address.
  IpAddrType AddrType() const;

  // Returns the encapsulated port.
  uint16_t Port() const;

  // Returns the encapsulated cmsg set.
  SendAndRecvCmsgSet CmsgSet() const;

 private:
  AddrKind kind_;
};

// Returns a valid (for testing purposes) RecvMsgMeta and `from` address buffer
// of the provided AddrKind. If `with_data` is `true`, the returned `RecvMsgMeta`
// will be be maximally filled with data; else, it will be minimally filled.
std::pair<RecvMsgMeta, ConstBuffer> GetTestRecvMsgMeta(AddrKind::Kind kind, bool with_data = true);

#endif
