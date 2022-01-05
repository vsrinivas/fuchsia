// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <sys/socket.h>

#ifdef __linux__
#include <sys/syscall.h>

#include <linux/capability.h>
#endif

#include <zircon/compiler.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace {

#ifdef __linux__
#define SKIP_IF_CANT_ACCESS_RAW_SOCKETS()                                              \
  do {                                                                                 \
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};         \
    struct __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3] = {};                 \
    auto ret = syscall(SYS_capget, &header, &caps);                                    \
    ASSERT_GE(ret, 0) << strerror(errno);                                              \
    if ((caps[CAP_TO_INDEX(CAP_NET_RAW)].effective & CAP_TO_MASK(CAP_NET_RAW)) == 0) { \
      GTEST_SKIP() << "Do not have CAP_NET_RAW capability";                            \
    }                                                                                  \
  } while (false)
#else
#define SKIP_IF_CANT_ACCESS_RAW_SOCKETS() ((void*)0)
#endif

TEST(RawSocketTest, ProtocolZeroNotSupported) {
  // This test intentionally does not check if we have access to raw sockets as
  // this test is independent of raw socket capabilities; a protocol value of 0
  // is always not supported for raw IP sockets.
  ASSERT_EQ(socket(AF_INET, SOCK_RAW, 0), -1);
  ASSERT_EQ(errno, EPROTONOSUPPORT) << strerror(errno);
}

TEST(RawSocketTest, SendToDifferentProtocolV6) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

#ifdef __Fuchsia__
  GTEST_SKIP() << "Does not pass on fuchsia (https://gvisor.dev/issue/6422)";
#endif  // __Fuchsia__

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  const struct sockaddr_in6 send_addr = {
      .sin6_family = AF_INET6,
      // If spcified, the port value must not be different from the associated
      // protocol of the raw IPv6 socket.
      .sin6_port = IPPROTO_TCP,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  char payload[1] = {};
  ASSERT_EQ(sendto(fd.get(), &payload, sizeof(payload), 0,
                   reinterpret_cast<const struct sockaddr*>(&send_addr), sizeof(send_addr)),
            -1);
  ASSERT_EQ(errno, EINVAL);

  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(RawSocketTest, SendToDifferentProtocolV4) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  const uint32_t network_endian_loopback_addr = htonl(INADDR_LOOPBACK);

  fbl::unique_fd udp;
  ASSERT_TRUE(udp = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  fbl::unique_fd tcp;
  ASSERT_TRUE(tcp = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_TCP))) << strerror(errno);

  const struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = network_endian_loopback_addr,
          },
  };

  char addrbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);

  auto send = [&addr](const fbl::unique_fd& fd, uint16_t proto, char payload) {
    struct sockaddr_in send_addr = addr;
    // The port is ignored when sending through raw IPv4 sockets.
    send_addr.sin_port = proto;

    ASSERT_EQ(sendto(fd.get(), &payload, sizeof(payload), 0,
                     reinterpret_cast<struct sockaddr*>(&send_addr), sizeof(send_addr)),
              ssize_t(sizeof(payload)))
        << strerror(errno);
  };
  {
    SCOPED_TRACE("try to send TCP through UDP associatd raw IP socket");
    ASSERT_NO_FATAL_FAILURE(send(udp, IPPROTO_TCP /* proto */, IPPROTO_UDP /* payload */));
  }
  {
    SCOPED_TRACE("try to send UDP through TCP associatd raw IP socket");
    ASSERT_NO_FATAL_FAILURE(send(tcp, IPPROTO_UDP /* proto */, IPPROTO_TCP /* payload */));
  }

  // Receive a packet through the raw socket and make sure the transport
  // protocol matches what the socket is associated with.
  auto recv = [&network_endian_loopback_addr, &addrstr](const fbl::unique_fd& fd, uint16_t proto,
                                                        char payload) {
    char read_raw_ip_buf[sizeof(iphdr) + sizeof(payload)] = {};
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(fd.get(), &read_raw_ip_buf, sizeof(read_raw_ip_buf), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(read_raw_ip_buf)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    iphdr* ip = reinterpret_cast<iphdr*>(read_raw_ip_buf);
    EXPECT_EQ(ntohs(ip->tot_len), sizeof(read_raw_ip_buf));
    EXPECT_EQ(ntohs(ip->frag_off) & IP_OFFMASK, 0);
    EXPECT_EQ(ip->protocol, proto);
    EXPECT_EQ(ip->saddr, network_endian_loopback_addr);
    EXPECT_EQ(ip->daddr, network_endian_loopback_addr);
    EXPECT_EQ(read_raw_ip_buf[sizeof(iphdr)], payload);
    char peerbuf[INET_ADDRSTRLEN];
    const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
    ASSERT_NE(peerstr, nullptr);
    EXPECT_STREQ(peerstr, addrstr);
  };

  {
    SCOPED_TRACE("receive UDP message on UDP associated raw socket");
    ASSERT_NO_FATAL_FAILURE(recv(udp, IPPROTO_UDP /* proto */, IPPROTO_UDP /* payload */));
  }
  {
    SCOPED_TRACE("receive TCP message on TCP associated raw socket");
    ASSERT_NO_FATAL_FAILURE(recv(tcp, IPPROTO_TCP /* proto */, IPPROTO_TCP /* payload */));
  }

  ASSERT_EQ(close(tcp.release()), 0) << strerror(errno);
  ASSERT_EQ(close(udp.release()), 0) << strerror(errno);
}

