// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filter.h"

#include <algorithm>
#include <cstring>

#include <zxtest/zxtest.h>

namespace netdump::test {

// Canonical packet data.
static uint16_t frame_length = 2000; // bytes
static uint8_t src_mac[] = {0xde, 0xad, 0xbe, 0xef, 0xd0, 0x0d};
static uint8_t dst_mac[] = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};
static uint16_t ip_pkt_length = htons(1842); //bytes
static uint8_t protocol = 0xab;
static uint32_t ip4addr_src = 0xc0a80a04;
static uint32_t ip4addr_dst = 0xfffefdfc;
static uint8_t ip6addr_src[IP6_ADDR_LEN] = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0x88, 0x88};
static uint8_t ip6addr_dst[IP6_ADDR_LEN] = {0x32, 0x11, 0xAB, 0xCD, 0x12, 0xFF, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0x12, 0x68};
static uint16_t src_port = htons(6587);
static uint16_t dst_port = htons(1234);

// Test packet storage.
static struct ethhdr test_frame;
struct iphdr test_ipv4;
ip6_hdr_t test_ipv6;
static struct tcphdr test_tcp;
static struct udphdr test_udp;

static Packet SetupEth(uint16_t ethtype) {
    Packet packet;
    packet.frame_length = frame_length;
    test_frame.h_proto = ethtype;
    std::copy(src_mac, src_mac + ETH_ALEN, test_frame.h_source);
    std::copy(dst_mac, dst_mac + ETH_ALEN, test_frame.h_dest);
    packet.frame = &test_frame;
    return packet;
}

static void SetupIPv4(Packet* packet) {
    test_ipv4.version = 4;
    test_ipv4.tot_len = ip_pkt_length;
    test_ipv4.protocol = protocol;
    test_ipv4.saddr = ip4addr_src;
    test_ipv4.daddr = ip4addr_dst;
    packet->ipv4 = &test_ipv4;
}

static void SetupIPv6(Packet* packet) {
    reinterpret_cast<struct iphdr*>(&test_ipv6)->version = 6; // Version is set through ip4 pointer.
    test_ipv6.length = ip_pkt_length;
    test_ipv6.next_header = protocol;
    std::copy(ip6addr_src, ip6addr_src + IP6_ADDR_LEN, test_ipv6.src.u8);
    std::copy(ip6addr_dst, ip6addr_dst + IP6_ADDR_LEN, test_ipv6.dst.u8);
    packet->ipv6 = &test_ipv6;
}

static void SetupTCP(Packet* packet) {
    test_tcp.source = src_port;
    test_tcp.dest = dst_port;
    test_ipv4.protocol = IPPROTO_TCP;
    test_ipv6.next_header = IPPROTO_TCP;
    packet->tcp = &test_tcp;
}

static void SetupUDP(Packet* packet) {
    test_udp.source = src_port;
    test_udp.dest = dst_port;
    test_ipv4.protocol = IPPROTO_UDP;
    test_ipv6.next_header = IPPROTO_UDP;
    packet->tcp = &test_tcp;
}

