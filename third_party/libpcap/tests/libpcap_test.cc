// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <pcap.h>
#include <zircon/compiler.h>

#include <array>
#include <thread>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <netpacket/packet.h>
#include <pcap/sll.h>

namespace {

using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Values;

constexpr char kLoopbackDeviceName[] = "lo";
constexpr char kAnyDeviceName[] = "any";

void LoadSockaddr(const fbl::unique_fd &fd, sockaddr_in &addr) {
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<struct sockaddr *>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
}

void BindToLoopback(const fbl::unique_fd &fd) {
  sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  ASSERT_EQ(bind(fd.get(), reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)), 0)
      << strerror(errno);
}

TEST(LibpcapFindAllDevicesTest, FindAllDevices) {
  pcap_if_t *devlist;
  char ebuf[PCAP_ERRBUF_SIZE];

  ASSERT_GE(pcap_findalldevs(&devlist, ebuf), 0) << std::string(ebuf);
  auto d = fit::defer([&] {
    pcap_freealldevs(devlist);
    devlist = nullptr;
  });

  bool has_loopback = false, has_any = false;
  for (pcap_if_t *dev = devlist; dev != nullptr; dev = dev->next) {
    if (strcmp(dev->name, kLoopbackDeviceName) == 0) {
      constexpr bpf_u_int32 kFlagsMask = PCAP_IF_UP | PCAP_IF_RUNNING | PCAP_IF_LOOPBACK |
                                         PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
      EXPECT_EQ(dev->flags & kFlagsMask, kFlagsMask);
      EXPECT_EQ(dev->flags & ~kFlagsMask, 0u);

      ASSERT_FALSE(has_loopback);
      has_loopback = true;
    } else if (strcmp(dev->name, kAnyDeviceName) == 0) {
      constexpr bpf_u_int32 kFlagsMask =
          PCAP_IF_UP | PCAP_IF_RUNNING | PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
      EXPECT_EQ(dev->flags & kFlagsMask, kFlagsMask);
      EXPECT_EQ(dev->flags & ~kFlagsMask, 0u);

      ASSERT_FALSE(has_any);
      has_any = true;
    } else {
      EXPECT_FALSE(true) << "got unexpected device with name = " << dev->name;
    }
  }

  ASSERT_TRUE(has_loopback);
  ASSERT_TRUE(has_any);
}

class LibpcapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_THAT(p_ = pcap_create(nullptr, ebuf()), NotNull()) << "ebuf: " << ebuf();
  }

  void TearDown() override {
    if (p_) {
      pcap_close(p_);
      p_ = nullptr;
    }
  }

  pcap_t *pcap_handle() { return p_; }

  char *ebuf() { return ebuf_.data(); }

 private:
  std::array<char, PCAP_ERRBUF_SIZE> ebuf_ = {};
  pcap_t *p_;
};

TEST_F(LibpcapTest, Breakloop) {
  int res;
  ASSERT_EQ(res = pcap_set_timeout(pcap_handle(), -1 /* infinite timeout */), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());
  ASSERT_EQ(res = pcap_activate(pcap_handle()), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());

  std::thread breaker([this]() {
    // Give some time for the main test thraed to block on `pcap_dispatch`.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    pcap_breakloop(pcap_handle());
  });
  auto d = fit::defer([&] { breaker.join(); });

  res = pcap_dispatch(
      pcap_handle(), 1 /* max packets */,
      [](u_char *user, const pcap_pkthdr *hdr, const u_char *data) {
        ADD_FAILURE() << "unexpectedly called dispatch callback";
      },
      nullptr /* user */);
  ASSERT_GE(res, PCAP_ERROR_BREAK)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());
}