TEST(RawSocketTest, SendtoRecvfromV6) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd udpfd;
  ASSERT_TRUE(udpfd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  const uint16_t udpfd_bound_port = 1335;
  const uint16_t udpfd_peer_port = 1336;
  const struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  {
    struct sockaddr_in6 bind_addr = addr;
    bind_addr.sin6_port = htons(udpfd_bound_port);
    ASSERT_EQ(
        bind(udpfd.get(), reinterpret_cast<const struct sockaddr*>(&bind_addr), sizeof(bind_addr)),
        0)
        << strerror(errno);
  }

  const char payload[] = {1, 2, 3, 4};
  char raw_udp_buf[sizeof(udphdr) + sizeof(payload)] = {};
  *(reinterpret_cast<udphdr*>(raw_udp_buf)) = udphdr{
      .uh_sport = htons(udpfd_peer_port),
      .uh_dport = htons(udpfd_bound_port),
      .uh_ulen = htons(sizeof(raw_udp_buf)),
      .uh_sum = 0,  // Checksumming is not required for packets that are looped
                    // back on Fuchsia. For Linux, we use IPV6_CHECKSUM below.
  };
  memcpy(raw_udp_buf + sizeof(udphdr), payload, sizeof(payload));

  fbl::unique_fd rawfd;
  ASSERT_TRUE(rawfd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

#ifdef __linux__
  {
    // Fuchsia does not require checksumming if the packet is looped back but
    // Linux always does so we ask the netstack to populate the checksum on
    // Linux.
    int offset = offsetof(udphdr, uh_sum);
    ASSERT_EQ(setsockopt(rawfd.get(), IPPROTO_IPV6, IPV6_CHECKSUM, &offset, sizeof(offset)), 0)
        << strerror(errno);
  }
#endif
  ASSERT_EQ(sendto(rawfd.get(), raw_udp_buf, sizeof(raw_udp_buf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            ssize_t(sizeof(raw_udp_buf)))
      << strerror(errno);

  char addrbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin6_family, &addr.sin6_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);

  {
    char read_payload[sizeof(payload)] = {};
    struct sockaddr_in6 peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(udpfd.get(), read_payload, sizeof(read_payload), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(payload)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    for (size_t i = 0; i < sizeof(payload); i++) {
      EXPECT_EQ(payload[i], read_payload[i]) << "byte mismatch @ idx=" << i;
    }
    char peerbuf[INET_ADDRSTRLEN];
    const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
    ASSERT_NE(peerstr, nullptr);
    EXPECT_STREQ(peerstr, addrstr);
    EXPECT_EQ(ntohs(peer.sin6_port), udpfd_peer_port);
    ASSERT_EQ(sendto(udpfd.get(), payload, sizeof(payload), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), peerlen),
              ssize_t(sizeof(payload)))
        << strerror(errno);
  }

  auto check_packet = [&rawfd, &raw_udp_buf, addrstr]() {
    char read_raw_udp_buf[sizeof(udphdr) + sizeof(payload)] = {};
    struct sockaddr_in6 peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(rawfd.get(), read_raw_udp_buf, sizeof(read_raw_udp_buf), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(read_raw_udp_buf)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    reinterpret_cast<udphdr*>(read_raw_udp_buf)->uh_sum = 0;
    for (size_t i = 0; i < sizeof(raw_udp_buf); i++) {
      EXPECT_EQ(raw_udp_buf[i], read_raw_udp_buf[i]) << "byte mismatch @ idx=" << i;
    }
    char peerbuf[INET_ADDRSTRLEN];
    const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
    ASSERT_NE(peerstr, nullptr);
    EXPECT_STREQ(peerstr, addrstr);
  };

  // The packet we originally wrote should be received by the raw socket since
  // the socket is bound to loopback and the UDP packet is sent to loopback.
  //
  // Note that for IPv6, raw sockets never return the IPv6 header, unlike IPv4.
  {
    SCOPED_TRACE("check packet sent with raw socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  // Validate the packet sent with the UDP socket.
  {
    // Update expected ports as the packet was sent from the peer.
    udphdr* udp = reinterpret_cast<udphdr*>(raw_udp_buf);
    udp->uh_dport = htons(udpfd_peer_port);
    udp->uh_sport = htons(udpfd_bound_port);
    SCOPED_TRACE("check packet sent with UDP socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  ASSERT_EQ(close(rawfd.release()), 0) << strerror(errno);
  ASSERT_EQ(close(udpfd.release()), 0) << strerror(errno);
}

TEST(RawSocketTest, SendtoRecvfrom) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  const uint16_t udpfd_bound_port = 1338;
  const uint16_t udpfd_peer_port = 1337;
  const uint32_t network_endian_loopback_addr = htonl(INADDR_LOOPBACK);

  fbl::unique_fd udpfd;
  ASSERT_TRUE(udpfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  const struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr =
          {
              .s_addr = network_endian_loopback_addr,
          },
  };

  {
    struct sockaddr_in bind_addr = addr;
    bind_addr.sin_port = htons(udpfd_bound_port);
    ASSERT_EQ(
        bind(udpfd.get(), reinterpret_cast<const struct sockaddr*>(&bind_addr), sizeof(bind_addr)),
        0)
        << strerror(errno);
  }

  const char payload[] = {1, 2, 3, 4};
  char raw_udp_buf[sizeof(udphdr) + sizeof(payload)] = {};
  *(reinterpret_cast<udphdr*>(raw_udp_buf)) = udphdr{
      .uh_sport = htons(udpfd_peer_port),
      .uh_dport = htons(udpfd_bound_port),
      .uh_ulen = htons(sizeof(raw_udp_buf)),
      .uh_sum = 0,  // Checksum is optional for UDP on IPv4.
  };
  memcpy(raw_udp_buf + sizeof(udphdr), payload, sizeof(payload));

  fbl::unique_fd rawfd;
  ASSERT_TRUE(rawfd = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);
  ASSERT_EQ(sendto(rawfd.get(), raw_udp_buf, sizeof(raw_udp_buf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            ssize_t(sizeof(raw_udp_buf)))
      << strerror(errno);

  char addrbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);

  auto check_payload = [&udpfd, &payload, addrstr, udpfd_peer_port]() {
    char read_payload[sizeof(payload)] = {};
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(udpfd.get(), read_payload, sizeof(read_payload), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(payload)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    for (size_t i = 0; i < sizeof(payload); i++) {
      EXPECT_EQ(payload[i], read_payload[i]) << "byte mismatch @ idx=" << i;
    }
    char peerbuf[INET_ADDRSTRLEN];
    const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
    ASSERT_NE(peerstr, nullptr);
    EXPECT_STREQ(peerstr, addrstr);
    EXPECT_EQ(ntohs(peer.sin_port), udpfd_peer_port);
  };
  {
    SCOPED_TRACE("check UDP payload sent with raw socket");
    ASSERT_NO_FATAL_FAILURE(check_payload());
  }
  {
    int yes = 1;
    ASSERT_EQ(setsockopt(udpfd.get(), SOL_SOCKET, SO_NO_CHECK, &yes, sizeof(yes)), 0)
        << strerror(errno);

    struct sockaddr_in peer = addr;
    peer.sin_port = htons(udpfd_peer_port);
    ASSERT_EQ(sendto(udpfd.get(), payload, sizeof(payload), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), sizeof(peer)),
              ssize_t(sizeof(payload)))
        << strerror(errno);
  }

  const unsigned int EXPECTED_IHL =
      sizeof(iphdr) / 4;  // IHL holds the number of header bytes in 4 byte units.

  auto check_packet = [EXPECTED_IHL, network_endian_loopback_addr, &rawfd, &raw_udp_buf,
                       addrstr]() {
    char read_raw_ip_buf[sizeof(iphdr) + sizeof(udphdr) + sizeof(payload)] = {};
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(rawfd.get(), read_raw_ip_buf, sizeof(read_raw_ip_buf), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(read_raw_ip_buf)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    iphdr* ip = reinterpret_cast<iphdr*>(read_raw_ip_buf);
    EXPECT_EQ(ip->version, static_cast<unsigned int>(IPVERSION));
    EXPECT_EQ(ip->ihl, EXPECTED_IHL);
    EXPECT_EQ(ntohs(ip->tot_len), sizeof(iphdr) + sizeof(udphdr) + sizeof(payload));
    EXPECT_EQ(ntohs(ip->frag_off) & IP_OFFMASK, 0);
    EXPECT_EQ(ip->protocol, SOL_UDP);
    EXPECT_EQ(ip->saddr, network_endian_loopback_addr);
    EXPECT_EQ(ip->daddr, network_endian_loopback_addr);
    char* read_raw_udp_buf = &read_raw_ip_buf[sizeof(iphdr)];
    for (size_t i = 0; i < sizeof(raw_udp_buf); i++) {
      EXPECT_EQ(raw_udp_buf[i], read_raw_udp_buf[i]) << "byte mismatch @ idx=" << i;
    }
    char peerbuf[INET_ADDRSTRLEN];
    const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
    ASSERT_NE(peerstr, nullptr);
    EXPECT_STREQ(peerstr, addrstr);
  };

  // The packet we originally wrote should be received by the raw socket since
  // the socket is bound to loopback and the UDP packet is sent to loopback.
  //
  // IPv4 always returns the IPv4 header when reading from a raw socket.
  {
    SCOPED_TRACE("check packet sent with raw socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  // Validate the packet sent with the UDP socket.
  {
    // Update expected ports as the packet was sent from the peer.
    udphdr* udp = reinterpret_cast<udphdr*>(raw_udp_buf);
    udp->uh_dport = htons(udpfd_peer_port);
    udp->uh_sport = htons(udpfd_bound_port);
    SCOPED_TRACE("check packet sent with UDP socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  // Attempt to write the packet but with the IPv4 header included.
  {
    udphdr* udp = reinterpret_cast<udphdr*>(raw_udp_buf);
    udp->uh_dport = htons(udpfd_bound_port);
    udp->uh_sport = htons(udpfd_peer_port);
    char raw_ip_buf[sizeof(iphdr) + sizeof(udphdr) + sizeof(payload)] = {};
    iphdr* ip = reinterpret_cast<iphdr*>(raw_ip_buf);
    ip->version = IPVERSION;
    ip->ihl = EXPECTED_IHL;
    ip->ttl = 2;
    ip->protocol = SOL_UDP;
    ip->saddr = network_endian_loopback_addr;
    ip->daddr = network_endian_loopback_addr;
    memcpy(raw_ip_buf + sizeof(iphdr), raw_udp_buf, sizeof(raw_udp_buf));
    int yes = 1;
    ASSERT_EQ(setsockopt(rawfd.get(), SOL_IP, IP_HDRINCL, (void*)&yes, sizeof(yes)), 0)
        << strerror(errno);
    ASSERT_EQ(sendto(rawfd.get(), raw_ip_buf, sizeof(raw_ip_buf), 0,
                     reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
              ssize_t(sizeof(raw_ip_buf)))
        << strerror(errno);
  }
  {
    SCOPED_TRACE("check UDP payload sent with raw socket with IP header included");
    ASSERT_NO_FATAL_FAILURE(check_payload());
  }

  ASSERT_EQ(close(rawfd.release()), 0) << strerror(errno);
  ASSERT_EQ(close(udpfd.release()), 0) << strerror(errno);
}

// Fixture for tests parameterized by family and protocol.
class RawSocketTest : public testing::TestWithParam<std::tuple<int, int>> {
 protected:
  // Creates a socket to be used in tests.
  void SetUp() override {
    SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

    const auto& [family, protocol] = GetParam();

    ASSERT_TRUE(fd_ = fbl::unique_fd(socket(family, SOCK_RAW, protocol))) << strerror(errno);
  }

  const fbl::unique_fd& fd() const { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_P(RawSocketTest, SockOptSoProtocol) {
  const auto& [family, protocol] = GetParam();

  int opt;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_PROTOCOL, &opt, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(opt));
  EXPECT_EQ(opt, protocol);
}

TEST_P(RawSocketTest, SockOptSoType) {
  int opt;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_TYPE, &opt, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(opt));
  EXPECT_EQ(opt, SOCK_RAW);
}

TEST_P(RawSocketTest, SockOptIPHdrIncl) {
  const auto& [family, protocol] = GetParam();

  int opt;
  socklen_t optlen = sizeof(opt);
// TODO(https://fxbug.deb/90810): Don't support IP_HDRINCL on raw IPv6 sockets.
#ifndef __Fuchsia__
  if (family == AF_INET) {
#endif  // __Fuchsia__
    ASSERT_EQ(getsockopt(fd().get(), SOL_IP, IP_HDRINCL, &opt, &optlen), 0) << strerror(errno);
    ASSERT_EQ(optlen, sizeof(opt));
    EXPECT_EQ(opt, protocol == IPPROTO_RAW);

    constexpr int yes = 1;
    ASSERT_EQ(setsockopt(fd().get(), SOL_IP, IP_HDRINCL, &yes, sizeof(yes)), 0) << strerror(errno);
    ASSERT_EQ(getsockopt(fd().get(), SOL_IP, IP_HDRINCL, &opt, &optlen), 0) << strerror(errno);
    ASSERT_EQ(optlen, sizeof(opt));
    EXPECT_EQ(opt, yes);
#ifndef __Fuchsia__
  } else {
    ASSERT_EQ(getsockopt(fd().get(), SOL_IP, IP_HDRINCL, &opt, &optlen), -1);
    EXPECT_EQ(errno, ENOPROTOOPT) << strerror(errno);
  }
#endif  // __Fuchsia__
}

INSTANTIATE_TEST_SUITE_P(AllRawSocketTests, RawSocketTest,
                         testing::Combine(testing::Values(AF_INET, AF_INET6),
                                          testing::Values(IPPROTO_TCP, IPPROTO_UDP, IPPROTO_RAW)));

TEST(RawSocketICMPv6Test, GetSetFilterSucceeds) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6))) << strerror(errno);

  icmp6_filter set_filter;
  ICMP6_FILTER_SETBLOCKALL(&set_filter);
  ICMP6_FILTER_SETPASS(std::numeric_limits<uint8_t>::min(), &set_filter);
  ICMP6_FILTER_SETPASS(std::numeric_limits<uint8_t>::max() / 2, &set_filter);
  ICMP6_FILTER_SETPASS(std::numeric_limits<uint8_t>::max(), &set_filter);
  ASSERT_EQ(setsockopt(fd.get(), SOL_ICMPV6, ICMP6_FILTER, &set_filter, sizeof(set_filter)), 0)
      << strerror(errno);

  {
    icmp6_filter got_filter;
    socklen_t got_filter_len = sizeof(got_filter);
    ASSERT_EQ(getsockopt(fd.get(), SOL_ICMPV6, ICMP6_FILTER, &got_filter, &got_filter_len), 0)
        << strerror(errno);
    EXPECT_EQ(memcmp(&got_filter, &set_filter, sizeof(set_filter)), 0);
  }

  // We use a length smaller than a full filter length and expect that only the
  // bytes up to the provided length are modified. The last element should be
  // unmodified when getsockopt returns.
  {
    icmp6_filter got_filter = {};
    constexpr socklen_t kShortFilterLen = sizeof(got_filter) - sizeof(got_filter.icmp6_filt[0]);
    socklen_t got_filter_len = kShortFilterLen;
    ASSERT_EQ(getsockopt(fd.get(), SOL_ICMPV6, ICMP6_FILTER, &got_filter, &got_filter_len), 0)
        << strerror(errno);
    ASSERT_EQ(got_filter_len, kShortFilterLen);
    icmp6_filter expected_filter = set_filter;
    expected_filter.icmp6_filt[std::size(expected_filter.icmp6_filt) - 1] = 0;
    EXPECT_EQ(memcmp(&got_filter, &expected_filter, sizeof(expected_filter)), 0);
  }
}

TEST(RawSocketICMPv6Test, FilterICMPPackets) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW | SOCK_NONBLOCK, IPPROTO_ICMPV6)))
      << strerror(errno);

  constexpr sockaddr_in6 kLoopbackAddr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  constexpr uint8_t kAllowedType = 111;

  // Pass only the allowed type.
  {
    icmp6_filter set_filter;
    ICMP6_FILTER_SETBLOCKALL(&set_filter);
    ICMP6_FILTER_SETPASS(kAllowedType, &set_filter);
    ASSERT_EQ(setsockopt(fd.get(), SOL_ICMPV6, ICMP6_FILTER, &set_filter, sizeof(set_filter)), 0)
        << strerror(errno);
  }

  // Send an ICMP packet for each type.
  uint8_t icmp_type = 0;
  constexpr uint8_t kUnusedICMPCode = 0;
  do {
    const icmp6_hdr packet = {
        .icmp6_type = icmp_type,
        .icmp6_code = kUnusedICMPCode,
        // The stack will calculate the checksum.
        .icmp6_cksum = 0,
    };

    ssize_t n = sendto(fd.get(), &packet, sizeof(packet), 0,
                       reinterpret_cast<const sockaddr*>(&kLoopbackAddr), sizeof(kLoopbackAddr));
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(packet)));
  } while (icmp_type++ != std::numeric_limits<uint8_t>::max());

  // Make sure only the allowed type was received.
  {
    icmp6_hdr got_packet;
    sockaddr_in6 sender;
    socklen_t sender_len = sizeof(sender);
    ssize_t n = recvfrom(fd.get(), &got_packet, sizeof(got_packet), 0 /* flags */,
                         reinterpret_cast<sockaddr*>(&sender), &sender_len);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(got_packet)));
    ASSERT_EQ(sender_len, sizeof(sender));
    EXPECT_EQ(memcmp(&sender, &kLoopbackAddr, sizeof(kLoopbackAddr)), 0);
    EXPECT_EQ(got_packet.icmp6_type, kAllowedType);
    EXPECT_EQ(got_packet.icmp6_code, kUnusedICMPCode);
    EXPECT_NE(got_packet.icmp6_cksum, 0);
  }

  // Make sure no more packets delivered to the raw socket.
  {
    icmp6_hdr got_packet;
    sockaddr_in6 sender;
    socklen_t sender_len = sizeof(sender);
    ASSERT_EQ(recvfrom(fd.get(), &got_packet, sizeof(got_packet), 0 /* flags */,
                       reinterpret_cast<sockaddr*>(&sender), &sender_len),
              -1);
    EXPECT_EQ(errno, EAGAIN);
  }
}

TEST(RawSocketICMPv6Test, NegativeIPv6ChecksumsFoldToNegativeOne) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  auto check = [&](int v) {
    ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &v, sizeof(v)), 0) << strerror(errno);

    int got;
    socklen_t got_len = sizeof(got);
    ASSERT_EQ(getsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &got, &got_len), 0) << strerror(errno);
    ASSERT_EQ(got_len, sizeof(got));
    EXPECT_EQ(got, -1);
  };

  ASSERT_NO_FATAL_FAILURE(check(-1));
  ASSERT_NO_FATAL_FAILURE(check(-2));
  ASSERT_NO_FATAL_FAILURE(std::numeric_limits<int>::min());
}

