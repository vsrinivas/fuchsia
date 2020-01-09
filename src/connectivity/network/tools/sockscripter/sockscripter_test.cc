// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sockscripter.h"

#include <iomanip>
#include <utility>

#include <gtest/gtest.h>

#include "addr.h"
#include "src/lib/fxl/strings/split_string.h"

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

struct DataBuffer {
  DataBuffer() = default;

  DataBuffer(const DataBuffer& other) : DataBuffer(other.data.get(), other.len) {}

  DataBuffer(DataBuffer&& other) : data(std::move(other.data)), len(other.len) {}

  DataBuffer(std::initializer_list<uint8_t> l) {
    data.reset(new uint8_t[l.size()]);
    len = l.size();
    auto* p = data.get();
    for (auto& v : l) {
      *p++ = v;
    }
  }

  explicit DataBuffer(const std::string& str) {
    data.reset(new uint8_t[str.length() + 1]);
    memcpy(data.get(), str.data(), str.length());
    data[str.length()] = 0;
    len = str.length();
  }

  DataBuffer(const void* src, size_t l) {
    if (src && l) {
      data.reset(new uint8_t[l]);
      memcpy(data.get(), src, l);
      len = l;
    } else {
      data.reset();
      len = 0;
    }
  }

  bool operator==(const DataBuffer& rhs) const {
    return len == rhs.len && memcmp(rhs.data.get(), data.get(), len) == 0;
  }

  bool operator!=(const DataBuffer& rhs) const { return !(rhs == *this); }

  friend std::ostream& operator<<(std::ostream& os, const DataBuffer& b) {
    os << "Buffer(" << b.len << ")";
    if (b.len != 0) {
      os << ": ";
    }
    auto* p = b.data.get();
    auto blen = b.len;
    std::ios_base::fmtflags f(os.flags());
    while (blen--) {
      os << std::hex << std::setfill('0') << std::setw(2) << (int)*p++ << " ";
    }
    os.flags(f);
    return os;
  }