TEST(NetdumpFilterTests, FrameLengthTest) {
    Packet packet = SetupEth(htons(ETH_P_IP));
    EXPECT_FALSE(FrameLengthFilter(htons(1842), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(FrameLengthFilter(htons(1842), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(FrameLengthFilter(htons(2000), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(FrameLengthFilter(htons(2000), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(FrameLengthFilter(htons(5555), LengthComparator::LEQ).match(packet));
    EXPECT_FALSE(FrameLengthFilter(htons(5555), LengthComparator::GEQ).match(packet));
}

TEST(NetdumpFilterTests, EthtypeTest) {
    Packet null_packet = SetupEth(htons(0x1430));
    null_packet.frame = nullptr;
    EXPECT_FALSE(EthFilter(htons(0x1430)).match(null_packet));

    EXPECT_TRUE(EthFilter(htons(0x1430)).match(SetupEth(htons(0x1430))));
    EXPECT_FALSE(EthFilter(htons(0x3014)).match(SetupEth(htons(0x1430))));
    EXPECT_FALSE(EthFilter(htons(0xCDAB)).match(SetupEth(htons(0x1430))));
}

TEST(NetdumpFilterTests, MacTest) {
    Packet packet = SetupEth(htons(0x1430));
    EthFilter matched_src = EthFilter(src_mac, SRC_ADDR);
    EthFilter matched_dst = EthFilter(dst_mac, DST_ADDR);

    uint8_t unmatched_mac1[] = {0x0d, 0xd0, 0xef, 0xbe, 0xad, 0xde};
    uint8_t unmatched_mac2[] = {0xef, 0xdc, 0xab, 0xef, 0xdc, 0xab};
    EthFilter unmatched_src1 = EthFilter(unmatched_mac1, SRC_ADDR);
    EthFilter unmatched_src2 = EthFilter(unmatched_mac2, SRC_ADDR);
    EthFilter unmatched_dst1 = EthFilter(unmatched_mac1, DST_ADDR);
    EthFilter unmatched_dst2 = EthFilter(unmatched_mac2, DST_ADDR);

    EXPECT_TRUE(matched_src.match(packet));
    EXPECT_TRUE(matched_dst.match(packet));
    EXPECT_FALSE(unmatched_src1.match(packet));
    EXPECT_FALSE(unmatched_src2.match(packet));
    EXPECT_FALSE(unmatched_dst1.match(packet));
    EXPECT_FALSE(unmatched_dst2.match(packet));
}

TEST(NetdumpFilterTests, VersionTest) {
    IpFilter ip4filter = IpFilter(4);
    IpFilter ip6filter = IpFilter(6);

    Packet packet = SetupEth(htons(ETH_P_IP));
    packet.ipv4 = nullptr;
    EXPECT_FALSE(ip4filter.match(packet));
    EXPECT_FALSE(ip6filter.match(packet));

    packet = SetupEth(htons(ETH_P_IP));
    SetupIPv4(&packet);
    EXPECT_TRUE(ip4filter.match(packet));
    EXPECT_FALSE(ip6filter.match(packet));

    packet = SetupEth(htons(ETH_P_IPV6));
    SetupIPv6(&packet);
    EXPECT_FALSE(ip4filter.match(packet));
    EXPECT_TRUE(ip6filter.match(packet));
}

TEST(NetdumpFilterTests, LengthTest) {
    Packet packet = SetupEth(htons(ETH_P_IP));
    SetupIPv4(&packet);
    EXPECT_FALSE(IpFilter(4, htons(40), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(IpFilter(4, htons(40), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(IpFilter(4, htons(1842), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(IpFilter(4, htons(1842), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(IpFilter(4, htons(4444), LengthComparator::LEQ).match(packet));
    EXPECT_FALSE(IpFilter(4, htons(4444), LengthComparator::GEQ).match(packet));

    packet = SetupEth(htons(ETH_P_IPV6));
    SetupIPv6(&packet);
    EXPECT_FALSE(IpFilter(6, htons(60), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(IpFilter(6, htons(60), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(IpFilter(6, htons(1842), LengthComparator::LEQ).match(packet));
    EXPECT_TRUE(IpFilter(6, htons(1842), LengthComparator::GEQ).match(packet));
    EXPECT_TRUE(IpFilter(6, htons(6666), LengthComparator::LEQ).match(packet));
    EXPECT_FALSE(IpFilter(6, htons(6666), LengthComparator::GEQ).match(packet));
}

TEST(NetdumpFilterTests, ProtocolTest) {
    IpFilter matched_ip4 = IpFilter(4, 0xab);
    IpFilter matched_ip6 = IpFilter(6, 0xab);
    IpFilter unmatched_ip4 = IpFilter(4, 0xcd);
    IpFilter unmatched_ip6 = IpFilter(6, 0xef);

    Packet packet = SetupEth(htons(ETH_P_IP));
    SetupIPv4(&packet);
    EXPECT_TRUE(matched_ip4.match(packet));
    EXPECT_FALSE(unmatched_ip4.match(packet));

    packet = SetupEth(htons(ETH_P_IPV6));
    SetupIPv6(&packet);
    EXPECT_TRUE(matched_ip6.match(packet));
    EXPECT_FALSE(unmatched_ip6.match(packet));
}

TEST(NetdumpFilterTests, Ipv4AddrTest) {
    Packet packet = SetupEth(htons(ETH_P_IP));
    SetupIPv4(&packet);

    IpFilter matched_src = IpFilter(0xc0a80a04, SRC_ADDR);
    IpFilter matched_dst = IpFilter(0xfffefdfc, DST_ADDR);
    IpFilter either_t = IpFilter(0xc0a80a04, EITHER_ADDR);
    IpFilter either_f = IpFilter(0xffffffff, EITHER_ADDR);
    IpFilter unmatched_src = IpFilter(0x040aa8c0, SRC_ADDR);
    IpFilter unmatched_dst = IpFilter(0xfcfdfeff, DST_ADDR);

    EXPECT_TRUE(matched_src.match(packet));
    EXPECT_TRUE(matched_dst.match(packet));
    EXPECT_TRUE(either_t.match(packet));
    EXPECT_FALSE(either_f.match(packet));
    EXPECT_FALSE(unmatched_src.match(packet));
    EXPECT_FALSE(unmatched_dst.match(packet));
}

TEST(NetdumpFilterTests, Ipv6AddrTest) {
    uint8_t ip6addr_other[IP6_ADDR_LEN];
    memset(&ip6addr_other, 123, IP6_ADDR_LEN);
    Packet packet = SetupEth(htons(ETH_P_IPV6));
    SetupIPv6(&packet);

    IpFilter matched_src = IpFilter(ip6addr_src, SRC_ADDR);
    IpFilter matched_dst = IpFilter(ip6addr_dst, DST_ADDR);
    IpFilter wrong_type_src = IpFilter(ip6addr_src, DST_ADDR);
    IpFilter wrong_type_dst = IpFilter(ip6addr_dst, SRC_ADDR);
    IpFilter either_t = IpFilter(ip6addr_dst, EITHER_ADDR);
    IpFilter either_f = IpFilter(ip6addr_other, EITHER_ADDR);
    IpFilter unmatched_src = IpFilter(ip6addr_other, SRC_ADDR);
    IpFilter unmatched_dst = IpFilter(ip6addr_other, DST_ADDR);

    EXPECT_TRUE(matched_src.match(packet));
    EXPECT_TRUE(matched_dst.match(packet));
    EXPECT_FALSE(wrong_type_src.match(packet));
    EXPECT_FALSE(wrong_type_dst.match(packet));
    EXPECT_TRUE(either_t.match(packet));
    EXPECT_FALSE(either_f.match(packet));
    EXPECT_FALSE(unmatched_src.match(packet));
    EXPECT_FALSE(unmatched_dst.match(packet));

    memset(&ip6addr_src, 0, IP6_ADDR_LEN);
    memset(&ip6addr_dst, 0, IP6_ADDR_LEN);
    // If IpFilter did not make a copy of the given IP6 address on construction then
    // the following will fail.
    EXPECT_TRUE(matched_src.match(packet));
    EXPECT_TRUE(matched_dst.match(packet));
}

void PortsTest(uint8_t version) {
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
        ASSERT_TRUE(version == 4 || version == 6); // IP version must be supported.
    }
    SetupTCP(&packet);

    using PortRange = PortFilter::PortRange;
    PortFilter src1(std::vector<PortRange>{}, SRC_PORT);
    PortFilter dst1(std::vector<PortRange>{}, DST_PORT);
    PortFilter either1(std::vector<PortRange>{}, EITHER_PORT);

    EXPECT_FALSE(src1.match(packet));
    EXPECT_FALSE(dst1.match(packet));
    EXPECT_FALSE(either1.match(packet));

    PortFilter src2(std::vector<PortRange>{PortRange(htons(10000), htons(20000))}, SRC_PORT);
    PortFilter dst2(std::vector<PortRange>{PortRange(htons(1), htons(1000))}, DST_PORT);
    PortFilter either2(std::vector<PortRange>{PortRange(htons(8888), htons(8888))}, EITHER_PORT);

    EXPECT_FALSE(src2.match(packet));
    EXPECT_FALSE(dst2.match(packet));
    EXPECT_FALSE(either2.match(packet));

    PortFilter src3(
        std::vector<PortRange>{PortRange(htons(10000), htons(20000)),
                               PortRange(htons(6587), htons(6587))},
        SRC_PORT);
    PortFilter dst3(
        std::vector<PortRange>{PortRange(htons(1), htons(1000)),
                               PortRange(htons(1234), htons(1234))},
        DST_PORT);
    PortFilter either3(
        std::vector<PortRange>{PortRange(htons(8888), htons(8888)),
                               PortRange(htons(1000), htons(2000))},
        EITHER_PORT);
    EXPECT_TRUE(src3.match(packet));
    EXPECT_TRUE(dst3.match(packet));
    EXPECT_TRUE(either3.match(packet));

    SetupUDP(&packet);
    EXPECT_TRUE(src3.match(packet));
    EXPECT_TRUE(dst3.match(packet));
    EXPECT_TRUE(either3.match(packet));

    packet.transport = nullptr;
    EXPECT_FALSE(src3.match(packet));
    EXPECT_FALSE(dst3.match(packet));
    EXPECT_FALSE(either3.match(packet));
}

TEST(NetdumpFilterTests, Ipv4PortsTest) {
    ASSERT_NO_FAILURES(PortsTest(4));
}

TEST(NetdumpFilterTests, Ipv6PortsTest) {
    ASSERT_NO_FAILURES(PortsTest(6));
}

TEST(NetdumpFilterTests, UnsupportedIpVersionAssertTest) {
    ASSERT_DEATH([]() { IpFilter(3); });
    ASSERT_DEATH([]() { IpFilter(5, 16, LengthComparator::LEQ); });
    ASSERT_DEATH([]() { IpFilter(7, IPPROTO_TCP); });
}

#define NETDUMP_TRUE FilterPtr(new EthFilter(htons(0x1430)))
#define NETDUMP_FALSE FilterPtr(new EthFilter(htons(0x3014)))
TEST(NetdumpFilterTests, CompositionTest) {
    Packet packet = SetupEth(htons(0x1430));

    NegFilter not_t = NegFilter(NETDUMP_TRUE);
    NegFilter not_f = NegFilter(NETDUMP_FALSE);
    ConjFilter conj_tt = ConjFilter(NETDUMP_TRUE, NETDUMP_TRUE);
    ConjFilter conj_tf = ConjFilter(NETDUMP_TRUE, NETDUMP_FALSE);
    ConjFilter conj_ft = ConjFilter(NETDUMP_FALSE, NETDUMP_TRUE);
    ConjFilter conj_ff = ConjFilter(NETDUMP_FALSE, NETDUMP_FALSE);
    DisjFilter disj_tt = DisjFilter(NETDUMP_TRUE, NETDUMP_TRUE);
    DisjFilter disj_tf = DisjFilter(NETDUMP_TRUE, NETDUMP_FALSE);
    DisjFilter disj_ft = DisjFilter(NETDUMP_FALSE, NETDUMP_TRUE);
    DisjFilter disj_ff = DisjFilter(NETDUMP_FALSE, NETDUMP_FALSE);

    EXPECT_TRUE(NETDUMP_TRUE->match(packet));
    EXPECT_FALSE(NETDUMP_FALSE->match(packet));
    EXPECT_FALSE(not_t.match(packet));
    EXPECT_TRUE(not_f.match(packet));
    EXPECT_TRUE(conj_tt.match(packet));
    EXPECT_FALSE(conj_tf.match(packet));
    EXPECT_FALSE(conj_ft.match(packet));
    EXPECT_FALSE(conj_ff.match(packet));
    EXPECT_TRUE(disj_tt.match(packet));
    EXPECT_TRUE(disj_tf.match(packet));
    EXPECT_TRUE(disj_ft.match(packet));
    EXPECT_FALSE(disj_ff.match(packet));
}
#undef NETDUMP_TRUE
#undef NETDUMP_FALSE

} // namespace netdump::test