void TestSetNonblockAndActivate(pcap_t *p, int timeout_ms, char *ebuf) {
  int res;
  ASSERT_EQ(res = pcap_set_timeout(p, timeout_ms), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(p);
  ASSERT_EQ(res = pcap_setnonblock(p, 1 /* nonblock */, ebuf), 0)
      << pcap_statustostr(res) << "; ebuf: " << ebuf;
  ASSERT_EQ(res = pcap_activate(p), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(p);

  res = pcap_getnonblock(p, ebuf);
  ASSERT_GE(res, 0) << pcap_statustostr(res) << "; ebuf: " << ebuf;
  ASSERT_EQ(res, 1);
  res = pcap_dispatch(
      p, 1 /* max packets */,
      [](u_char *user, const pcap_pkthdr *hdr, const u_char *data) {
        ADD_FAILURE() << "unexpectedly called dispatch callback";
      },
      nullptr /* user */);
  ASSERT_GE(res, 0) << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(p);
  EXPECT_EQ(res, 0 /* expected packets */);
}

TEST_F(LibpcapTest, NonblockWithInfiniteTimeout) {
  ASSERT_NO_FATAL_FAILURE(
      TestSetNonblockAndActivate(pcap_handle(), -1 /* infinite timeout */, ebuf()));
}

class LibpcapPacketTest : public LibpcapTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(LibpcapTest::SetUp());
    ASSERT_TRUE(udp_ = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
    ASSERT_NO_FATAL_FAILURE(BindToLoopback(udp_));
  }

  void TearDown() override {
    EXPECT_EQ(udp_.reset(), 0) << strerror(errno);

    LibpcapTest::TearDown();
  }

  void Send() { ASSERT_NO_FATAL_FAILURE(Send(udp_)); }

  void Send(const fbl::unique_fd &dst) {
    sockaddr_in dst_addr;
    ASSERT_NO_FATAL_FAILURE(LoadSockaddr(dst, dst_addr));

    ASSERT_EQ(sendto(udp_.get(), nullptr, 0, 0, reinterpret_cast<const sockaddr *>(&dst_addr),
                     sizeof(dst_addr)),
              0)
        << strerror(errno);
  }

  void PcapDispatch(uint8_t pkttype, int max_packets, int expected_packets,
                    uint16_t expected_dst_port = 0) {
    sockaddr_in addr;
    ASSERT_NO_FATAL_FAILURE(LoadSockaddr(udp_, addr));
    uint16_t bound_udp_port = ntohs(addr.sin_port);
    packet_context ctx = {
        .src_port = bound_udp_port,
        .dst_port = bound_udp_port,
        .pkttype = pkttype,
    };

    if (expected_dst_port != 0) {
      ctx.dst_port = expected_dst_port;
    }

    int res =
        pcap_dispatch(pcap_handle(), max_packets, HandlePacket, reinterpret_cast<u_char *>(&ctx));
    ASSERT_GE(res, 0) << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());
    ASSERT_EQ(res, expected_packets);
  }

 private:
  struct packet_context {
    uint16_t src_port, dst_port;
    uint8_t pkttype;
  };

  static int GetLoopbackIndex() {
    const int loopback_ifindex = if_nametoindex(kLoopbackDeviceName);
    EXPECT_GE(loopback_ifindex, 0) << strerror(errno);
    return loopback_ifindex;
  }

  static void HandlePacket(u_char *user, const pcap_pkthdr *hdr, const u_char *data) {
    ASSERT_THAT(user, NotNull());
    ASSERT_THAT(hdr, NotNull());
    ASSERT_THAT(data, NotNull());

    packet_context *ctx = reinterpret_cast<packet_context *>(user);

    // The timeval should not be empty.
    timeval not_expected = {};
    ASSERT_NE(memcmp(&hdr->ts, &not_expected, sizeof(not_expected)), 0);

    struct {
      sll2_header sll;
      iphdr ip;
      udphdr udp;
    } __PACKED packet;
    ASSERT_EQ(hdr->caplen, sizeof(packet));
    ASSERT_EQ(hdr->len, sizeof(packet));

    memcpy(&packet, data, sizeof(packet));
    EXPECT_EQ(ntohs(packet.sll.sll2_protocol), ETH_P_IP);
    EXPECT_EQ(packet.sll.sll2_reserved_mbz, 0);
    EXPECT_EQ(ntohl(packet.sll.sll2_if_index), static_cast<unsigned int>(GetLoopbackIndex()));
    EXPECT_EQ(packet.sll.sll2_hatype, ARPHRD_ETHER);
    EXPECT_EQ(packet.sll.sll2_pkttype, ctx->pkttype);
    EXPECT_EQ(packet.sll.sll2_halen, ETH_ALEN);
    // Packet was sent through the loopback interface which has the all zeroes
    // address.
    for (int i = 0; i < packet.sll.sll2_halen; ++i) {
      EXPECT_EQ(packet.sll.sll2_addr[i], 0) << "sll2_addr byte mismatch @ idx = " << i;
    }
    // IHL hold the size of the header in 4 byte units.
    EXPECT_EQ(packet.ip.ihl, sizeof(iphdr) / 4);
    EXPECT_EQ(packet.ip.version, static_cast<u_int>(IPVERSION));
    EXPECT_EQ(ntohs(packet.ip.tot_len), sizeof(iphdr) + sizeof(udphdr));
    EXPECT_EQ(packet.ip.protocol, IPPROTO_UDP);
    EXPECT_EQ(ntohl(packet.ip.daddr), INADDR_LOOPBACK);
    EXPECT_EQ(ntohl(packet.ip.saddr), INADDR_LOOPBACK);
    EXPECT_EQ(ntohs(packet.udp.source), ctx->src_port);
    EXPECT_EQ(ntohs(packet.udp.dest), ctx->dst_port);
    EXPECT_EQ(ntohs(packet.udp.len), sizeof(udphdr));
  }

  fbl::unique_fd udp_;
};