  std::unique_ptr<uint8_t[]> data;
  size_t len = 0;
};

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
  TestApi() { Reset(); }

  struct SockOptStorage {
    int level;
    int optname;
    DataBuffer val;
  };

  struct SendStorage {
    DataBuffer data;
    SockAddrIn to;
  };

  static constexpr int kSockFd = 15;

  int socket(int domain, int type, int protocol) override {
    call_info.socket.domain = domain;
    call_info.socket.type = type;
    call_info.socket.protocol = protocol;
    return kSockFd;
  }

  int close(int fd) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.close++;
    return return_value;
  }

  int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.setsockopt.push_back(
        SockOptStorage{.level = level, .optname = optname, .val = DataBuffer(optval, optlen)});
    return return_value;
  }

  int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.getsockopt.push_back(SockOptStorage{.level = level, .optname = optname});
    for (const auto& set : call_info.setsockopt) {
      if (set.optname == optname && set.level == level) {
        if (*optlen >= set.val.len) {
          memcpy(optval, set.val.data.get(), set.val.len);
          *optlen = set.val.len;
          return return_value;
        } else {
          // some sockopts's reverse operation do not use the same struct (like IP_MULTICAST_IF)
          // the trick of reading it back to get pretty log lines won't work.
          break;
        }
      }
    }
    memset(optval, 0x00, *optlen);
    return return_value;
  }

  int bind(int fd, const struct sockaddr* addr, socklen_t len) override {
    EXPECT_EQ(fd, kSockFd);
    EXPECT_TRUE(call_info.bind.emplace_back().Set(addr, len));
    return return_value;
  }

  int connect(int fd, const struct sockaddr* addr, socklen_t len) override {
    EXPECT_EQ(fd, kSockFd);
    EXPECT_TRUE(call_info.connect.emplace_back().Set(addr, len));
    return return_value;
  }

  int accept(int fd, struct sockaddr* addr, socklen_t* len) override {
    EXPECT_EQ(fd, kSockFd);
    memset(addr, 0x00, *len);
    call_info.accept++;
    return kSockFd;
  }

  int listen(int fd, int backlog) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.listen++;
    return return_value;
  }

  ssize_t send(int fd, const void* buf, size_t len, int flags) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.send.push_back(SendStorage{.data = DataBuffer(buf, len)});
    if (succeed_data_calls) {
      return len;
    } else {
      return -1;
    }
  }

  ssize_t sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr,
                 socklen_t addrlen) override {
    EXPECT_EQ(fd, kSockFd);
    auto& storage = call_info.sendto.emplace_back(SendStorage{
        .data = DataBuffer(buf, buflen),
    });
    EXPECT_TRUE(storage.to.Set(addr, addrlen));
    if (succeed_data_calls) {
      return buflen;
    } else {
      return -1;
    }
  }

  ssize_t recv(int fd, void* buf, size_t len, int flags) override {
    EXPECT_EQ(fd, kSockFd);
    call_info.recv++;
    if (succeed_data_calls) {
      return len;
    } else {
      return -1;
    }
  }

  ssize_t recvfrom(int fd, void* buf, size_t buflen, int flags, struct sockaddr* addr,
                   socklen_t* addrlen) override {
    EXPECT_EQ(fd, kSockFd);
    memset(addr, 0x00, *addrlen);
    call_info.recvfrom++;
    if (succeed_data_calls) {
      return buflen;
    } else {
      return -1;
    }
  }

  int getsockname(int fd, struct sockaddr* addr, socklen_t* len) override {
    EXPECT_EQ(fd, kSockFd);
    memset(addr, 0x00, *len);
    return return_value;
  }

  int getpeername(int fd, struct sockaddr* addr, socklen_t* len) override {
    EXPECT_EQ(fd, kSockFd);
    memset(addr, 0x00, *len);
    return return_value;
  }

 public:
  void Reset() {
    memset(&call_info.socket, 0x00, sizeof(call_info.socket));
    call_info.close = 0;
    call_info.setsockopt.clear();
    call_info.getsockopt.clear();
    call_info.bind.clear();
    call_info.connect.clear();
    call_info.accept = 0;
    call_info.listen = 0;
    call_info.send.clear();
    call_info.sendto.clear();
    call_info.recv = 0;
    call_info.recvfrom = 0;
    return_value = 0;
  }

  int RunCommandLine(const std::string& line) {
    Reset();
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
        if (start && strlen(start)) {
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
    return scripter.Execute(args.size(), &args[0]);
  }

  struct {
    struct {
      int domain;
      int type;
      int protocol;
    } socket;
    int close;
    std::vector<SockOptStorage> setsockopt;
    // only fills level and optname, always just sets the incoming struct to all zeros.
    std::vector<SockOptStorage> getsockopt;
    std::vector<SockAddrIn> bind;
    std::vector<SockAddrIn> connect;
    int accept;
    int listen;
    std::vector<SendStorage> send;
    std::vector<SendStorage> sendto;
    int recv;
    int recvfrom;
  } call_info{};
  int return_value = 0;
  bool succeed_data_calls = true;
};