TEST(RawSocketICMPv6Test, SetIPv6ChecksumErrorForOddValues) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  int intV = 3;
  ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &intV, sizeof(intV)), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  intV = 5;
  ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &intV, sizeof(intV)), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
}

TEST(RawSocketICMPv6Test, SetIPv6ChecksumSuccessForEvenValues) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  int intV = 2;
  ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &intV, sizeof(intV)), 0)
      << strerror(errno);

  intV = 4;
  ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &intV, sizeof(intV)), 0)
      << strerror(errno);
}

TEST(RawSocketICMPv6Test, IPv6Checksum_ValidateAndCalculate) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  fbl::unique_fd checksum_set;
  ASSERT_TRUE(checksum_set = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP)))
      << strerror(errno);

  fbl::unique_fd checksum_not_set;
  ASSERT_TRUE(checksum_not_set = fbl::unique_fd(socket(AF_INET6, SOCK_RAW, IPPROTO_UDP)))
      << strerror(errno);

  const sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  auto bind_and_set_checksum = [&](const fbl::unique_fd& fd, int v) {
    ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    int got;
    socklen_t got_len = sizeof(got);
    ASSERT_EQ(getsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &got, &got_len), 0) << strerror(errno);
    ASSERT_EQ(got_len, sizeof(got));
    EXPECT_EQ(got, -1);

    ASSERT_EQ(setsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &v, sizeof(v)), 0) << strerror(errno);
    ASSERT_EQ(getsockopt(fd.get(), SOL_IPV6, IPV6_CHECKSUM, &got, &got_len), 0) << strerror(errno);
    ASSERT_EQ(got_len, sizeof(got));
    EXPECT_EQ(got, v);
  };

  struct udp_packet {
    udphdr udp;
    uint32_t value;
  } __PACKED;

  ASSERT_NO_FATAL_FAILURE(
      bind_and_set_checksum(checksum_set, offsetof(udp_packet, udp) + offsetof(udphdr, uh_sum)));
  ASSERT_NO_FATAL_FAILURE(bind_and_set_checksum(checksum_not_set, -1));

  auto send = [&](const fbl::unique_fd& fd, uint32_t v) {
    const udp_packet packet = {
        .value = v,
    };

    ssize_t n = sendto(fd.get(), &packet, sizeof(packet), /*flags=*/0,
                       reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(packet)));
  };

  auto expect_receive = [&](const fbl::unique_fd& fd, uint32_t v, bool should_check_xsum) {
    udp_packet packet;
    sockaddr_in6 sender;
    socklen_t sender_len = sizeof(sender);
    ssize_t n = recvfrom(fd.get(), &packet, sizeof(packet), /*flags=*/0,
                         reinterpret_cast<sockaddr*>(&sender), &sender_len);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(packet)));
    ASSERT_EQ(sender_len, sizeof(sender));
    EXPECT_EQ(memcmp(&sender, &addr, sizeof(addr)), 0);
    EXPECT_EQ(packet.value, v);
    if (should_check_xsum) {
      EXPECT_NE(packet.udp.uh_sum, 0);
    } else {
      EXPECT_EQ(packet.udp.uh_sum, 0);
    }
  };

  uint32_t counter = 1;
  // Packets sent through checksum_not_set will not have a valid checksum set so
  // checksum_set should not accept those packets.
  ASSERT_NO_FATAL_FAILURE(send(checksum_not_set, counter));
  ASSERT_NO_FATAL_FAILURE(expect_receive(checksum_not_set, counter, false));

  // Packets sent through checksum_set will have a valid checksum so both
  // sockets should accept them.
  ASSERT_NO_FATAL_FAILURE(send(checksum_set, ++counter));
  ASSERT_NO_FATAL_FAILURE(expect_receive(checksum_set, counter, true));
  ASSERT_NO_FATAL_FAILURE(expect_receive(checksum_not_set, counter, true));
}

}  // namespace
