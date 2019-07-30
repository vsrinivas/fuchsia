// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file specifies a pure virtual interface, `FilterBuilder`, of filter operations the parser
// may emit. This interface is intended to be reflective of the filter language the parser supports,
// rather than be tied to any particular filter implementation. Therefore it should evolve,
// for example becoming more abstract and/or flexible, as the filter language becomes more
// expressive. During parse, the client must pass a concrete implementation of `FilterBuilder` to
// the parser in order to construct filters.

#ifndef SRC_CONNECTIVITY_NETWORK_NETDUMP_FILTER_BUILDER_H_
#define SRC_CONNECTIVITY_NETWORK_NETDUMP_FILTER_BUILDER_H_

#include <array>
#include <type_traits>

#include "tokens.h"

namespace netdump::parser {

// As a filter string is parsed, functions declared in this interface are called to emit filters of
// type `T`. Parsed data is supplied to `FilterBuilder` in host byte order. It is the job of the
// builder implementations to rearrange the data into the order required by filters. The internal
// state of a `FilterBuilder` is also allowed to change during a parse. The filter operation methods
// are therefore not marked `const`, and the correct way to pass a `FilterBuilder` is by pointer
// rather than `const` reference.
template <class T>
class FilterBuilder {
  // During parsing, filter objects will pass between the parser and `FilterBuilder`.
  // In general the objects will be moved, and the parser requires `T` to be a move constructible
  // and move assignable type. This does not prohibit a copyable `T`.
  static_assert(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
                "Parser will move construct and move assign filter objects.");

 public:
  // Filter operation methods. Parameters to these methods are:
  // - Literal data that was parsed from user input, such as a protocol number.
  // - The qualifier keyword token encountered that specifies the type of data, such as `src`.

  // Frame length.
  virtual T frame_length(uint16_t length, TokenPtr comparator) = 0;

  // Ethernet II ethertype.
  virtual T ethertype(uint16_t type) = 0;
  // MAC address expression.
  virtual T mac(std::array<uint8_t, ETH_ALEN> address, TokenPtr addr_type) = 0;

  // IP version. In this and following functions, the parser guarantees `version` is 4 or 6.
  virtual T ip_version(uint8_t version) = 0;
  // IP packet length.
  virtual T ip_pkt_length(uint8_t version, uint16_t length, TokenPtr comparator) = 0;
  // IP protocol number.
  virtual T ip_protocol(uint8_t version, uint8_t protocol) = 0;
  // IPv4 address.
  virtual T ipv4_address(uint32_t address, TokenPtr type) = 0;
  // IPv6 address.
  virtual T ipv6_address(std::array<uint8_t, IP6_ADDR_LEN> address, TokenPtr addr_type) = 0;

  // Port ranges.
  virtual T ports(std::vector<PortRange> ranges, TokenPtr port_type) = 0;

  // Logical operations.
  // In general, if a filter operation uses another `T` filter as input, like the case here,
  // the input objects are passed by value and implementations of `FilterBuilder<T>` should
  // move them as necessary.
  virtual T negation(T filter) = 0;            // Logical `NOT`.
  virtual T conjunction(T left, T right) = 0;  // Logical `AND`.
  virtual T disjunction(T left, T right) = 0;  // Logical `OR`.

  FilterBuilder(const FilterBuilder&) = delete;
  FilterBuilder& operator=(const FilterBuilder&) = delete;

 protected:
  // In order to facilitate keyword lookup, construction must be with the same `Tokenizer` used to
  // lex the tokens passed into the filter operation methods.
  explicit FilterBuilder<T>(const Tokenizer& tokenizer) : tkz(tokenizer) {}

  const Tokenizer& tkz;
};

}  // namespace netdump::parser

#endif  // SRC_CONNECTIVITY_NETWORK_NETDUMP_FILTER_BUILDER_H_
