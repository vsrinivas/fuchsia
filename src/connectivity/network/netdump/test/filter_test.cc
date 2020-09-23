// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filter_test.h"

#include <algorithm>

namespace netdump::test {

// Canonical packet data.
static constexpr uint16_t frame_length = 2000;  // bytes
static constexpr EthFilter::MacAddress src_mac = {0xde, 0xad, 0xbe, 0xef, 0xd0, 0x0d};
static constexpr EthFilter::MacAddress dst_mac = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};
static uint16_t ip_pkt_length = htons(1842);  // bytes
static constexpr uint8_t protocol = 0xab;
static constexpr uint32_t ip4addr_src = 0xc0a80a04;
static constexpr uint32_t ip4addr_dst = 0xfffefdfc;
static constexpr IpFilter::IPv6Address ip6addr_src{0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0,    0,
                                                   0,    0,    0,    0,    0,    0,    0x88, 0x88};
static constexpr IpFilter::IPv6Address ip6addr_dst{0x32, 0x11, 0xAB, 0xCD, 0x12, 0xFF, 0,    0,
                                                   0,    0,    0,    0,    0,    0,    0x12, 0x68};
static uint16_t src_port = htons(6587);
static uint16_t dst_port = htons(1234);

// Test packet storage.
static struct ethhdr test_frame;
static struct iphdr test_ipv4;
static struct ip6_hdr test_ipv6;
static struct tcphdr test_tcp;
static struct udphdr test_udp;

// Setup functions.
static Packet SetupEth(uint16_t ethtype) {
  Packet packet;
  packet.frame_length = frame_length;
  test_frame.h_proto = ethtype;
  std::copy(src_mac.begin(), src_mac.end(), test_frame.h_source);
  std::copy(dst_mac.begin(), dst_mac.end(), test_frame.h_dest);
  packet.eth = &test_frame;
  return packet;
}

static void SetupIPv4(Packet* packet) {
  test_ipv4.version = 4;
  test_ipv4.tot_len = ip_pkt_length;
  test_ipv4.protocol = protocol;
  test_ipv4.saddr = ip4addr_src;
  test_ipv4.daddr = ip4addr_dst;
  packet->ip = &test_ipv4;
}

static void SetupIPv6(Packet* packet) {
  reinterpret_cast<struct iphdr*>(&test_ipv6)->version = 6;  // Version is set with iphdr pointer.
  test_ipv6.ip6_plen = ip_pkt_length;
  test_ipv6.ip6_nxt = protocol;
  std::copy(ip6addr_src.begin(), ip6addr_src.end(), test_ipv6.ip6_src.s6_addr);
  std::copy(ip6addr_dst.begin(), ip6addr_dst.end(), test_ipv6.ip6_dst.s6_addr);
  packet->ipv6 = &test_ipv6;
}

static void SetupTCP(Packet* packet) {
  test_tcp.source = src_port;
  test_tcp.dest = dst_port;
  test_ipv4.protocol = IPPROTO_TCP;
  test_ipv6.ip6_nxt = IPPROTO_TCP;
  packet->tcp = &test_tcp;
}

static void SetupUDP(Packet* packet) {
  test_udp.uh_sport = src_port;
  test_udp.uh_dport = dst_port;
  test_ipv4.protocol = IPPROTO_UDP;
  test_ipv6.ip6_nxt = IPPROTO_UDP;
  packet->tcp = &test_tcp;
}