DataBuffer TestPacketNumber(int c) {
  std::stringstream ss;
  ss << "Packet number " << c << ".";
  return DataBuffer(ss.str());
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
  EXPECT_EQ(test.RunCommandLine("udp"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET);
  EXPECT_EQ(test.call_info.socket.type, SOCK_DGRAM);
  EXPECT_EQ(test.call_info.socket.protocol, 0);
  EXPECT_EQ(test.RunCommandLine("tcp"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET);
  EXPECT_EQ(test.call_info.socket.type, SOCK_STREAM);
  EXPECT_EQ(test.call_info.socket.protocol, 0);
  EXPECT_EQ(test.RunCommandLine("udp6"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET6);
  EXPECT_EQ(test.call_info.socket.type, SOCK_DGRAM);
  EXPECT_EQ(test.call_info.socket.protocol, 0);
  EXPECT_EQ(test.RunCommandLine("tcp6"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET6);
  EXPECT_EQ(test.call_info.socket.type, SOCK_STREAM);
  EXPECT_EQ(test.call_info.socket.protocol, 0);
  EXPECT_EQ(test.RunCommandLine("raw 1"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET);
  EXPECT_EQ(test.call_info.socket.type, SOCK_RAW);
  EXPECT_EQ(test.call_info.socket.protocol, 1);
  EXPECT_EQ(test.RunCommandLine("raw6 2"), 0);
  EXPECT_EQ(test.call_info.socket.domain, AF_INET6);
  EXPECT_EQ(test.call_info.socket.type, SOCK_RAW);
  EXPECT_EQ(test.call_info.socket.protocol, 2);
  EXPECT_NE(test.RunCommandLine("raw bind"), 0);
}

TEST(CommandLine, UdpBindSendToRecvFrom) {
  TestApi test;
  EXPECT_EQ(test.RunCommandLine("udp bind any:2020 sendto 192.168.0.1:2021 recvfrom"), 0);
  ASSERT_EQ(test.call_info.bind.size(), 1ul);
  ASSERT_EQ(test.call_info.sendto.size(), 1ul);
  EXPECT_EQ(test.call_info.bind[0].Name(), "ANY:2020");
  EXPECT_EQ(test.call_info.sendto[0].to.Name(), "192.168.0.1:2021");
  EXPECT_EQ(test.call_info.sendto[0].data, TestPacketNumber(0));
  EXPECT_EQ(test.call_info.recvfrom, 1);
}

TEST(CommandLine, TcpBindConnectSendRecv) {
  TestApi test;
  EXPECT_EQ(test.RunCommandLine("tcp bind any:0 connect 192.168.0.1:2021 send recv close"), 0);
  ASSERT_EQ(test.call_info.bind.size(), 1ul);
  ASSERT_EQ(test.call_info.send.size(), 1ul);
  EXPECT_EQ(test.call_info.bind[0].Name(), "ANY:0");
  EXPECT_EQ(test.call_info.connect[0].Name(), "192.168.0.1:2021");
  EXPECT_EQ(test.call_info.send[0].data, TestPacketNumber(0));
  EXPECT_EQ(test.call_info.recv, 1);
  EXPECT_EQ(test.call_info.close, 1);
}

TEST(CommandLine, JoinDropMcast) {
  TestApi test;
  EXPECT_EQ(test.RunCommandLine("udp join4 224.0.0.1-192.168.0.1%1 drop4 224.0.0.1-192.168.0.1%1"),
            0);
  ASSERT_EQ(test.call_info.setsockopt.size(), 2ul);

  InAddr mcast;
  ASSERT_TRUE(mcast.Set("224.0.0.1"));
  LocalIfAddr ifaddr;
  ASSERT_TRUE(ifaddr.Set("192.168.0.1%1"));
  struct ip_mreqn mreq = {.imr_multiaddr = mcast.GetAddr4(),
                          .imr_address = ifaddr.GetAddr4(),
                          .imr_ifindex = ifaddr.GetId()};

  EXPECT_EQ(test.call_info.setsockopt[0].level, IPPROTO_IP);
  EXPECT_EQ(test.call_info.setsockopt[0].optname, IP_ADD_MEMBERSHIP);
  EXPECT_EQ(test.call_info.setsockopt[0].val, DataBuffer(&mreq, sizeof(mreq)));
  EXPECT_EQ(test.call_info.setsockopt[1].level, IPPROTO_IP);
  EXPECT_EQ(test.call_info.setsockopt[1].optname, IP_DROP_MEMBERSHIP);
  EXPECT_EQ(test.call_info.setsockopt[1].val, DataBuffer(&mreq, sizeof(mreq)));
}

TEST(CommandLine, JoinDropMcast6) {
  TestApi test;
  EXPECT_EQ(test.RunCommandLine("udp join6 ff02:0::01-ff02:0::02%1 drop6 ff02:0::01-ff02:0::02%1"),
            0);
  ASSERT_EQ(test.call_info.setsockopt.size(), 2ul);

  InAddr mcast;
  ASSERT_TRUE(mcast.Set("ff02:0::01"));
  LocalIfAddr ifaddr;
  ASSERT_TRUE(ifaddr.Set("ff02:0::02%1"));
  struct ipv6_mreq mreq = {.ipv6mr_multiaddr = mcast.GetAddr6(),
                           .ipv6mr_interface = static_cast<unsigned int>(ifaddr.GetId())};

  EXPECT_EQ(test.call_info.setsockopt[0].level, IPPROTO_IPV6);
  EXPECT_EQ(test.call_info.setsockopt[0].optname, IPV6_JOIN_GROUP);
  EXPECT_EQ(test.call_info.setsockopt[0].val, DataBuffer(&mreq, sizeof(mreq)));
  EXPECT_EQ(test.call_info.setsockopt[1].level, IPPROTO_IPV6);
  EXPECT_EQ(test.call_info.setsockopt[1].optname, IPV6_LEAVE_GROUP);
  EXPECT_EQ(test.call_info.setsockopt[1].val, DataBuffer(&mreq, sizeof(mreq)));
}

struct SockOptParam {
  SockOptParam(std::string opt, std::string strValue, int level, int optname, DataBuffer value)
      : opt(std::move(opt)),
        str_value(std::move(strValue)),
        level(level),
        optname(optname),
        value(std::move(value)) {}

  SockOptParam(std::string opt, std::string strValue, int level, int optname, int int_value)
      : opt(std::move(opt)),
        str_value(std::move(strValue)),
        level(level),
        optname(optname),
        value(&int_value, sizeof(int_value)) {}

  friend std::ostream& operator<<(std::ostream& os, const SockOptParam& param) {
    return os << "SockOptParam(" << param.opt << ")";
  }

  std::string opt;
  std::string str_value;
  int level;
  int optname;
  DataBuffer value;
};

class SockOptTest : public testing::TestWithParam<SockOptParam> {};

TEST_P(SockOptTest, SetGetParams) {
  TestApi test;
  std::stringstream cmd;
  auto& param = GetParam();
  cmd << "tcp set-" << param.opt << " " << param.str_value << " log-" << param.opt;
  ASSERT_EQ(test.RunCommandLine(cmd.str()), 0);
  ASSERT_EQ(test.call_info.setsockopt.size(), 1ul);
  EXPECT_EQ(test.call_info.setsockopt[0].level, param.level);
  EXPECT_EQ(test.call_info.setsockopt[0].optname, param.optname);
  EXPECT_EQ(test.call_info.setsockopt[0].val, param.value);
  ASSERT_EQ(test.call_info.getsockopt.size(), 1ul);
  EXPECT_EQ(test.call_info.getsockopt[0].level, param.level);
  EXPECT_EQ(test.call_info.getsockopt[0].optname, param.optname);
}

const struct {
  uint32_t a;
  uint8_t addr[4];
  int idx;
} kMulticastMreq = {.a = 0, .addr = {192, 168, 0, 1}, .idx = 1};

INSTANTIATE_TEST_SUITE_P(
    ParameterizedSockOpt, SockOptTest,
    testing::Values(SockOptParam("broadcast", "1", SOL_SOCKET, SO_BROADCAST, 1),
#ifdef SO_BINDTODEVICE
                    SockOptParam("bindtodevice", "device", SOL_SOCKET, SO_BINDTODEVICE,
                                 DataBuffer("device")),
#endif
                    SockOptParam("reuseaddr", "1", SOL_SOCKET, SO_REUSEADDR, 1),
                    SockOptParam("reuseport", "1", SOL_SOCKET, SO_REUSEPORT, 1),
                    SockOptParam("unicast-ttl", "20", IPPROTO_IP, IP_TTL, 20),
                    SockOptParam("unicast-hops", "10", IPPROTO_IPV6, IPV6_UNICAST_HOPS, 10),
                    SockOptParam("mcast-ttl", "10", IPPROTO_IP, IP_MULTICAST_TTL, 10),
                    SockOptParam("mcast-loop4", "1", IPPROTO_IP, IP_MULTICAST_LOOP, 1),
                    SockOptParam("mcast-hops", "15", IPPROTO_IPV6, IPV6_MULTICAST_HOPS, 15),
                    SockOptParam("mcast-loop6", "1", IPPROTO_IPV6, IPV6_MULTICAST_LOOP, 1),
                    SockOptParam("mcast-if4", "192.168.0.1%1", IPPROTO_IP, IP_MULTICAST_IF,
                                 DataBuffer(&kMulticastMreq, sizeof(kMulticastMreq))),
                    SockOptParam("mcast-if6", "ff02:0::01%1", IPPROTO_IPV6, IPV6_MULTICAST_IF, 1),
                    SockOptParam("ipv6-only", "1", IPPROTO_IPV6, IPV6_V6ONLY, 1)));
