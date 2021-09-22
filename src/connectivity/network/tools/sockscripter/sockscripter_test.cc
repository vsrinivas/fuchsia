// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sockscripter.h"

#include <arpa/inet.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(SendBufferGenTest, LoadHexBuffer) {
  SendBufferGenerator gen;
  EXPECT_TRUE(gen.SetSendBufHex("61 62 63 64"));
  EXPECT_EQ(gen.GetSndStr(), "abcd");
  EXPECT_TRUE(gen.SetSendBufHex("61626364"));
  EXPECT_EQ(gen.GetSndStr(), "abcd");
  EXPECT_TRUE(gen.SetSendBufHex("61,62, ,6364"));
  EXPECT_EQ(gen.GetSndStr(), "abcd");
  EXPECT_FALSE(gen.SetSendBufHex("123"));
  EXPECT_FALSE(gen.SetSendBufHex("jk"));
  EXPECT_FALSE(gen.SetSendBufHex("-08"));
}

TEST(SendBufferGenTest, CounterBuffer) {
  // SendBufferGenerator defaults to sending strings with incrementing packet count.
  SendBufferGenerator gen;
  EXPECT_EQ(gen.GetSndStr(), "Packet number 0.");
  EXPECT_EQ(gen.GetSndStr(), "Packet number 1.");
  EXPECT_EQ(gen.GetSndStr(), "Packet number 2.");
  for (int i = 0; i < 7; i++) {
    gen.GetSndStr();
  }
  EXPECT_EQ(gen.GetSndStr(), "Packet number 10.");
}

class TestApi : ApiAbstraction {
 public:
  MOCK_METHOD(int, socket, (int domain, int type, int protocol), (override));

  MOCK_METHOD(int, close, (int fd), (override));

  MOCK_METHOD(int, setsockopt,
              (int fd, int level, int optname, const void* optval, socklen_t optlen), (override));

  MOCK_METHOD(int, getsockopt, (int fd, int level, int optname, void* optval, socklen_t* optlen),
              (override));

  MOCK_METHOD(int, bind, (int fd, const struct sockaddr* addr, socklen_t len), (override));

  MOCK_METHOD(int, shutdown, (int fd, int how), (override));

  MOCK_METHOD(int, connect, (int fd, const struct sockaddr* addr, socklen_t len), (override));

  MOCK_METHOD(int, accept, (int fd, struct sockaddr* addr, socklen_t* len), (override));

  MOCK_METHOD(int, listen, (int fd, int backlog), (override));

  MOCK_METHOD(ssize_t, send, (int fd, const void* buf, size_t len, int flags), (override));

  MOCK_METHOD(ssize_t, sendto,
              (int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr,
               socklen_t addrlen),
              (override));

  MOCK_METHOD(ssize_t, recv, (int fd, void* buf, size_t len, int flags), (override));

  MOCK_METHOD(ssize_t, recvfrom,
              (int fd, void* buf, size_t buflen, int flags, struct sockaddr* addr,
               socklen_t* addrlen),
              (override));

  MOCK_METHOD(int, getsockname, (int fd, struct sockaddr* addr, socklen_t* len), (override));

  MOCK_METHOD(int, getpeername, (int fd, struct sockaddr* addr, socklen_t* len), (override));

  int RunCommandLine(const std::string& line) {
    SockScripter scripter(this);

    std::unique_ptr<char[]> parsing(new char[line.length() + 1]);
    strcpy(parsing.get(), line.c_str());
    auto* p = parsing.get();
    char* start = nullptr;
    char program[] = "sockscripter";
    std::vector<char*> args;
    args.push_back(program);

    while (*p) {
      if (!start) {
        start = p;
      }
      if (*p == ' ') {
        if (strlen(start)) {
          args.push_back(start);
          start = nullptr;
        }
        *p = '\0';
      }
      p++;
    }
    if (start && strlen(start)) {
      args.push_back(start);
    }
    return scripter.Execute(static_cast<int>(args.size()), args.data());
  }
};

std::string TestPacketNumber(int c) {
  std::stringstream ss;
  ss << "Packet number " << c << ".";
  return ss.str();
}

