// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

#include <arpa/inet.h>
#include <endian.h>

namespace netconnector {

// Represents an IP port number.
//
// Ports are not treated as 16-bit integers by the network stack, meaning that,
// on little-endian targets, the two bytes will be reversed. For example, the
// mDNS multicast port is 5353 decimal or 14E9 hex. On arrival on a little-
// endian machine, the bytes are laid down in the order 14, E9 resulting in
// the integer value E914.
//
// This class exists primarily to alleviate the confusion this causes.
//
// The types |in_port_t| and |uint16_t| are interpreted differently, in spite
// of the fact that they have the same underlying type (hence the From_*
// static methods rather than constructors). |in_port_t| is assumed to be
// big-endian (in network order) consistent with the way the network stack
// produces and consumes port values. |uint16_t| is assumed to be host-endian.
// This means that creating an |IpPort| constant from a |uint16_t| literal
// should be done using |From_uint16_t|.
//
// An |ostream| operator<< overload is provided so |IpPort| values are
// represented properly as text. It just writes |value.as_uint16_t()|.
class IpPort {
 public:
  // Creates an |IpPort| from an |in_port_t|. This assumes that the value is
  // big-endian (network order). Use this method when creating an |IpPort|
  // from |in_port_t| values produced by the network stack.
  static IpPort From_in_port_t(in_port_t port) { return IpPort(port); }

  // Creates an |IpPort| from a |uint16_t|. This assumes that the value is
  // host-endian. Use this method when creating |IpPort| from host-endian
  // |uint16_t| values, such as literals.
  static IpPort From_uint16_t(uint16_t port_as_uint16_t) {
    return IpPort(htobe16(port_as_uint16_t));
  }

  // Creates an invalid IpPort.
  IpPort();

  // Creates a port from two bytes in network order.
  IpPort(uint8_t b0, uint8_t b1);

  bool is_valid() const { return value_ != 0; }

  // Returns an |in_port_t| value for the port. This value is big-endian
  // (network order), suitable for consumption by the network stack.
  in_port_t as_in_port_t() const { return value_; }

  // Returns a |uint16_t| value for the port. This value is host-endian,
  // suitable for displaying to humans, etc.
  uint16_t as_uint16_t() const { return be16toh(value_); }

  explicit operator bool() const { return is_valid(); }

  bool operator==(const IpPort& other) const {
    return as_in_port_t() == other.as_in_port_t();
  }

  bool operator!=(const IpPort& other) const {
    return as_in_port_t() != other.as_in_port_t();
  }

 private:
  explicit IpPort(in_port_t port) : value_(port) {}

  in_port_t value_;  // big-endian
};

std::ostream& operator<<(std::ostream& os, IpPort value);

}  // namespace netconnector
