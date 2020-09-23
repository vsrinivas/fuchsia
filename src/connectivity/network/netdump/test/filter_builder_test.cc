// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Instantiations of filter tests with filter tree builder.

#include <type_traits>
#include <unordered_map>

#include <zxtest/zxtest.h>

#include "filter_builder_impl.h"
#include "filter_test.h"

namespace netdump::test {

static Tokenizer tkz{};
static parser::FilterTreeBuilder bld{tkz};

template <typename T>
class TokenMap;

template <>
class TokenMap<LengthComparator> {
 public:
  const std::unordered_map<LengthComparator, TokenPtr> map{{LEQ, tkz.LESS}, {GEQ, tkz.GREATER}};
};

template <>
class TokenMap<AddressFieldType> {
 public:
  const std::unordered_map<AddressFieldType, TokenPtr> map{
      {SRC_ADDR, tkz.SRC}, {DST_ADDR, tkz.DST}, {EITHER_ADDR, tkz.HOST}};
};

template <>
class TokenMap<PortFieldType> {
 public:
  const std::unordered_map<PortFieldType, TokenPtr> map{
      {SRC_PORT, tkz.SRC}, {DST_PORT, tkz.DST}, {EITHER_PORT, tkz.PORT}};
};

template <typename T>
static inline TokenPtr lookup_token(T key) {
  TokenMap<T> tm{};
  auto token = tm.map.find(key);
  ZX_DEBUG_ASSERT(token != tm.map.end());
  return token->second;
}

template <class Flt>
class CallFilterBuilder;

template <>
class CallFilterBuilder<FrameLengthFilter> {
 public:
  inline FilterPtr operator()(uint16_t length, LengthComparator comparator) {
    return bld.frame_length(htons(length), lookup_token(comparator));
  }
};

template <>
class CallFilterBuilder<EthFilter> {
 public:
  inline FilterPtr operator()(uint16_t ethtype) { return bld.ethertype(htons(ethtype)); }
  inline FilterPtr operator()(EthFilter::MacAddress mac, AddressFieldType type) {
    std::reverse(mac.begin(), mac.end());
    return bld.mac(mac, lookup_token(type));
  }
};

template <>
class CallFilterBuilder<IpFilter> {
 public:
  inline FilterPtr operator()(uint8_t version) { return bld.ip_version(version); }
  inline FilterPtr operator()(uint8_t version, uint8_t protocol) {
    return bld.ip_protocol(version, protocol);
  }
  inline FilterPtr operator()(uint8_t version, uint16_t length, LengthComparator comparator) {
    return bld.ip_pkt_length(version, htons(length), lookup_token(comparator));
  }
  inline FilterPtr operator()(uint32_t ipv4_addr, AddressFieldType type) {
    return bld.ipv4_address(htonl(ipv4_addr), lookup_token(type));
  }
  inline FilterPtr operator()(IpFilter::IPv6Address ipv6_addr, AddressFieldType type) {
    std::reverse(ipv6_addr.begin(), ipv6_addr.end());
    return bld.ipv6_address(ipv6_addr, lookup_token(type));
  }
};

template <>
class CallFilterBuilder<PortFilter> {
 public:
  inline FilterPtr operator()(std::vector<PortRange> ranges, PortFieldType type) {
    for (auto it = ranges.begin(); it < ranges.end(); ++it) {
      it->first = htons(it->first);
      it->second = htons(it->second);
    }
    return bld.ports(std::move(ranges), lookup_token(type));
  }
};

template <>
class CallFilterBuilder<NegFilter> {
 public:
  inline FilterPtr operator()(FilterPtr filter) { return bld.negation(std::move(filter)); }
};

template <>
class CallFilterBuilder<ConjFilter> {
 public:
  inline FilterPtr operator()(FilterPtr left, FilterPtr right) {
    return bld.conjunction(std::move(left), std::move(right));
  }
};

template <>
class CallFilterBuilder<DisjFilter> {
 public:
  inline FilterPtr operator()(FilterPtr left, FilterPtr right) {
    return bld.disjunction(std::move(left), std::move(right));
  }
};

#define NETDUMP_TEST(test, flt) \
  TEST(NetdumpFilterBuilderTest, test) { test(CallFilterBuilder<flt>()); }

NETDUMP_TEST(FrameLengthTest, FrameLengthFilter)
NETDUMP_TEST(EthtypeTest, EthFilter)
NETDUMP_TEST(MacTest, EthFilter)
NETDUMP_TEST(VersionTest, IpFilter)
NETDUMP_TEST(IPLengthTest, IpFilter)
NETDUMP_TEST(ProtocolTest, IpFilter)
NETDUMP_TEST(IPv4AddrTest, IpFilter)
NETDUMP_TEST(IPv6AddrTest, IpFilter)
NETDUMP_TEST(IPv4PortsTest, PortFilter)
NETDUMP_TEST(IPv6PortsTest, PortFilter)

#undef NETDUMP_TEST

TEST(NetdumpFilterBuilderTest, CompositionTest) {
  CompositionTest(CallFilterBuilder<NegFilter>(), CallFilterBuilder<ConjFilter>(),
                  CallFilterBuilder<DisjFilter>());
}

}  // namespace netdump::test