TEST(CommandLine, RepeatConfig) {
  TestRepeatCfg cfg;
  EXPECT_TRUE(cfg.Parse("{test}[N=20][T=15]"));
  EXPECT_EQ(cfg.command, "test");
  EXPECT_EQ(cfg.repeat_count, 20);
  EXPECT_EQ(cfg.delay_ms, 15);
  EXPECT_TRUE(cfg.Parse("{test}[N=20]"));
  EXPECT_EQ(cfg.command, "test");
  EXPECT_EQ(cfg.repeat_count, 20);
  EXPECT_EQ(cfg.delay_ms, 0);
  EXPECT_TRUE(cfg.Parse("{test}[T=20]"));
  EXPECT_EQ(cfg.command, "test");
  EXPECT_EQ(cfg.repeat_count, 1);
  EXPECT_EQ(cfg.delay_ms, 20);
  EXPECT_FALSE(cfg.Parse("{test"));
  EXPECT_EQ(cfg.repeat_count, 1);
  EXPECT_EQ(cfg.delay_ms, 0);
  EXPECT_FALSE(cfg.Parse("{test}[N=hjk]"));
}

TEST(CommandLine, SocketBuild) {
  TestApi test;
  EXPECT_CALL(test, socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  EXPECT_EQ(test.RunCommandLine("udp"), 0);
  EXPECT_CALL(test, socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP));
  EXPECT_EQ(test.RunCommandLine("icmp"), 0);
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  EXPECT_EQ(test.RunCommandLine("tcp"), 0);
  EXPECT_CALL(test, socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP));
  EXPECT_EQ(test.RunCommandLine("udp6"), 0);
  EXPECT_CALL(test, socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6));
  EXPECT_EQ(test.RunCommandLine("icmp6"), 0);
  EXPECT_CALL(test, socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  EXPECT_EQ(test.RunCommandLine("tcp6"), 0);
  EXPECT_CALL(test, socket(AF_INET, SOCK_RAW, 1));
  EXPECT_EQ(test.RunCommandLine("raw 1"), 0);
  EXPECT_CALL(test, socket(AF_INET6, SOCK_RAW, 2));
  EXPECT_EQ(test.RunCommandLine("raw6 2"), 0);
  EXPECT_NE(test.RunCommandLine("raw bind"), 0);
}

constexpr int kSockFd = 15;

TEST(CommandLine, UdpBindSendToRecvFrom) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  EXPECT_CALL(test, socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, bind(kSockFd, testing::_, testing::_))
      .WillOnce([](testing::Unused, const struct sockaddr* addr, socklen_t addrlen) {
        const struct sockaddr_in expected_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(2020),
            .sin_addr =
                {
                    .s_addr = htonl(INADDR_ANY),
                },
        };
        EXPECT_GE(addrlen, sizeof(expected_addr));
        const auto& addr_in = *reinterpret_cast<const struct sockaddr_in*>(addr);
        EXPECT_EQ(addr_in.sin_family, expected_addr.sin_family);
        EXPECT_EQ(addr_in.sin_port, expected_addr.sin_port);
        EXPECT_EQ(addr_in.sin_addr.s_addr, expected_addr.sin_addr.s_addr);
        return 0;
      });
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, sendto(kSockFd, testing::_, testing::_, 0, testing::_, testing::_))
      .WillOnce([](testing::Unused, const void* buf, size_t len, testing::Unused,
                   const struct sockaddr* addr, socklen_t addrlen) {
        EXPECT_EQ(std::string(static_cast<const char*>(buf), len), TestPacketNumber(0));
        struct sockaddr_in expected_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(2021),
        };
        EXPECT_EQ(1, inet_pton(expected_addr.sin_family, "192.168.0.1", &expected_addr.sin_addr))
            << strerror(errno);
        EXPECT_GE(addrlen, sizeof(expected_addr));
        const auto& addr_in = *reinterpret_cast<const struct sockaddr_in*>(addr);
        EXPECT_EQ(addr_in.sin_family, expected_addr.sin_family);
        EXPECT_EQ(addr_in.sin_port, expected_addr.sin_port);
        EXPECT_EQ(addr_in.sin_addr.s_addr, expected_addr.sin_addr.s_addr);
        return len;
      });
  EXPECT_CALL(test, recvfrom(kSockFd, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("udp bind 0.0.0.0:2020 sendto 192.168.0.1:2021 recvfrom"), 0);
}

