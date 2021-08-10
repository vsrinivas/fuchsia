// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <sys/socket.h>

#ifdef __linux__
#include <sys/syscall.h>

#include <linux/capability.h>
#endif

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

  struct sockaddr_in6 send_addr = {
      .sin6_family = AF_INET6,
      // If spcified, the port value must not be different from the associated
      // protocol of the raw IPv6 socket.
      .sin6_port = IPPROTO_TCP,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  char payload[1] = {};
  ASSERT_EQ(sendto(fd.get(), &payload, sizeof(payload), 0,
                   reinterpret_cast<struct sockaddr*>(&send_addr), sizeof(send_addr)),
            -1);
  ASSERT_EQ(errno, EINVAL);

  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(RawSocketTest, SendToDifferentProtocolV4) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  const uint32_t loopback_as_u32 = htonl(INADDR_LOOPBACK);

  fbl::unique_fd udp;
  ASSERT_TRUE(udp = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);

  fbl::unique_fd tcp;
  ASSERT_TRUE(tcp = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_TCP))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = loopback_as_u32,
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
  auto recv = [&loopback_as_u32, &addrstr](const fbl::unique_fd& fd, uint16_t proto, char payload) {
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
    EXPECT_EQ(ip->saddr, loopback_as_u32);
    EXPECT_EQ(ip->daddr, loopback_as_u32);
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

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  ASSERT_EQ(bind(udpfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(udpfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char payload[] = {1, 2, 3, 4};
  char raw_udp_buf[sizeof(udphdr) + sizeof(payload)] = {};
  *(reinterpret_cast<udphdr*>(raw_udp_buf)) = udphdr{
      .uh_sport = htons(1337),
      .uh_dport = addr.sin6_port,
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
  {
    struct sockaddr_in6 raw_sendto_addr = addr;
    raw_sendto_addr.sin6_port = 0;
    ASSERT_EQ(sendto(rawfd.get(), raw_udp_buf, sizeof(raw_udp_buf), 0,
                     reinterpret_cast<struct sockaddr*>(&raw_sendto_addr), sizeof(raw_sendto_addr)),
              ssize_t(sizeof(raw_udp_buf)))
        << strerror(errno);
  }

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
    // Flip the ports as the packet was sent from the peer.
    udphdr* udp = reinterpret_cast<udphdr*>(raw_udp_buf);
    udp->uh_dport = udp->uh_sport;
    udp->uh_sport = addr.sin6_port;
    SCOPED_TRACE("check packet sent with UDP socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  ASSERT_EQ(close(rawfd.release()), 0) << strerror(errno);
  ASSERT_EQ(close(udpfd.release()), 0) << strerror(errno);
}

TEST(RawSocketTest, SendtoRecvfrom) {
  SKIP_IF_CANT_ACCESS_RAW_SOCKETS();

  const uint32_t loopback_as_u32 = htonl(INADDR_LOOPBACK);

  fbl::unique_fd udpfd;
  ASSERT_TRUE(udpfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = loopback_as_u32,
  };

  ASSERT_EQ(bind(udpfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(udpfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char payload[] = {1, 2, 3, 4};
  char raw_udp_buf[sizeof(udphdr) + sizeof(payload)] = {};
  *(reinterpret_cast<udphdr*>(raw_udp_buf)) = udphdr{
      .uh_sport = htons(1337),
      .uh_dport = addr.sin_port,
      .uh_ulen = htons(sizeof(raw_udp_buf)),
      .uh_sum = 0,  // Checksum is optional for UDP on IPv4.
  };
  memcpy(raw_udp_buf + sizeof(udphdr), payload, sizeof(payload));

  fbl::unique_fd rawfd;
  ASSERT_TRUE(rawfd = fbl::unique_fd(socket(AF_INET, SOCK_RAW, IPPROTO_UDP))) << strerror(errno);
  ASSERT_EQ(sendto(rawfd.get(), raw_udp_buf, sizeof(raw_udp_buf), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), addrlen),
            ssize_t(sizeof(raw_udp_buf)))
      << strerror(errno);

  char addrbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);

  {
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
    int yes = 1;
    ASSERT_EQ(setsockopt(udpfd.get(), SOL_SOCKET, SO_NO_CHECK, &yes, sizeof yes), 0)
        << strerror(errno);
    ASSERT_EQ(sendto(udpfd.get(), payload, sizeof(payload), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), peerlen),
              ssize_t(sizeof(payload)))
        << strerror(errno);
  }

  auto check_packet = [loopback_as_u32, &rawfd, &raw_udp_buf, addrstr]() {
    char read_raw_ip_buf[sizeof(iphdr) + sizeof(udphdr) + sizeof(payload)] = {};
    struct sockaddr_in peer;
    socklen_t peerlen = sizeof(peer);
    ASSERT_EQ(recvfrom(rawfd.get(), read_raw_ip_buf, sizeof(read_raw_ip_buf), 0,
                       reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
              ssize_t(sizeof(read_raw_ip_buf)))
        << strerror(errno);
    ASSERT_EQ(peerlen, sizeof(peer));
    iphdr* ip = reinterpret_cast<iphdr*>(read_raw_ip_buf);
    EXPECT_EQ(ntohs(ip->tot_len), sizeof(iphdr) + sizeof(udphdr) + sizeof(payload));
    EXPECT_EQ(ntohs(ip->frag_off) & IP_OFFMASK, 0);
    EXPECT_EQ(ip->protocol, SOL_UDP);
    EXPECT_EQ(ip->saddr, loopback_as_u32);
    EXPECT_EQ(ip->daddr, loopback_as_u32);
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
    // Flip the ports as the packet was sent from the peer.
    udphdr* udp = reinterpret_cast<udphdr*>(raw_udp_buf);
    udp->uh_dport = udp->uh_sport;
    udp->uh_sport = addr.sin_port;
    SCOPED_TRACE("check packet sent with UDP socket");
    ASSERT_NO_FATAL_FAILURE(check_packet());
  }

  ASSERT_EQ(close(rawfd.release()), 0) << strerror(errno);
  ASSERT_EQ(close(udpfd.release()), 0) << strerror(errno);
}

// Fixture for tests parameterized by family and protocol.
class RawSocketTest : public ::testing::TestWithParam<std::tuple<int, int>> {
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
  ASSERT_EQ(optlen, sizeof(int));
  EXPECT_EQ(opt, SOCK_RAW);
}

INSTANTIATE_TEST_SUITE_P(AllRawSocketTests, RawSocketTest,
                         ::testing::Combine(::testing::Values(AF_INET, AF_INET6),
                                            ::testing::Values(IPPROTO_TCP, IPPROTO_UDP,
                                                              IPPROTO_RAW)));

};  // namespace