TEST_F(LibpcapPacketTest, BlockingModes) {
  constexpr int kTimeoutMs = 1000;
  ASSERT_NO_FATAL_FAILURE(TestSetNonblockAndActivate(pcap_handle(), kTimeoutMs, ebuf()));

  auto send_and_dispatch_checks = [&]() {
    ASSERT_NO_FATAL_FAILURE(Send());
    ASSERT_NO_FATAL_FAILURE(
        PcapDispatch(PACKET_HOST, 1 /* max_packets */, 1 /* expected_packets */));
    ASSERT_NO_FATAL_FAILURE(Send());
    ASSERT_NO_FATAL_FAILURE(
        PcapDispatch(PACKET_HOST, 2 /* max_packets */, 1 /* expected_packets */));
  };
  ASSERT_NO_FATAL_FAILURE(send_and_dispatch_checks());

  // Block until packets are ready.
  int res;
  ASSERT_EQ(res = pcap_setnonblock(pcap_handle(), 0 /* nonblock */, ebuf()), 0)
      << pcap_statustostr(res) << "; ebuf: " << ebuf();
  res = pcap_getnonblock(pcap_handle(), ebuf());
  ASSERT_GE(res, 0) << pcap_statustostr(res) << "; ebuf: " << ebuf();
  ASSERT_EQ(res, 0);

  // Make sure that when no packets are ready, we block for at least the
  // specified timeout.
  std::chrono::time_point start = std::chrono::steady_clock::now();
  ASSERT_NO_FATAL_FAILURE(PcapDispatch(PACKET_HOST, 1 /* max_packets */, 0 /* expected_packets */));
  EXPECT_GE(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                  start)
                .count(),
            kTimeoutMs);
  ASSERT_NO_FATAL_FAILURE(send_and_dispatch_checks());
}

TEST_F(LibpcapPacketTest, Filter) {
  int res;
  ASSERT_EQ(res = pcap_setnonblock(pcap_handle(), 1 /* nonblock */, ebuf()), 0)
      << pcap_statustostr(res) << "; ebuf: " << ebuf();
  ASSERT_EQ(res = pcap_activate(pcap_handle()), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());

  fbl::unique_fd filtered_dst;
  ASSERT_TRUE(filtered_dst = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(BindToLoopback(filtered_dst));
  sockaddr_in filtered_dstaddr;
  ASSERT_NO_FATAL_FAILURE(LoadSockaddr(filtered_dst, filtered_dstaddr));

  uint16_t filtered_port = ntohs(filtered_dstaddr.sin_port);
  std::string filter = "udp dst port " + std::to_string(filtered_port);
  bpf_program bpf;
  ASSERT_EQ(res = pcap_compile(pcap_handle(), &bpf, filter.data(), 0 /* optimize */,
                               PCAP_NETMASK_UNKNOWN),
            0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());
  res = pcap_setfilter(pcap_handle(), &bpf);
  pcap_freecode(&bpf);
  ASSERT_EQ(res, 0) << "pcap_setfilter" << pcap_statustostr(res)
                    << "; pcap error: " << pcap_geterr(pcap_handle());

  fbl::unique_fd nonfiltered_dst;
  ASSERT_TRUE(nonfiltered_dst = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(BindToLoopback(nonfiltered_dst));
  sockaddr_in nonfiltered_dstaddr;
  ASSERT_NO_FATAL_FAILURE(LoadSockaddr(nonfiltered_dst, nonfiltered_dstaddr));

  // Send a packet to some other port and expect not to receive the packet.
  ASSERT_NO_FATAL_FAILURE(Send(nonfiltered_dst));
  ASSERT_NO_FATAL_FAILURE(
      PcapDispatch(PACKET_HOST, 1 /* max_packets */, 0 /* expected_packets */,
                   ntohs(nonfiltered_dstaddr.sin_port) /* expected_dst_port */));

  // Send a packet to the filtered port and expect to receive the packet.
  ASSERT_NO_FATAL_FAILURE(Send(filtered_dst));
  ASSERT_NO_FATAL_FAILURE(PcapDispatch(PACKET_HOST, 2 /* max_packets */, 1 /* expected_packets */,
                                       filtered_port /* expected_dst_port */));
}

class LibpcapPacketDirectionTest
    : public LibpcapPacketTest,
      public ::testing::WithParamInterface<std::tuple<pcap_direction_t, uint8_t>> {};

TEST_P(LibpcapPacketDirectionTest, FilterTest) {
  auto [direction, pkttype] = GetParam();

  int res;
  ASSERT_EQ(res = pcap_activate(pcap_handle()), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());
  ASSERT_EQ(res = pcap_setdirection(pcap_handle(), direction), 0)
      << pcap_statustostr(res) << "; pcap error: " << pcap_geterr(pcap_handle());

  // We only wrote one packet so we should only read one packet.
  ASSERT_NO_FATAL_FAILURE(Send());
  ASSERT_NO_FATAL_FAILURE(PcapDispatch(pkttype, 2 /* max_packets */, 1 /* expected_packets */));
}

INSTANTIATE_TEST_SUITE_P(LibpcapTests, LibpcapPacketDirectionTest,
                         Values(std::make_tuple(PCAP_D_INOUT, PACKET_HOST),
                                std::make_tuple(PCAP_D_IN, PACKET_HOST),
                                std::make_tuple(PCAP_D_OUT, PACKET_OUTGOING)));

}  // namespace