TEST(CommandLine, TcpBindConnectSendRecv) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, bind(kSockFd, testing::_, testing::_))
      .WillOnce([](testing::Unused, const struct sockaddr* addr, socklen_t addrlen) {
        const struct sockaddr_in expected_addr = {
            .sin_family = AF_INET,
            .sin_addr =
                {
                    .s_addr = htonl(INADDR_ANY),
                },
        };
        EXPECT_GE(addrlen, sizeof(expected_addr));
        const auto& addr_in = *reinterpret_cast<const struct sockaddr_in*>(addr);
        EXPECT_EQ(addr_in.sin_family, expected_addr.sin_family);
        EXPECT_EQ(addr_in.sin_port, expected_addr.sin_port);
        EXPECT_EQ(addr_in.sin_addr.s_addr, expected_addr.sin_addr.s_addr);
        return 0;
      });
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, connect(kSockFd, testing::_, testing::_))
      .WillOnce([](testing::Unused, const struct sockaddr* addr, socklen_t addrlen) {
        struct sockaddr_in expected_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(2021),
        };
        EXPECT_EQ(1, inet_pton(expected_addr.sin_family, "192.168.0.1", &expected_addr.sin_addr))
            << strerror(errno);
        EXPECT_GE(addrlen, sizeof(expected_addr));
        const auto& addr_in = *reinterpret_cast<const struct sockaddr_in*>(addr);
        EXPECT_EQ(addr_in.sin_family, expected_addr.sin_family);
        EXPECT_EQ(addr_in.sin_port, expected_addr.sin_port);
        EXPECT_EQ(addr_in.sin_addr.s_addr, expected_addr.sin_addr.s_addr);
        return 0;
      });
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, send(kSockFd, testing::_, testing::_, 0))
      .WillOnce([](testing::Unused, const void* buf, size_t len, testing::Unused) {
        EXPECT_EQ(std::string(static_cast<const char*>(buf), len), TestPacketNumber(0));
        return len;
      });
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getpeername(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, recv(kSockFd, testing::_, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, close(kSockFd)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp bind 0.0.0.0:0 connect 192.168.0.1:2021 send recv close"), 0);
}

TEST(CommandLine, TcpShutdown) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;

  // Missing argument.
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown"), -1);

  // Nonsense argument.
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown foobar"), -1);

  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, shutdown(kSockFd, SHUT_RD)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown rd"), 0);

  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, shutdown(kSockFd, SHUT_WR)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown wr"), 0);

  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, shutdown(kSockFd, SHUT_RDWR)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown rdwr"), 0);

  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, shutdown(kSockFd, SHUT_RDWR)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown wrrd"), 0);

  // Overlapping specifier. Probably should not allow this?
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, shutdown(kSockFd, SHUT_RDWR)).WillOnce(testing::Return(0));
  EXPECT_CALL(test, getsockname(kSockFd, testing::_, testing::_)).WillOnce(testing::Return(0));
  EXPECT_EQ(test.RunCommandLine("tcp shutdown wrd"), 0);
}

TEST(CommandLine, JoinDropMcast) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  EXPECT_CALL(test, socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)).WillOnce(testing::Return(kSockFd));
  struct ip_mreqn expected = {
      .imr_ifindex = 1,
  };
  ASSERT_EQ(1, inet_pton(AF_INET, "224.0.0.1", &expected.imr_multiaddr)) << strerror(errno);
  ASSERT_EQ(1, inet_pton(AF_INET, "192.168.0.1", &expected.imr_address)) << strerror(errno);
  EXPECT_CALL(test, setsockopt(kSockFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, testing::_, testing::_))
      .WillOnce([&expected](testing::Unused, testing::Unused, testing::Unused, const void* optval,
                            socklen_t optlen) {
        EXPECT_EQ(optlen, sizeof(expected));
        const auto& mreq = *reinterpret_cast<const struct ip_mreqn*>(optval);
        EXPECT_EQ(mreq.imr_multiaddr.s_addr, expected.imr_multiaddr.s_addr);
        EXPECT_EQ(mreq.imr_address.s_addr, expected.imr_address.s_addr);
        EXPECT_EQ(mreq.imr_ifindex, expected.imr_ifindex);
        return 0;
      });
  EXPECT_CALL(test, setsockopt(kSockFd, IPPROTO_IP, IP_DROP_MEMBERSHIP, testing::_, testing::_))
      .WillOnce([&expected](testing::Unused, testing::Unused, testing::Unused, const void* optval,
                            socklen_t optlen) {
        EXPECT_EQ(optlen, sizeof(expected));
        const auto& mreq = *reinterpret_cast<const struct ip_mreqn*>(optval);
        EXPECT_EQ(mreq.imr_multiaddr.s_addr, expected.imr_multiaddr.s_addr);
        EXPECT_EQ(mreq.imr_address.s_addr, expected.imr_address.s_addr);
        EXPECT_EQ(mreq.imr_ifindex, expected.imr_ifindex);
        return 0;
      });
  EXPECT_EQ(test.RunCommandLine("udp join4 224.0.0.1-192.168.0.1%1 drop4 224.0.0.1-192.168.0.1%1"),
            0);
}