// Implementation of the test battery.
void FrameLengthTest(FrameLengthFn filter_fn) {
  Packet packet = SetupEth(htons(ETH_P_IP));
  EXPECT_FALSE(filter_fn(htons(1842), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(htons(1842), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(htons(2000), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(htons(2000), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(htons(5555), LengthComparator::LEQ)->match(packet));
  EXPECT_FALSE(filter_fn(htons(5555), LengthComparator::GEQ)->match(packet));
}

void EthtypeTest(EthtypeFn filter_fn) {
  Packet null_packet = SetupEth(htons(0x1430));
  null_packet.eth = nullptr;
  EXPECT_FALSE(filter_fn(htons(0x1430))->match(null_packet));

  EXPECT_TRUE(filter_fn(htons(0x1430))->match(SetupEth(htons(0x1430))));
  EXPECT_FALSE(filter_fn(htons(0x3014))->match(SetupEth(htons(0x1430))));
  EXPECT_FALSE(filter_fn(htons(0xCDAB))->match(SetupEth(htons(0x1430))));
}

void MacTest(MacFn filter_fn) {
  Packet packet = SetupEth(htons(0x1430));
  FilterPtr matched_src = filter_fn(src_mac, SRC_ADDR);
  FilterPtr matched_dst = filter_fn(dst_mac, DST_ADDR);

  EthFilter::MacAddress unmatched_mac1{0x0d, 0xd0, 0xef, 0xbe, 0xad, 0xde};
  EthFilter::MacAddress unmatched_mac2{0xef, 0xdc, 0xab, 0xef, 0xdc, 0xab};
  FilterPtr unmatched_src1 = filter_fn(unmatched_mac1, SRC_ADDR);
  FilterPtr unmatched_src2 = filter_fn(unmatched_mac2, SRC_ADDR);
  FilterPtr unmatched_dst1 = filter_fn(unmatched_mac1, DST_ADDR);
  FilterPtr unmatched_dst2 = filter_fn(unmatched_mac2, DST_ADDR);

  EXPECT_TRUE(matched_src->match(packet));
  EXPECT_TRUE(matched_dst->match(packet));
  EXPECT_FALSE(unmatched_src1->match(packet));
  EXPECT_FALSE(unmatched_src2->match(packet));
  EXPECT_FALSE(unmatched_dst1->match(packet));
  EXPECT_FALSE(unmatched_dst2->match(packet));
}

void VersionTest(VersionFn filter_fn) {
  FilterPtr ip4filter_fn = filter_fn(4);
  FilterPtr ip6filter_fn = filter_fn(6);

  Packet packet = SetupEth(htons(ETH_P_IP));
  packet.ip = nullptr;
  EXPECT_FALSE(ip4filter_fn->match(packet));
  EXPECT_FALSE(ip6filter_fn->match(packet));

  packet = SetupEth(htons(ETH_P_IP));
  SetupIPv4(&packet);
  EXPECT_TRUE(ip4filter_fn->match(packet));
  EXPECT_FALSE(ip6filter_fn->match(packet));

  packet = SetupEth(htons(ETH_P_IPV6));
  SetupIPv6(&packet);
  EXPECT_FALSE(ip4filter_fn->match(packet));
  EXPECT_TRUE(ip6filter_fn->match(packet));
}

void IPLengthTest(IPLengthFn filter_fn) {
  Packet packet = SetupEth(htons(ETH_P_IP));
  SetupIPv4(&packet);
  EXPECT_FALSE(filter_fn(4, htons(40), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(4, htons(40), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(4, htons(1842), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(4, htons(1842), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(4, htons(4444), LengthComparator::LEQ)->match(packet));
  EXPECT_FALSE(filter_fn(4, htons(4444), LengthComparator::GEQ)->match(packet));

  packet = SetupEth(htons(ETH_P_IPV6));
  SetupIPv6(&packet);
  EXPECT_FALSE(filter_fn(6, htons(60), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(6, htons(60), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(6, htons(1842), LengthComparator::LEQ)->match(packet));
  EXPECT_TRUE(filter_fn(6, htons(1842), LengthComparator::GEQ)->match(packet));
  EXPECT_TRUE(filter_fn(6, htons(6666), LengthComparator::LEQ)->match(packet));
  EXPECT_FALSE(filter_fn(6, htons(6666), LengthComparator::GEQ)->match(packet));
}

void ProtocolTest(ProtocolFn filter_fn) {
  FilterPtr matched_ip4 = filter_fn(4, 0xab);
  FilterPtr matched_ip6 = filter_fn(6, 0xab);
  FilterPtr unmatched_ip4 = filter_fn(4, 0xcd);
  FilterPtr unmatched_ip6 = filter_fn(6, 0xef);

  Packet packet = SetupEth(htons(ETH_P_IP));
  SetupIPv4(&packet);
  EXPECT_TRUE(matched_ip4->match(packet));
  EXPECT_FALSE(unmatched_ip4->match(packet));

  packet = SetupEth(htons(ETH_P_IPV6));
  SetupIPv6(&packet);
  EXPECT_TRUE(matched_ip6->match(packet));
  EXPECT_FALSE(unmatched_ip6->match(packet));
}

void IPv4AddrTest(IPv4AddrFn filter_fn) {
  Packet packet = SetupEth(htons(ETH_P_IP));
  SetupIPv4(&packet);

  FilterPtr matched_src = filter_fn(0xc0a80a04, SRC_ADDR);
  FilterPtr matched_dst = filter_fn(0xfffefdfc, DST_ADDR);
  FilterPtr either_t = filter_fn(0xc0a80a04, EITHER_ADDR);
  FilterPtr either_f = filter_fn(0xffffffff, EITHER_ADDR);
  FilterPtr unmatched_src = filter_fn(0x040aa8c0, SRC_ADDR);
  FilterPtr unmatched_dst = filter_fn(0xfcfdfeff, DST_ADDR);

  EXPECT_TRUE(matched_src->match(packet));
  EXPECT_TRUE(matched_dst->match(packet));
  EXPECT_TRUE(either_t->match(packet));
  EXPECT_FALSE(either_f->match(packet));
  EXPECT_FALSE(unmatched_src->match(packet));
  EXPECT_FALSE(unmatched_dst->match(packet));
}

void IPv6AddrTest(IPv6AddrFn filter_fn) {
  IpFilter::IPv6Address ip6addr_other;
  ip6addr_other.fill(123);
  Packet packet = SetupEth(htons(ETH_P_IPV6));
  SetupIPv6(&packet);

  IpFilter::IPv6Address ip6addr_src_copy(ip6addr_src);  // Copy construction.
  IpFilter::IPv6Address ip6addr_dst_copy(ip6addr_dst);
  FilterPtr matched_src = filter_fn(ip6addr_src_copy, SRC_ADDR);
  FilterPtr matched_dst = filter_fn(ip6addr_dst_copy, DST_ADDR);
  FilterPtr wrong_type_src = filter_fn(ip6addr_src, DST_ADDR);
  FilterPtr wrong_type_dst = filter_fn(ip6addr_dst, SRC_ADDR);
  FilterPtr either_t = filter_fn(ip6addr_dst, EITHER_ADDR);
  FilterPtr either_f = filter_fn(ip6addr_other, EITHER_ADDR);
  FilterPtr unmatched_src = filter_fn(ip6addr_other, SRC_ADDR);
  FilterPtr unmatched_dst = filter_fn(ip6addr_other, DST_ADDR);

  EXPECT_TRUE(matched_src->match(packet));
  EXPECT_TRUE(matched_dst->match(packet));
  EXPECT_FALSE(wrong_type_src->match(packet));
  EXPECT_FALSE(wrong_type_dst->match(packet));
  EXPECT_TRUE(either_t->match(packet));
  EXPECT_FALSE(either_f->match(packet));
  EXPECT_FALSE(unmatched_src->match(packet));
  EXPECT_FALSE(unmatched_dst->match(packet));

  ip6addr_src_copy.fill(0);
  ip6addr_dst_copy.fill(0);
  // If IpFilter did not make a copy of the given IP6 address on construction then
  // the following will fail.
  EXPECT_TRUE(matched_src->match(packet));
  EXPECT_TRUE(matched_dst->match(packet));
}

static void PortsTest(uint8_t version, PortFn filter_fn) {
  Packet packet;
  switch (version) {
    case 4:
      packet = SetupEth(htons(ETH_P_IP));
      SetupIPv4(&packet);
      break;
    case 6:
      packet = SetupEth(htons(ETH_P_IPV6));
      SetupIPv6(&packet);
      break;
    default:
      ASSERT_TRUE(version == 4 || version == 6);  // IP version must be supported.
  }
  SetupTCP(&packet);

  auto src1 = filter_fn(std::vector<PortRange>{}, SRC_PORT);
  auto dst1 = filter_fn(std::vector<PortRange>{}, DST_PORT);
  auto either1 = filter_fn(std::vector<PortRange>{}, EITHER_PORT);

  EXPECT_FALSE(src1->match(packet));
  EXPECT_FALSE(dst1->match(packet));
  EXPECT_FALSE(either1->match(packet));

  auto src2 = filter_fn(std::vector<PortRange>{PortRange(htons(10000), htons(20000))}, SRC_PORT);
  auto dst2 = filter_fn(std::vector<PortRange>{PortRange(htons(1), htons(1000))}, DST_PORT);
  auto either2 =
      filter_fn(std::vector<PortRange>{PortRange(htons(8888), htons(8888))}, EITHER_PORT);

  EXPECT_FALSE(src2->match(packet));
  EXPECT_FALSE(dst2->match(packet));
  EXPECT_FALSE(either2->match(packet));

  auto src3 = filter_fn(std::vector<PortRange>{PortRange(htons(10000), htons(20000)),
                                               PortRange(htons(6587), htons(6587))},
                        SRC_PORT);
  auto dst3 = filter_fn(
      std::vector<PortRange>{PortRange(htons(1), htons(1000)), PortRange(htons(1234), htons(1234))},
      DST_PORT);
  auto either3 = filter_fn(std::vector<PortRange>{PortRange(htons(8888), htons(8888)),
                                                  PortRange(htons(1000), htons(2000))},
                           EITHER_PORT);
  EXPECT_TRUE(src3->match(packet));
  EXPECT_TRUE(dst3->match(packet));
  EXPECT_TRUE(either3->match(packet));

  SetupUDP(&packet);
  EXPECT_TRUE(src3->match(packet));
  EXPECT_TRUE(dst3->match(packet));
  EXPECT_TRUE(either3->match(packet));

  packet.transport = nullptr;
  EXPECT_FALSE(src3->match(packet));
  EXPECT_FALSE(dst3->match(packet));
  EXPECT_FALSE(either3->match(packet));
}

void IPv4PortsTest(PortFn filter_fn) { PortsTest(4, std::move(filter_fn)); }

void IPv6PortsTest(PortFn filter_fn) { PortsTest(6, std::move(filter_fn)); }

void UnsupportedIpVersionAssertTest(VersionFn version_fn, IPLengthFn length_fn,
                                    ProtocolFn protocol_fn) {
  if (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    ASSERT_DEATH([&version_fn]() { version_fn(3); });
    ASSERT_DEATH([&length_fn]() { length_fn(5, 16, LengthComparator::LEQ); });
    ASSERT_DEATH([&protocol_fn]() { protocol_fn(7, IPPROTO_TCP); });
  }
}

#define NETDUMP_TRUE FilterPtr(new EthFilter(htons(0x1430)))
#define NETDUMP_FALSE FilterPtr(new EthFilter(htons(0x3014)))
void CompositionTest(UnaryFn neg_fn, BinaryFn conj_fn, BinaryFn disj_fn) {
  Packet packet = SetupEth(htons(0x1430));

  auto neg_t = neg_fn(NETDUMP_TRUE);
  auto neg_f = neg_fn(NETDUMP_FALSE);
  auto conj_tt = conj_fn(NETDUMP_TRUE, NETDUMP_TRUE);
  auto conj_tf = conj_fn(NETDUMP_TRUE, NETDUMP_FALSE);
  auto conj_ft = conj_fn(NETDUMP_FALSE, NETDUMP_TRUE);
  auto conj_ff = conj_fn(NETDUMP_FALSE, NETDUMP_FALSE);
  auto disj_tt = disj_fn(NETDUMP_TRUE, NETDUMP_TRUE);
  auto disj_tf = disj_fn(NETDUMP_TRUE, NETDUMP_FALSE);
  auto disj_ft = disj_fn(NETDUMP_FALSE, NETDUMP_TRUE);
  auto disj_ff = disj_fn(NETDUMP_FALSE, NETDUMP_FALSE);

  EXPECT_TRUE(NETDUMP_TRUE->match(packet));
  EXPECT_FALSE(NETDUMP_FALSE->match(packet));
  EXPECT_FALSE(neg_t->match(packet));
  EXPECT_TRUE(neg_f->match(packet));
  EXPECT_TRUE(conj_tt->match(packet));
  EXPECT_FALSE(conj_tf->match(packet));
  EXPECT_FALSE(conj_ft->match(packet));
  EXPECT_FALSE(conj_ff->match(packet));
  EXPECT_TRUE(disj_tt->match(packet));
  EXPECT_TRUE(disj_tf->match(packet));
  EXPECT_TRUE(disj_ft->match(packet));
  EXPECT_FALSE(disj_ff->match(packet));
}
#undef NETDUMP_TRUE
#undef NETDUMP_FALSE

// Instantiation of the tests for filter tree node constructors.
// Using templates, the right constructors are picked automatically.
// Any callable class is automatically convertible to `std::function` as long as a matching overload
// of `operator()` is present.
template <class Flt>
class CallConstructor;

template <>
class CallConstructor<IpFilter> {
 public:
  // IpFilter constructors need some help with integer conversions, so forward them explicitly.
  inline FilterPtr operator()(uint8_t version) { return FilterPtr(new IpFilter(version)); }
  inline FilterPtr operator()(uint8_t version, uint8_t protocol) {
    return FilterPtr(new IpFilter(version, protocol));
  }
  inline FilterPtr operator()(uint8_t version, uint16_t length, LengthComparator comparator) {
    return FilterPtr(new IpFilter(version, length, comparator));
  }
  inline FilterPtr operator()(uint32_t ipv4_addr, AddressFieldType type) {
    return FilterPtr(new IpFilter(ipv4_addr, type));
  }
  inline FilterPtr operator()(const IpFilter::IPv6Address& ipv6_addr, AddressFieldType type) {
    return FilterPtr(new IpFilter(ipv6_addr, type));
  }
};

// This template handles all the rest.
template <class Flt>
class CallConstructor {
 public:
  template <typename... Args>
  inline FilterPtr operator()(Args&&... args) {
    return FilterPtr(new Flt(std::forward<Args>(args)...));
  }
};

#define NETDUMP_TEST(test, flt) \
  TEST(NetdumpFilterTest, test) { test(CallConstructor<flt>()); }

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

TEST(NetdumpFilterTest, UnsupportedIpVersionAssertTest) {
  // NB: We don't use CallConstructor here because it causes a leak when the assertion trips in the
  // constructor, since the calling thread dies after the memory is allocated and unique_ptr's
  // destructor doesn't get a chance to run.
  UnsupportedIpVersionAssertTest(
      [](uint8_t version) {
        IpFilter filter(version);
        return nullptr;
      },
      [](uint8_t version, uint16_t length, LengthComparator comparator) {
        IpFilter filter(version, length, comparator);
        return nullptr;
      },
      [](uint8_t version, uint8_t protocol) {
        IpFilter filter(version, protocol);
        return nullptr;
      });
}

TEST(NetdumpFilterTest, CompositionTest) {
  CompositionTest(CallConstructor<NegFilter>(), CallConstructor<ConjFilter>(),
                  CallConstructor<DisjFilter>());
}

// Tests for the `populate` method in the `Packet` class that finds the pointers to the headers.
static constexpr uint16_t BUFFER_LENGTH = 256;

inline struct ethhdr* SetupPopulateBuffer(uint16_t ethtype, uint8_t* buffer) {
  auto frame = reinterpret_cast<struct ethhdr*>(buffer);
  frame->h_proto = ethtype;
  return frame;
}

TEST(NetdumpFilterTest, PopulatePacketEthernetTest) {
  uint8_t buffer[BUFFER_LENGTH];
  Packet packet;
  auto eth_hdr = SetupPopulateBuffer(0, buffer);

  // Unrecognized ethtype.
  packet.populate(buffer, BUFFER_LENGTH);
  EXPECT_EQ(BUFFER_LENGTH, packet.frame_length);
  EXPECT_BYTES_EQ(eth_hdr, packet.eth, sizeof(struct ethhdr));
  EXPECT_NULL(packet.ip);
  EXPECT_NULL(packet.transport);

  // Incomplete Ethernet headers.
  SetupPopulateBuffer(ntohs(ETH_P_IP), buffer);
  packet.populate(buffer, ETH_HLEN - 1);
  EXPECT_EQ(ETH_HLEN - 1, packet.frame_length);
  EXPECT_NULL(packet.eth);
  EXPECT_NULL(packet.ip);
  EXPECT_NULL(packet.transport);

  // Incomplete L3 headers.
  packet.populate(buffer, ETH_HLEN + 1);
  EXPECT_EQ(ETH_HLEN + 1, packet.frame_length);
  EXPECT_BYTES_EQ(eth_hdr, packet.eth, sizeof(struct ethhdr));
  EXPECT_NULL(packet.ip);
  EXPECT_NULL(packet.transport);
}

void PopulatePacketIPTest(size_t iphdr_len, uint8_t* transport_protocol, uint8_t* buffer) {
  Packet packet;
  auto eth = reinterpret_cast<struct ethhdr*>(buffer);
  auto ip = reinterpret_cast<struct iphdr*>(buffer + ETH_HLEN);
  void* transport = buffer + ETH_HLEN + iphdr_len;

  // Unrecognized transport protocol.
  *transport_protocol = 0;
  packet.populate(buffer, BUFFER_LENGTH);
  EXPECT_EQ(BUFFER_LENGTH, packet.frame_length);
  EXPECT_BYTES_EQ(eth, packet.eth, sizeof(struct ethhdr));
  EXPECT_BYTES_EQ(ip, packet.ip, sizeof(struct iphdr));
  EXPECT_NULL(packet.transport);

  // UDP headers.
  *transport_protocol = IPPROTO_UDP;
  packet.populate(buffer, static_cast<uint16_t>(ETH_HLEN + iphdr_len + sizeof(struct udphdr)));
  EXPECT_EQ(ETH_HLEN + iphdr_len + sizeof(struct udphdr), packet.frame_length);
  EXPECT_BYTES_EQ(eth, packet.eth, sizeof(struct ethhdr));
  EXPECT_BYTES_EQ(ip, packet.ip, sizeof(struct iphdr));
  EXPECT_BYTES_EQ(transport, packet.udp, sizeof(struct udphdr));

  // Incomplete UDP headers.
  packet.populate(buffer, static_cast<uint16_t>(ETH_HLEN + iphdr_len + 1));
  EXPECT_EQ(ETH_HLEN + iphdr_len + 1, packet.frame_length);
  EXPECT_BYTES_EQ(eth, packet.eth, sizeof(struct ethhdr));
  EXPECT_BYTES_EQ(ip, packet.ip, sizeof(struct iphdr));
  EXPECT_NULL(packet.transport);

  // TCP headers.
  *transport_protocol = IPPROTO_TCP;
  packet.populate(buffer, BUFFER_LENGTH);
  EXPECT_EQ(BUFFER_LENGTH, packet.frame_length);
  EXPECT_BYTES_EQ(eth, packet.eth, sizeof(struct ethhdr));
  EXPECT_BYTES_EQ(ip, packet.ip, sizeof(struct iphdr));
  EXPECT_BYTES_EQ(transport, packet.tcp, sizeof(struct tcphdr));

  // Incomplete TCP headers, length sufficient for UDP but not TCP.
  packet.populate(buffer, static_cast<uint16_t>(ETH_HLEN + iphdr_len + sizeof(struct udphdr)));
  EXPECT_EQ(ETH_HLEN + iphdr_len + sizeof(struct udphdr), packet.frame_length);
  EXPECT_BYTES_EQ(eth, packet.eth, sizeof(struct ethhdr));
  EXPECT_BYTES_EQ(ip, packet.ip, sizeof(struct iphdr));
  EXPECT_NULL(packet.transport);
}

TEST(NetdumpFilterTest, PopulatePacketIPv4Test) {
  uint8_t buffer[BUFFER_LENGTH];
  SetupPopulateBuffer(ntohs(ETH_P_IP), buffer);
  auto ip = reinterpret_cast<struct iphdr*>(buffer + ETH_HLEN);
  // Make a copy of the IP header to prevent unaligned access in the Ethernet buffer.
  struct iphdr copy = *ip;
  // Set the IP header length, in units of 32 bit words.
  copy.ihl = sizeof(struct iphdr) / sizeof(uint32_t);
  *ip = copy;
  PopulatePacketIPTest(sizeof(struct iphdr), &buffer[ETH_HLEN + offsetof(struct iphdr, protocol)],
                       buffer);
}

TEST(NetdumpFilterTest, PopulatePacketIPv6Test) {
  uint8_t buffer[BUFFER_LENGTH];
  SetupPopulateBuffer(ntohs(ETH_P_IPV6), buffer);
  PopulatePacketIPTest(sizeof(struct ip6_hdr),
                       &buffer[ETH_HLEN + offsetof(struct ip6_hdr, ip6_nxt)], buffer);
}

}  // namespace netdump::test
