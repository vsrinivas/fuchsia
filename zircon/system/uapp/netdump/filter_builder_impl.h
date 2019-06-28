// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Concrete implementations of `FilterBuilder` required by the parser.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_BUILDER_IMPL_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_BUILDER_IMPL_H_

#include "filter.h"
#include "parser.h"

namespace netdump::parser {

// The reference implementation for constructing filter tree nodes.
// `FilterTreeBuilder` takes parameters in host byte order, and converts them in network byte order
// for the filter node constructors.
class FilterTreeBuilder : public FilterBuilder<FilterPtr> {
 public:
  explicit FilterTreeBuilder(const Tokenizer& tokenizer) : FilterBuilder<FilterPtr>(tokenizer) {}

  inline FilterPtr frame_length(uint16_t length, TokenPtr comparator) override {
    return FilterPtr(new FrameLengthFilter(htons(length), comparator->get_tag<LengthComparator>()));
  }

  inline FilterPtr ethertype(uint16_t type) override {
    return FilterPtr(new EthFilter(htons(type)));
  }

  inline FilterPtr mac(std::array<uint8_t, ETH_ALEN> address, TokenPtr addr_type) override {
    std::reverse(address.begin(), address.end());  // Constructor expects network byte order.
    return FilterPtr(new EthFilter(address, addr_type->get_tag<AddressFieldType>()));
  }

  inline FilterPtr ip_version(uint8_t version) override { return FilterPtr(new IpFilter(version)); }

  inline FilterPtr ip_pkt_length(uint8_t version, uint16_t length, TokenPtr comparator) override {
    return FilterPtr(new IpFilter(version, htons(length), comparator->get_tag<LengthComparator>()));
  }

  inline FilterPtr ip_protocol(uint8_t version, uint8_t protocol) override {
    return FilterPtr(new IpFilter(version, protocol));
  }

  inline FilterPtr ipv4_address(uint32_t address, TokenPtr type) override {
    return FilterPtr(new IpFilter(htonl(address), type->get_tag<AddressFieldType>()));
  }

  inline FilterPtr ipv6_address(std::array<uint8_t, IP6_ADDR_LEN> address,
                                TokenPtr addr_type) override {
    std::reverse(address.begin(), address.end());  // Constructor expects network byte order.
    return FilterPtr(new IpFilter(address, addr_type->get_tag<AddressFieldType>()));
  }

  FilterPtr ports(std::vector<PortRange> ranges, TokenPtr port_type) override {
    for (auto range_it = ranges.begin(); range_it < ranges.end(); ++range_it) {
      range_it->first = htons(range_it->first);
      range_it->second = htons(range_it->second);
    }
    if (port_type == tkz.SRC) {
      return FilterPtr(new PortFilter(std::move(ranges), SRC_PORT));
    }
    if (port_type == tkz.DST) {
      return FilterPtr(new PortFilter(std::move(ranges), DST_PORT));
    }
    if (port_type == tkz.PORT) {
      return FilterPtr(new PortFilter(std::move(ranges), EITHER_PORT));
    }
    ZX_DEBUG_ASSERT_MSG(port_type->one_of(tkz.SRC, tkz.DST, tkz.PORT),
                        "Invalid port type token: %s", port_type->get_term().c_str());
    return nullptr;
  }

  inline FilterPtr negation(FilterPtr filter) override {
    return FilterPtr(new NegFilter(std::move(filter)));
  }

  inline FilterPtr conjunction(FilterPtr left, FilterPtr right) override {
    return FilterPtr(new ConjFilter(std::move(left), std::move(right)));
  }

  inline FilterPtr disjunction(FilterPtr left, FilterPtr right) override {
    return FilterPtr(new DisjFilter(std::move(left), std::move(right)));
  }
};

}  // namespace netdump::parser

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_FILTER_BUILDER_IMPL_H_