TEST(CommandLine, JoinDropMcast6) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  EXPECT_CALL(test, socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)).WillOnce(testing::Return(kSockFd));
  constexpr char multiaddr[] = "ff02::1";
  constexpr unsigned interface = 1;
  EXPECT_CALL(test, setsockopt(kSockFd, IPPROTO_IPV6, IPV6_JOIN_GROUP, testing::_, testing::_))
      .WillOnce([multiaddr, interface](testing::Unused, testing::Unused, testing::Unused,
                                       const void* optval, socklen_t optlen) {
        EXPECT_EQ(optlen, sizeof(ipv6_mreq));
        const auto& mreq = *reinterpret_cast<const struct ipv6_mreq*>(optval);
        char buf[INET6_ADDRSTRLEN];
        EXPECT_STREQ(inet_ntop(AF_INET6, &mreq.ipv6mr_multiaddr, buf, sizeof(buf)), multiaddr);
        EXPECT_EQ(mreq.ipv6mr_interface, interface);
        return 0;
      });
  EXPECT_CALL(test, setsockopt(kSockFd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, testing::_, testing::_))
      .WillOnce([multiaddr, interface](testing::Unused, testing::Unused, testing::Unused,
                                       const void* optval, socklen_t optlen) {
        EXPECT_EQ(optlen, sizeof(ipv6_mreq));
        const auto& mreq = *reinterpret_cast<const struct ipv6_mreq*>(optval);
        char buf[INET6_ADDRSTRLEN];
        EXPECT_STREQ(inet_ntop(AF_INET6, &mreq.ipv6mr_multiaddr, buf, sizeof(buf)), multiaddr);
        EXPECT_EQ(mreq.ipv6mr_interface, interface);
        return 0;
      });
  std::stringstream o;
  o << "udp join6 " << multiaddr << '-' << interface << " drop6 " << multiaddr << '-' << interface;
  EXPECT_EQ(test.RunCommandLine(o.str()), 0) << o.str();
}

struct SockOptParam {
  SockOptParam(std::string name, std::string arg, int level, int optname,
               std::vector<uint8_t> optval)
      : name(std::move(name)),
        arg(std::move(arg)),
        level(level),
        optname(optname),
        optval(std::move(optval)){};

  std::string name;
  std::string arg;
  int level;
  int optname;
  std::vector<uint8_t> optval;
};

SockOptParam MakeSockOptParam(std::string name, std::string arg, int level, int optname,
                              int optval) {
  const auto* start = reinterpret_cast<const uint8_t*>(&optval);
  const auto* end = start + sizeof(optval);
  std::vector<uint8_t> buf(start, end);
  return SockOptParam(std::move(name), std::move(arg), level, optname, buf);
}

SockOptParam MakeSockOptParam(std::string name, std::string arg, int level, int optname,
                              const char* optval) {
  const auto* start = reinterpret_cast<const uint8_t*>(optval);
  const auto* end = start + strlen(optval);
  std::vector<uint8_t> buf(start, end);
  return SockOptParam(std::move(name), std::move(arg), level, optname, buf);
}

class SockOptTest : public testing::TestWithParam<SockOptParam> {};

TEST_P(SockOptTest, SetGetParam) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  std::stringstream cmd;
  auto& param = GetParam();
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  EXPECT_CALL(test, setsockopt(kSockFd, param.level, param.optname, testing::_, testing::_))
      .WillOnce([&param](testing::Unused, testing::Unused, testing::Unused, const void* optval,
                         socklen_t optlen) {
        EXPECT_EQ(optlen, param.optval.size());
        EXPECT_EQ(memcmp(optval, param.optval.data(), param.optval.size()), 0);
        return 0;
      });
  EXPECT_CALL(test, getsockopt(kSockFd, param.level, param.optname, testing::_, testing::_))
      .WillOnce([&param](testing::Unused, testing::Unused, testing::Unused, void* optval,
                         socklen_t* optlen) {
        auto expected = param.optval;
        EXPECT_GE(*optlen, param.optval.size());
        memcpy(optval, param.optval.data(), param.optval.size());
        *optlen = static_cast<socklen_t>(param.optval.size());
        return 0;
      });
  cmd << "tcp set-" << param.name << " " << param.arg << " log-" << param.name;
  EXPECT_EQ(test.RunCommandLine(cmd.str()), 0) << cmd.str();
}

TEST(SockOptTestMulticastIf, SetGetParam) {
  testing::StrictMock<TestApi> test;
  testing::InSequence s;
  std::stringstream cmd;
  EXPECT_CALL(test, socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)).WillOnce(testing::Return(kSockFd));
  struct ip_mreqn expected;
  EXPECT_CALL(test, setsockopt(kSockFd, IPPROTO_IP, IP_MULTICAST_IF, testing::_, testing::_))
      .WillOnce([&expected](testing::Unused, testing::Unused, testing::Unused, const void* optval,
                            socklen_t optlen) {
        EXPECT_EQ(optlen, sizeof(expected));
        memcpy(&expected, optval, optlen);
        return 0;
      });
  EXPECT_CALL(test, getsockopt(kSockFd, IPPROTO_IP, IP_MULTICAST_IF, testing::_, testing::_))
      .WillOnce([&expected](testing::Unused, testing::Unused, testing::Unused, void* optval,
                            socklen_t* optlen) {
        EXPECT_EQ(*optlen, sizeof(struct in_addr));
        memcpy(optval, &expected.imr_address, *optlen);
        *optlen = sizeof(expected.imr_address);
        return 0;
      });
  cmd << "tcp set-mcast-if4 192.168.0.1%1 log-mcast-if4";
  EXPECT_EQ(test.RunCommandLine(cmd.str()), 0);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterizedSockOpt, SockOptTest,
    testing::Values(MakeSockOptParam("broadcast", "1", SOL_SOCKET, SO_BROADCAST, 1),
#ifdef SO_BINDTODEVICE
                    MakeSockOptParam("bindtodevice", "device", SOL_SOCKET, SO_BINDTODEVICE,
                                     "device"),
#endif
                    MakeSockOptParam("reuseaddr", "1", SOL_SOCKET, SO_REUSEADDR, 1),
                    MakeSockOptParam("reuseport", "1", SOL_SOCKET, SO_REUSEPORT, 1),
                    MakeSockOptParam("unicast-ttl", "20", IPPROTO_IP, IP_TTL, 20),
                    MakeSockOptParam("unicast-hops", "10", IPPROTO_IPV6, IPV6_UNICAST_HOPS, 10),
                    MakeSockOptParam("mcast-ttl", "10", IPPROTO_IP, IP_MULTICAST_TTL, 10),
                    MakeSockOptParam("mcast-loop4", "1", IPPROTO_IP, IP_MULTICAST_LOOP, 1),
                    MakeSockOptParam("mcast-hops", "15", IPPROTO_IPV6, IPV6_MULTICAST_HOPS, 15),
                    MakeSockOptParam("mcast-loop6", "1", IPPROTO_IPV6, IPV6_MULTICAST_LOOP, 1),
                    MakeSockOptParam("mcast-if6", "1", IPPROTO_IPV6, IPV6_MULTICAST_IF, 1),
                    MakeSockOptParam("ipv6-only", "1", IPPROTO_IPV6, IPV6_V6ONLY, 1)));
