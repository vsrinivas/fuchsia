// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure fdio can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <array>
#include <cstdlib>
#include <future>
#include <latch>
#include <thread>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "util.h"

namespace {

TEST(LocalhostTest, DatagramSocketIgnoresMsgWaitAll) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(recvfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  ASSERT_EQ(recvfrom(recvfd.get(), nullptr, 0, MSG_WAITALL, nullptr, nullptr), -1);
  ASSERT_EQ(errno, EAGAIN) << strerror(errno);

  ASSERT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, DatagramSocketSendMsgNameLenTooBig) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
  };

  struct msghdr msg = {
      .msg_name = &addr,
      .msg_namelen = sizeof(sockaddr_storage) + 1,
  };

  ASSERT_EQ(sendmsg(fd.get(), &msg, 0), -1);
  ASSERT_EQ(errno, EINVAL) << strerror(errno);

  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

#if !defined(__Fuchsia__)
bool IsRoot() {
  uid_t ruid, euid, suid;
  EXPECT_EQ(getresuid(&ruid, &euid, &suid), 0) << strerror(errno);
  if (ruid != 0 || euid != 0 || suid != 0) {
    return false;
  }
  gid_t rgid, egid, sgid;
  EXPECT_EQ(getresgid(&rgid, &egid, &sgid), 0) << strerror(errno);
  if (rgid != 0 || egid != 0 || sgid != 0) {
    return false;
  }
  return true;
}
#endif

TEST(LocalhostTest, BindToDevice) {
#if !defined(__Fuchsia__)
  if (!IsRoot()) {
    GTEST_SKIP() << "This test requires root";
  }
#endif

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  {
    // The default is that a socket is not bound to a device.
    char get_dev[IFNAMSIZ] = {};
    socklen_t get_dev_length = sizeof(get_dev);
    EXPECT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, get_dev, &get_dev_length), 0)
        << strerror(errno);
    EXPECT_EQ(get_dev_length, socklen_t(0));
    EXPECT_STREQ(get_dev, "");
  }

  const char set_dev[IFNAMSIZ] = "lo\0blahblah";

  // Bind to "lo" with null termination should work even if the size is too big.
  ASSERT_EQ(setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, set_dev, sizeof(set_dev)), 0)
      << strerror(errno);

  const char set_dev_unknown[] = "loblahblahblah";
  // Bind to "lo" without null termination but with accurate length should work.
  EXPECT_EQ(setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, set_dev_unknown, 2), 0)
      << strerror(errno);

  // Bind to unknown name should fail.
  EXPECT_EQ(
      setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, "loblahblahblah", sizeof(set_dev_unknown)),
      -1);
  EXPECT_EQ(errno, ENODEV) << strerror(errno);

  {
    // Reading it back should work.
    char get_dev[IFNAMSIZ] = {};
    socklen_t get_dev_length = sizeof(get_dev);
    EXPECT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, get_dev, &get_dev_length), 0)
        << strerror(errno);
    EXPECT_EQ(get_dev_length, strlen(set_dev) + 1);
    EXPECT_STREQ(get_dev, set_dev);
  }

  {
    // Reading it back without enough space in the buffer should fail.
    char get_dev[] = "";
    socklen_t get_dev_length = sizeof(get_dev);
    EXPECT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, get_dev, &get_dev_length), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
    EXPECT_EQ(get_dev_length, sizeof(get_dev));
    EXPECT_STREQ(get_dev, "");
  }

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

// Raw sockets are typically used for implementing custom protocols. We intend to support custom
// protocols through structured FIDL APIs in the future, so this test ensures that raw sockets are
// disabled to prevent them from accidentally becoming load-bearing.
TEST(LocalhostTest, RawSocketsNotSupported) {
  // No raw INET sockets.
  ASSERT_EQ(socket(AF_INET, SOCK_RAW, 0), -1);
  ASSERT_EQ(errno, EPROTONOSUPPORT) << strerror(errno);

  // No packet sockets.
  ASSERT_EQ(socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)), -1);
  ASSERT_EQ(errno, EPERM) << strerror(errno);
}

TEST(LocalhostTest, IP_ADD_MEMBERSHIP_INADDR_ANY) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  ip_mreqn param = {
      .imr_address.s_addr = htonl(INADDR_ANY),
      .imr_ifindex = 1,
  };
  int n = inet_pton(AF_INET, "224.0.2.1", &param.imr_multiaddr.s_addr);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  ASSERT_EQ(setsockopt(s.get(), SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0)
      << strerror(errno);

  ASSERT_EQ(close(s.release()), 0) << strerror(errno);
}

struct SockOption {
  int level;
  int option;
};

constexpr int INET_ECN_MASK = 3;

std::string socketTypeToString(const int type) {
  switch (type) {
    case SOCK_DGRAM:
      return "Datagram";
    case SOCK_STREAM:
      return "Stream";
    default:
      return std::to_string(type);
  }
}

using SocketKind = std::tuple<int, int>;

std::string socketKindToString(const ::testing::TestParamInfo<SocketKind>& info) {
  auto const& [domain, type] = info.param;

  std::string domain_str;
  switch (domain) {
    case AF_INET:
      domain_str = "IPv4";
      break;
    case AF_INET6:
      domain_str = "IPv6";
      break;
    default:
      domain_str = std::to_string(domain);
      break;
  }
  return domain_str + "_" + socketTypeToString(type);
}

// Share common functions for SocketKind based tests.
class SocketKindTest : public ::testing::TestWithParam<SocketKind> {
 protected:
  fbl::unique_fd NewSocket() const {
    auto const& [domain, type] = GetParam();
    return fbl::unique_fd(socket(domain, type, 0));
  }
};

constexpr int kSockOptOn = 1;
constexpr int kSockOptOff = 0;

class SocketOptsTest : public SocketKindTest {
 protected:
  bool IsTCP() const { return std::get<1>(GetParam()) == SOCK_STREAM; }

  bool IsIPv6() const { return std::get<0>(GetParam()) == AF_INET6; }

  SockOption GetTOSOption() {
    if (IsIPv6()) {
      return {
          .level = IPPROTO_IPV6,
          .option = IPV6_TCLASS,
      };
    }
    return {
        .level = IPPROTO_IP,
        .option = IP_TOS,
    };
  }

  SockOption GetMcastLoopOption() {
    if (IsIPv6()) {
      return {
          .level = IPPROTO_IPV6,
          .option = IPV6_MULTICAST_LOOP,
      };
    }
    return {
        .level = IPPROTO_IP,
        .option = IP_MULTICAST_LOOP,
    };
  }

  SockOption GetMcastTTLOption() {
    if (IsIPv6()) {
      return {
          .level = IPPROTO_IPV6,
          .option = IPV6_MULTICAST_HOPS,
      };
    }
    return {
        .level = IPPROTO_IP,
        .option = IP_MULTICAST_TTL,
    };
  }

  SockOption GetMcastIfOption() {
    if (IsIPv6()) {
      return {
          .level = IPPROTO_IPV6,
          .option = IPV6_MULTICAST_IF,
      };
    }
    return {
        .level = IPPROTO_IP,
        .option = IP_MULTICAST_IF,
    };
  }

  SockOption GetRecvTOSOption() {
    if (IsIPv6()) {
      return {
          .level = IPPROTO_IPV6,
          .option = IPV6_RECVTCLASS,
      };
    }
    return {
        .level = IPPROTO_IP,
        .option = IP_RECVTOS,
    };
  }

  SockOption GetNoChecksum() {
    return {
        .level = SOL_SOCKET,
        .option = SO_NO_CHECK,
    };
  }
};

// The SocketOptsTest is adapted from gvisor/tests/syscalls/linux/socket_ip_unbound.cc
TEST_P(SocketOptsTest, TtlDefault) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get = -1;
  socklen_t get_sz = sizeof(get);
  constexpr int kDefaultTTL = 64;
  EXPECT_EQ(getsockopt(s.get(), IPPROTO_IP, IP_TTL, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get, kDefaultTTL);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get1 = -1;
  socklen_t get1_sz = sizeof(get1);
  EXPECT_EQ(getsockopt(s.get(), IPPROTO_IP, IP_TTL, &get1, &get1_sz), 0) << strerror(errno);
  EXPECT_EQ(get1_sz, sizeof(get1));

  int set = 100;
  if (set == get1) {
    set += 1;
  }
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), 0) << strerror(errno);

  int get2 = -1;
  socklen_t get2_sz = sizeof(get2);
  EXPECT_EQ(getsockopt(s.get(), IPPROTO_IP, IP_TTL, &get2, &get2_sz), 0) << strerror(errno);
  EXPECT_EQ(get2_sz, sizeof(get2));
  EXPECT_EQ(get2, set);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, ResetTtlToDefault) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get1 = -1;
  socklen_t get1_sz = sizeof(get1);
  EXPECT_EQ(getsockopt(s.get(), IPPROTO_IP, IP_TTL, &get1, &get1_sz), 0) << strerror(errno);
  EXPECT_EQ(get1_sz, sizeof(get1));

  int set1 = 100;
  if (set1 == get1) {
    set1 += 1;
  }
  socklen_t set1_sz = sizeof(set1);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set1, set1_sz), 0) << strerror(errno);

  int set2 = -1;
  socklen_t set2_sz = sizeof(set2);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set2, set2_sz), 0) << strerror(errno);

  int get2 = -1;
  socklen_t get2_sz = sizeof(get2);
  EXPECT_EQ(getsockopt(s.get(), IPPROTO_IP, IP_TTL, &get2, &get2_sz), 0) << strerror(errno);
  EXPECT_EQ(get2_sz, sizeof(get2));
  EXPECT_EQ(get2, get1);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, ZeroTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0;
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidLargeTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 256;
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidNegativeTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = -2;
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, TOSDefault) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  SockOption t = GetTOSOption();
  int get = -1;
  socklen_t get_sz = sizeof(get);
  constexpr int kDefaultTOS = 0;
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, kDefaultTOS);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0xC0;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);

  int get = -1;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, set);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, NullTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  socklen_t set_sz = sizeof(int);
  SockOption t = GetTOSOption();
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, nullptr, set_sz), 0) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, nullptr, set_sz), -1);
    EXPECT_EQ(errno, EFAULT) << strerror(errno);
  }
  socklen_t get_sz = sizeof(int);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, nullptr, &get_sz), -1);
  EXPECT_EQ(errno, EFAULT) << strerror(errno);
  int get = -1;
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, nullptr), -1);
  EXPECT_EQ(errno, EFAULT) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, ZeroTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  int get = -1;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, set);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidLargeTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  // Test with exceeding the byte space.
  int set = 256;
  constexpr int kDefaultTOS = 0;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  }
  int get = -1;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, kDefaultTOS);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, CheckSkipECN) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0xFF;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  int expect = static_cast<uint8_t>(set);
  if (IsTCP()
#if !defined(__Fuchsia__)
      // gvisor-netstack`s implemention of setsockopt(..IPV6_TCLASS..)
      // clears the ECN bits from the TCLASS value. This keeps gvisor
      // in parity with the Linux test-hosts that run a custom kernel.
      // But that is not the behavior of vanilla Linux kernels.
      // This #if can be removed when we migrate away from gvisor-netstack.
      && !IsIPv6()
#endif
  ) {
    expect &= ~INET_ECN_MASK;
  }
  int get = -1;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, expect);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, ZeroTOSOptionSize) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0xC0;
  socklen_t set_sz = 0;
  SockOption t = GetTOSOption();
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  }
  int get = -1;
  socklen_t get_sz = 0;
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, 0u);
  EXPECT_EQ(get, -1);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SmallTOSOptionSize) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 0xC0;
  constexpr int kDefaultTOS = 0;
  SockOption t = GetTOSOption();
  for (socklen_t i = 1; i < sizeof(int); i++) {
    int expect_tos;
    socklen_t expect_sz;
    if (IsIPv6()) {
      EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, i), -1);
      EXPECT_EQ(errno, EINVAL) << strerror(errno);
      expect_tos = kDefaultTOS;
      expect_sz = i;
    } else {
      EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, i), 0) << strerror(errno);
      expect_tos = set;
      expect_sz = sizeof(uint8_t);
    }
    uint get = -1;
    socklen_t get_sz = i;
    EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
    EXPECT_EQ(get_sz, expect_sz);
    // Account for partial copies by getsockopt, retrieve the lower
    // bits specified by get_sz, while comparing against expect_tos.
    EXPECT_EQ(get & ~(~0 << (get_sz * 8)), static_cast<uint>(expect_tos));
  }
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, LargeTOSOptionSize) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  char buffer[100];
  int* set = reinterpret_cast<int*>(buffer);
  // Point to a larger buffer so that the setsockopt does not overrun.
  *set = 0xC0;
  SockOption t = GetTOSOption();
  for (socklen_t i = sizeof(int); i < 10; i++) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, set, i), 0) << strerror(errno);
    int get = -1;
    socklen_t get_sz = i;
    // We expect the system call handler to only copy atmost sizeof(int) bytes
    // as asserted by the check below. Hence, we do not expect the copy to
    // overflow in getsockopt.
    EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
    EXPECT_EQ(get_sz, sizeof(int));
    EXPECT_EQ(get, *set);
  }
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, NegativeTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = -1;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  int expect;
  if (IsIPv6()) {
    // On IPv6 TCLASS, setting -1 has the effect of resetting the
    // TrafficClass.
    expect = 0;
  } else {
    expect = static_cast<uint8_t>(set);
    if (IsTCP()) {
      expect &= ~INET_ECN_MASK;
    }
  }
  int get = -1;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, expect);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidNegativeTOS) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = -2;
  socklen_t set_sz = sizeof(set);
  SockOption t = GetTOSOption();
  int expect;
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
    expect = 0;
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
    expect = static_cast<uint8_t>(set);
    if (IsTCP()) {
      expect &= ~INET_ECN_MASK;
    }
  }
  int get = 0;
  socklen_t get_sz = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_sz), 0) << strerror(errno);
  EXPECT_EQ(get_sz, sizeof(get));
  EXPECT_EQ(get, expect);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, MulticastLoopDefault) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  SockOption t = GetMcastLoopOption();
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOn);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetMulticastLoop) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  SockOption t = GetMcastLoopOption();
  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOff, sizeof(kSockOptOff)), 0)
      << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOn, sizeof(kSockOptOn)), 0)
      << strerror(errno);

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOn);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetMulticastLoopChar) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr char kSockOptOnChar = kSockOptOn;
  constexpr char kSockOptOffChar = kSockOptOff;

  SockOption t = GetMcastLoopOption();
  int want;
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOffChar, sizeof(kSockOptOffChar)),
              -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
    want = kSockOptOnChar;
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOffChar, sizeof(kSockOptOffChar)), 0)
        << strerror(errno);
    want = kSockOptOffChar;
  }

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, want);

  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOnChar, sizeof(kSockOptOnChar)), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOnChar, sizeof(kSockOptOnChar)), 0)
        << strerror(errno);
  }

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOn);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, MulticastTTLDefault) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, 1);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLMin) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kMin = 0;
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kMin, sizeof(kMin)), 0) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kMin);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLMax) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kMax = 255;
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kMax, sizeof(kMax)), 0) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kMax);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLNegativeOne) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kArbitrary = 6;
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kArbitrary, sizeof(kArbitrary)), 0)
      << strerror(errno);

  constexpr int kNegOne = -1;
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kNegOne, sizeof(kNegOne)), 0)
      << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, 1);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLBelowMin) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kBelowMin = -2;
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kBelowMin, sizeof(kBelowMin)), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLAboveMax) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kAboveMax = 256;
  SockOption t = GetMcastTTLOption();
  EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kAboveMax, sizeof(kAboveMax)), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastTTLChar) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr char kArbitrary = 6;
  SockOption t = GetMcastTTLOption();
  int want;
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kArbitrary, sizeof(kArbitrary)), -1);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
    want = 1;
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kArbitrary, sizeof(kArbitrary)), 0)
        << strerror(errno);
    want = kArbitrary;
  }

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, want);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastIf_ifindex) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr int kOne = 1;
  SockOption t = GetMcastIfOption();
  if (IsIPv6()) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kOne, sizeof(kOne)), 0) << strerror(errno);

    int param_out;
    socklen_t len = sizeof(param_out);
    ASSERT_EQ(getsockopt(s.get(), t.level, t.option, &param_out, &len), 0) << strerror(errno);
    ASSERT_EQ(len, sizeof(param_out));

    ASSERT_EQ(param_out, kOne);
  } else {
    ip_mreqn param_in = {
        .imr_ifindex = kOne,
    };
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &param_in, sizeof(param_in)), 0)
        << strerror(errno);

    in_addr param_out;
    socklen_t len = sizeof(param_out);
    ASSERT_EQ(getsockopt(s.get(), t.level, t.option, &param_out, &len), 0) << strerror(errno);
    ASSERT_EQ(len, sizeof(param_out));

    ASSERT_EQ(param_out.s_addr, INADDR_ANY);
  }

  ASSERT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastIf_ifaddr) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip multicast tests on TCP socket";
  }
  if (IsIPv6()) {
    GTEST_SKIP() << "V6 sockets don't support setting IP_MULTICAST_IF by addr";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  SockOption t = GetMcastIfOption();
  ip_mreqn param_in = {
      .imr_address.s_addr = htonl(INADDR_LOOPBACK),
  };
  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  in_addr param_out;
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s.get(), t.level, t.option, &param_out, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  ASSERT_EQ(param_out.s_addr, param_in.imr_address.s_addr);

  ASSERT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, ReceiveTOSDefault) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip receive TOS tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  SockOption t = GetRecvTOSOption();
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetReceiveTOS) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip receive TOS tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  SockOption t = GetRecvTOSOption();
  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOn, sizeof(kSockOptOn)), 0)
      << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOn);

  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOff, sizeof(kSockOptOff)), 0)
      << strerror(errno);

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

// Tests that a two byte RECVTOS/RECVTCLASS optval is acceptable.
TEST_P(SocketOptsTest, SetReceiveTOSShort) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip receive TOS tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr char kSockOptOn2Byte[] = {kSockOptOn, 0};
  constexpr char kSockOptOff2Byte[] = {kSockOptOff, 0};

  SockOption t = GetRecvTOSOption();
  if (IsIPv6()) {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOn2Byte, sizeof(kSockOptOn2Byte)), -1)
        << strerror(errno);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOn2Byte, sizeof(kSockOptOn2Byte)), 0)
        << strerror(errno);
  }

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  if (IsIPv6()) {
    EXPECT_EQ(get, kSockOptOff);
  } else {
    EXPECT_EQ(get, kSockOptOn);
  }

  if (IsIPv6()) {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOff2Byte, sizeof(kSockOptOff2Byte)),
              -1)
        << strerror(errno);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOff2Byte, sizeof(kSockOptOff2Byte)),
              0)
        << strerror(errno);
  }

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

// Tests that a one byte sized optval is acceptable for RECVTOS and not for
// RECVTCLASS.
TEST_P(SocketOptsTest, SetReceiveTOSChar) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip receive TOS tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  constexpr char kSockOptOnChar = kSockOptOn;
  constexpr char kSockOptOffChar = kSockOptOff;

  SockOption t = GetRecvTOSOption();
  if (IsIPv6()) {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOnChar, sizeof(kSockOptOnChar)), -1)
        << strerror(errno);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOnChar, sizeof(kSockOptOnChar)), 0)
        << strerror(errno);
  }

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  if (IsIPv6()) {
    EXPECT_EQ(get, kSockOptOff);
  } else {
    EXPECT_EQ(get, kSockOptOn);
  }

  if (IsIPv6()) {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOffChar, sizeof(kSockOptOffChar)), -1)
        << strerror(errno);
    EXPECT_EQ(errno, EINVAL) << strerror(errno);
  } else {
    ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOffChar, sizeof(kSockOptOffChar)), 0)
        << strerror(errno);
  }

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, NoChecksumDefault) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip NoChecksum tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  SockOption t = GetNoChecksum();
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetNoChecksum) {
  if (IsTCP()) {
    GTEST_SKIP() << "Skip NoChecksum tests on TCP socket";
  }

  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  SockOption t = GetNoChecksum();
  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOn, sizeof(kSockOptOn)), 0)
      << strerror(errno);

  int get = -1;
  socklen_t get_len = sizeof(get);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOn);

  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOff, sizeof(kSockOptOff)), 0)
      << strerror(errno);

  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, &get_len), 0) << strerror(errno);
  EXPECT_EQ(get_len, sizeof(get));
  EXPECT_EQ(get, kSockOptOff);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(LocalhostTest, SocketOptsTest,
                         ::testing::Combine(::testing::Values(AF_INET, AF_INET6),
                                            ::testing::Values(SOCK_DGRAM, SOCK_STREAM)),
                         socketKindToString);

using typeMulticast = std::tuple<int, bool>;

std::string typeMulticastToString(const ::testing::TestParamInfo<typeMulticast>& info) {
  auto const& [type, multicast] = info.param;
  std::string addr;
  if (multicast) {
    addr = "Multicast";
  } else {
    addr = "Loopback";
  }
  return socketTypeToString(type) + addr;
}

class ReuseTest : public ::testing::TestWithParam<typeMulticast> {};

TEST_P(ReuseTest, AllowsAddressReuse) {
  const int on = true;

  auto const& [type, multicast] = GetParam();

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  if (multicast) {
    int n = inet_pton(addr.sin_family, "224.0.2.1", &addr.sin_addr);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
  }

  fbl::unique_fd s1;
  ASSERT_TRUE(s1 = fbl::unique_fd(socket(AF_INET, type, 0))) << strerror(errno);

// TODO(gvisor.dev/issue/3839): Remove this.
#if defined(__Fuchsia__)
  // Must outlive the block below.
  fbl::unique_fd s;
  if (type != SOCK_DGRAM && multicast) {
    ASSERT_EQ(bind(s1.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);
    ASSERT_EQ(errno, EADDRNOTAVAIL) << strerror(errno);
    ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);
    ip_mreqn param = {
        .imr_multiaddr = addr.sin_addr,
        .imr_address.s_addr = htonl(INADDR_ANY),
        .imr_ifindex = 1,
    };
    ASSERT_EQ(setsockopt(s.get(), SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0)
        << strerror(errno);
  }
#endif

  ASSERT_EQ(setsockopt(s1.get(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);
  ASSERT_EQ(bind(s1.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(s1.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  fbl::unique_fd s2;
  ASSERT_TRUE(s2 = fbl::unique_fd(socket(AF_INET, type, 0))) << strerror(errno);
  ASSERT_EQ(setsockopt(s2.get(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);
  ASSERT_EQ(bind(s2.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(LocalhostTest, ReuseTest,
                         ::testing::Combine(::testing::Values(SOCK_DGRAM, SOCK_STREAM),
                                            ::testing::Values(false, true)),
                         typeMulticastToString);

TEST(LocalhostTest, Accept) {
  fbl::unique_fd serverfd;
  ASSERT_TRUE(serverfd = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in6 serveraddr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };
  socklen_t serveraddrlen = sizeof(serveraddr);
  ASSERT_EQ(bind(serverfd.get(), (sockaddr*)&serveraddr, serveraddrlen), 0) << strerror(errno);
  ASSERT_EQ(getsockname(serverfd.get(), (sockaddr*)&serveraddr, &serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(serveraddrlen, sizeof(serveraddr));
  ASSERT_EQ(listen(serverfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), (sockaddr*)&serveraddr, serveraddrlen), 0) << strerror(errno);

  struct sockaddr_in connaddr;
  socklen_t connaddrlen = sizeof(connaddr);
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(serverfd.get(), (sockaddr*)&connaddr, &connaddrlen)))
      << strerror(errno);
  ASSERT_GT(connaddrlen, sizeof(connaddr));
}

TEST(LocalhostTest, AcceptAfterReset) {
  fbl::unique_fd server;
  ASSERT_TRUE(server = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(bind(server.get(), (const sockaddr*)&addr, addrlen), 0) << strerror(errno);
  ASSERT_EQ(getsockname(server.get(), (sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(listen(server.get(), 1), 0) << strerror(errno);

  {
    fbl::unique_fd client;
    ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client.get(), (const sockaddr*)&addr, addrlen), 0) << strerror(errno);
    struct linger opt = {
        .l_onoff = 1,
        .l_linger = 0,
    };
    ASSERT_EQ(setsockopt(client.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
        << strerror(errno);
    ASSERT_EQ(close(client.release()), 0) << strerror(errno);
  }

  memset(&addr, 0, sizeof(addr));

  fbl::unique_fd conn;
  // TODO(https://github.com/google/gvisor/issues/3780): Remove this.
#if defined(__Fuchsia__)
  ASSERT_TRUE(conn = fbl::unique_fd(accept(server.get(), nullptr, 0))) << strerror(errno);
  return;
#endif
  ASSERT_TRUE(conn = fbl::unique_fd(accept(server.get(), (sockaddr*)&addr, &addrlen)))
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(addr.sin6_family, AF_INET6);
  char buf[INET6_ADDRSTRLEN];
  ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&addr.sin6_addr))
      << inet_ntop(addr.sin6_family, &addr.sin6_addr, buf, sizeof(buf));
  ASSERT_NE(addr.sin6_port, 0);

  int err;
  socklen_t optlen = sizeof(err);
  ASSERT_EQ(getsockopt(conn.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
  ASSERT_EQ(err, ECONNRESET);
  ASSERT_EQ(optlen, sizeof(err));
}

TEST(LocalhostTest, ConnectAFMismatchINET) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_port = htons(1337),
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };
  EXPECT_EQ(connect(s.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);
  EXPECT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, ConnectAFMismatchINET6) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(1337),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  EXPECT_EQ(connect(s.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, ConnectTwice) {
  fbl::unique_fd client, listener;
  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(connect(client.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);

  ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

  // TODO(tamird): decide if we want to match Linux's behaviour.
  ASSERT_EQ(connect(client.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
#if defined(__linux__)
            0)
      << strerror(errno);
#else
            -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);
#endif

  ASSERT_EQ(close(listener.release()), 0) << strerror(errno);
  ASSERT_EQ(close(client.release()), 0) << strerror(errno);
}

void TestHangupDuringConnect(void (*hangup)(fbl::unique_fd*)) {
  fbl::unique_fd client, listener;
  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr_in = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  auto addr = reinterpret_cast<struct sockaddr*>(&addr_in);
  socklen_t addr_len = sizeof(addr_in);

  ASSERT_EQ(bind(listener.get(), addr, addr_len), 0) << strerror(errno);
  {
    socklen_t addr_len_in = addr_len;
    ASSERT_EQ(getsockname(listener.get(), addr, &addr_len), 0) << strerror(errno);
    EXPECT_EQ(addr_len, addr_len_in);
  }
  ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

  // Connect asynchronously and immediately hang up the listener.

  ASSERT_EQ(connect(client.get(), addr, addr_len), -1);
  ASSERT_EQ(errno, EINPROGRESS) << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(hangup(&listener));

  // Wait for the connection to close.
  {
    struct pollfd pfd = {
        .fd = client.get(),
        .events = POLLIN,
    };

    int n = poll(&pfd, 1, kTimeout);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
  }

  ASSERT_EQ(close(client.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, CloseDuringConnect) {
  TestHangupDuringConnect([](fbl::unique_fd* listener) {
    ASSERT_EQ(close(listener->release()), 0) << strerror(errno);
  });
}

TEST(NetStreamTest, ShutdownDuringConnect) {
#if defined(__Fuchsia__)
  GTEST_SKIP() << "TODO(fxbug.dev/35594): shutdown doesn't work on listeners";
#endif
  TestHangupDuringConnect([](fbl::unique_fd* listener) {
    ASSERT_EQ(shutdown(listener->get(), SHUT_RD), 0) << strerror(errno);
  });
}

TEST(LocalhostTest, RaceLocalPeerClose) {
  fbl::unique_fd listener;

  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  constexpr int kIterations = 50;

  ASSERT_EQ(bind(listener.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(listener.get(), kIterations), 0) << strerror(errno);

  std::vector<std::thread> threads;
  threads.reserve(kIterations);
  // Run many iterations in parallel in order to increase load on Netstack and increase the
  // probability we'll hit the problem.
  for (int i = 0; i < kIterations; i++) {
    threads.emplace_back(std::thread([&] {
      fbl::unique_fd peer, local;
      ASSERT_TRUE(peer = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
      // Setting SO_LINGER to 0 and `close`ing the peer socket should
      // immediately send a TCP RST.
      struct linger opt = {
          .l_onoff = 1,
          .l_linger = 0,
      };
      EXPECT_EQ(setsockopt(peer.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);

      // Start a connection and immediately close it.
      ASSERT_EQ(connect(peer.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
                0)
          << strerror(errno);
      ASSERT_EQ(close(peer.release()), 0) << strerror(errno);

      // Accept the connection created above and close it in sequence. Ensure that signaling races
      // within Netstack from the previous connection closing are not wrong or catastrophic.
      ASSERT_TRUE(local = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
          << strerror(errno);
      ASSERT_EQ(close(local.release()), 0) << strerror(errno);
    }));
  }

  for (auto& t : threads) {
    t.join();
  }

  ASSERT_EQ(close(listener.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, GetAddrInfo) {
  struct addrinfo hints = {
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
  };

  struct addrinfo* result;
  ASSERT_EQ(getaddrinfo("localhost", NULL, &hints, &result), 0) << strerror(errno);

  int i = 0;
  for (struct addrinfo* ai = result; ai != NULL; ai = ai->ai_next) {
    i++;

    EXPECT_EQ(ai->ai_socktype, hints.ai_socktype);
    const struct sockaddr* sa = ai->ai_addr;

    switch (ai->ai_family) {
      case AF_INET: {
        EXPECT_EQ(ai->ai_addrlen, (socklen_t)16);

        unsigned char expected_addr[4] = {0x7f, 0x00, 0x00, 0x01};

        const struct sockaddr_in* sin = (struct sockaddr_in*)sa;
        EXPECT_EQ(sin->sin_addr.s_addr, *reinterpret_cast<uint32_t*>(expected_addr));

        break;
      }
      case AF_INET6: {
        EXPECT_EQ(ai->ai_addrlen, (socklen_t)28);

        const char expected_addr[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

        auto sin6 = reinterpret_cast<const struct sockaddr_in6*>(sa);
        EXPECT_STREQ((const char*)sin6->sin6_addr.s6_addr, expected_addr);

        break;
      }
    }
  }
  EXPECT_EQ(i, 2);
  freeaddrinfo(result);
}

TEST(LocalhostTest, GetSockName) {
  fbl::unique_fd sockfd;
  ASSERT_TRUE(sockfd = fbl::unique_fd(socket(AF_INET6, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr sa;
  socklen_t len = sizeof(sa);
  ASSERT_EQ(getsockname(sockfd.get(), &sa, &len), 0) << strerror(errno);
  ASSERT_GT(len, sizeof(sa));
  ASSERT_EQ(sa.sa_family, AF_INET6);
}

TEST(NetStreamTest, PeerClosedPOLLOUT) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  EXPECT_EQ(bind(acptfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(acptfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));

  EXPECT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  EXPECT_EQ(connect(clientfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);

  fill_stream_send_buf(connfd.get(), clientfd.get());

  EXPECT_EQ(close(clientfd.release()), 0) << strerror(errno);

  struct pollfd pfd = {
      .fd = connfd.get(),
      .events = POLLOUT,
  };
  int n = poll(&pfd, 1, kTimeout);
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLOUT | POLLERR | POLLHUP);

  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, BlockingAcceptWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

class TimeoutSockoptsTest : public ::testing::TestWithParam<int /* optname */> {};

TEST_P(TimeoutSockoptsTest, TimeoutSockopts) {
  int optname = GetParam();
  ASSERT_TRUE(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO);

  fbl::unique_fd socket_fd;
  ASSERT_TRUE(socket_fd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  // Set the timeout.
  const struct timeval expected_tv = {
      .tv_sec = 39,
      // NB: for some reason, Linux's resolution is limited to 4ms.
      .tv_usec = 504000,
  };
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv, sizeof(expected_tv)), 0)
      << strerror(errno);

  // Reading it back should work.
  struct timeval actual_tv;
  socklen_t optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, expected_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, expected_tv.tv_usec);

  // Reading it back with too much space should work and set optlen.
  char actual_tv2_buffer[sizeof(struct timeval) * 2];
  memset(&actual_tv2_buffer, 44, sizeof(actual_tv2_buffer));
  optlen = sizeof(actual_tv2_buffer);
  struct timeval* actual_tv2 = (struct timeval*)&actual_tv2_buffer;
  EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, actual_tv2, &optlen), 0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(struct timeval));
  EXPECT_EQ(actual_tv2->tv_sec, expected_tv.tv_sec);
  EXPECT_EQ(actual_tv2->tv_usec, expected_tv.tv_usec);
  for (auto i = sizeof(struct timeval); i < sizeof(struct timeval) * 2; i++) {
    EXPECT_EQ(actual_tv2_buffer[i], 44);
  }

  // Reading it back without enough space should fail gracefully.
  memset(&actual_tv, 0, sizeof(actual_tv));
  optlen = sizeof(actual_tv) - 7;  // Not enough space to store the result.
  // TODO(eyalsoha): Decide if we want to match Linux's behaviour.  It writes to
  // only the first optlen bytes of the timeval.
  EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen),
#if defined(__Fuchsia__)
            -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
#else
            0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv) - 7);
  struct timeval linux_expected_tv = expected_tv;
  memset(((char*)&linux_expected_tv) + optlen, 0, sizeof(linux_expected_tv) - optlen);
  EXPECT_EQ(memcmp(&actual_tv, &linux_expected_tv, sizeof(actual_tv)), 0);
#endif

  // Setting it without enough space should fail gracefully.
  optlen = sizeof(expected_tv) - 1;  // Not big enough.
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv, optlen), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  // Setting it with too much space should work okay.
  const struct timeval expected_tv2 = {
      .tv_sec = 42,
      .tv_usec = 0,
  };
  optlen = sizeof(expected_tv2) + 1;  // Too big.
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &expected_tv2, optlen), 0)
      << strerror(errno);
  EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(expected_tv2));
  EXPECT_EQ(actual_tv.tv_sec, expected_tv2.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, expected_tv2.tv_usec);

  // Disabling rcvtimeo by setting it to zero should work.
  const struct timeval zero_tv = {
      .tv_sec = 0,
      .tv_usec = 0,
  };
  optlen = sizeof(zero_tv);
  EXPECT_EQ(setsockopt(socket_fd.get(), SOL_SOCKET, optname, &zero_tv, optlen), 0)
      << strerror(errno);

  // Reading back the disabled timeout should work.
  memset(&actual_tv, 55, sizeof(actual_tv));
  optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd.get(), SOL_SOCKET, optname, &actual_tv, &optlen), 0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, zero_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, zero_tv.tv_usec);
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, TimeoutSockoptsTest,
                         ::testing::Values(SO_RCVTIMEO, SO_SNDTIMEO));

const int32_t kConnections = 100;

TEST(NetStreamTest, BlockingAcceptWriteMultiple) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), kConnections), 0) << strerror(errno);

  fbl::unique_fd clientfds[kConnections];
  for (int i = 0; i < kConnections; i++) {
    ASSERT_TRUE(clientfds[i] = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(clientfds[i].get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
        << strerror(errno);
  }

  const char msg[] = "hello";
  for (int i = 0; i < kConnections; i++) {
    fbl::unique_fd connfd;
    ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

    ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
    ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);
  }

  for (int i = 0; i < kConnections; i++) {
    char buf[sizeof(msg) + 1] = {};
    ASSERT_EQ(read(clientfds[i].get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
    ASSERT_STREQ(buf, msg);
    ASSERT_EQ(close(clientfds[i].release()), 0) << strerror(errno);
  }

  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, BlockingAcceptDupWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  fbl::unique_fd dupfd;
  ASSERT_TRUE(dupfd = fbl::unique_fd(dup(connfd.get()))) << strerror(errno);
  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(dupfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(dupfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  struct pollfd pfd = {
      .fd = acptfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptDupWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(clientfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  struct pollfd pfd = {
      .fd = acptfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  fbl::unique_fd dupfd;
  ASSERT_TRUE(dupfd = fbl::unique_fd(dup(connfd.get()))) << strerror(errno);
  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(dupfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(dupfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectWrite) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 10), 0) << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    struct pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLOUT,
    };
    int n = poll(&pfd, 1, kTimeout);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(optlen, sizeof(err));
  }

  fbl::unique_fd clientfd;
  ASSERT_TRUE(clientfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr))) << strerror(errno);

  const char msg[] = "hello";
  ASSERT_EQ(write(connfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);

  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(clientfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectRead) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  ASSERT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    fbl::unique_fd clientfd;
    ASSERT_TRUE(clientfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr)))
        << strerror(errno);

    const char msg[] = "hello";
    ASSERT_EQ(write(clientfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
    ASSERT_EQ(close(clientfd.release()), 0) << strerror(errno);

    // Note: the success of connection can be detected with POLLOUT, but
    // we use POLLIN here to wait until some data is written by the peer.
    struct pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, kTimeout);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(err, 0);
    ASSERT_EQ(optlen, sizeof(err));

    char buf[sizeof(msg) + 1] = {};
    ASSERT_EQ(read(connfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
    ASSERT_STREQ(buf, msg);
    ASSERT_EQ(close(connfd.release()), 0) << strerror(errno);
    EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
  }
}

// ReadBeforeConnect tests the application behavior when we start to
// read from a stream socket that is not yet connected.
TEST(NetStreamTest, ReadBeforeConnect) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(listener.get(), 0), 0) << strerror(errno);

  // Setup a test client connection over which we test socket reads
  // when the connection is not yet established.

  // Linux default behavior is to complete one more connection than what
  // was passed as listen backlog (zero here).
  // Hence we initiate 2 client connections in this order:
  // (1) a precursor client for the sole purpose of filling up the server
  //     accept queue after handshake completion.
  // (2) a test client that keeps trying to establish connection with
  //     server, but remains in SYN-SENT.
#if !defined(__Fuchsia__)
  // TODO(gvisor.dev/issue/3153): Unlike Linux, gVisor does not complete
  // handshake for a connection when listen backlog is zero. Hence, we
  // do not maintain the precursor client connection on Fuchsia.
  fbl::unique_fd precursor_client;
  ASSERT_TRUE(precursor_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0)))
      << strerror(errno);
  ASSERT_EQ(
      connect(precursor_client.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);

  // Observe the precursor client connection on the server side. This ensures that the TCP stack's
  // server accept queue is updated with the precursor client connection before any subsequent
  // client connect requests. The precursor client connect call returns after handshake completion,
  // but not necessarily after the server side has processed the ACK from the client and updated its
  // accept queue.
  struct pollfd pfd = {
      .fd = listener.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  ASSERT_EQ(pfd.revents, POLLIN);
#endif

  // The test client connection would get established _only_ after both
  // these conditions are met:
  // (1) prior client connections are accepted by the server thus
  //     making room for a new connection.
  // (2) the server-side TCP stack completes handshake in response to
  //     the retransmitted SYN for the test client connection.
  //
  // The test would likely perform socket reads before any connection
  // timeout.
  fbl::unique_fd test_client;
  ASSERT_TRUE(test_client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  char c;
  // Test read before initiating connect.
  EXPECT_EQ(read(test_client.get(), &c, sizeof(c)), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  ASSERT_EQ(connect(test_client.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)),
            -1);
  ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

  // Test socket read without waiting for connection to be established.
  EXPECT_EQ(read(test_client.get(), &c, sizeof(c)), -1);
  EXPECT_EQ(errno, EWOULDBLOCK) << strerror(errno);

  std::latch fut_started(1);
  // Asynchronously block on reading from the test client socket.
  const auto fut = std::async(std::launch::async, [&]() {
    // Make the socket blocking.
    int status = fcntl(test_client.get(), F_GETFL, 0);
    EXPECT_EQ(0, fcntl(test_client.get(), F_SETFL, status ^ O_NONBLOCK));

    fut_started.count_down();

    char c;
    // Block on read before the connection is established.
    EXPECT_EQ(read(test_client.get(), &c, sizeof(c)), static_cast<ssize_t>(sizeof(c)))
        << strerror(errno);
  });
  fut_started.wait();
  // Wait for the task to be blocked on read.
  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

#if !defined(__Fuchsia__)
  // Accept the precursor connection to make room for the test client
  // connection to complete.
  fbl::unique_fd precursor_accept;
  ASSERT_TRUE(precursor_accept = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
      << strerror(errno);
  ASSERT_EQ(close(precursor_accept.release()), 0) << strerror(errno);
  ASSERT_EQ(close(precursor_client.release()), 0) << strerror(errno);
#endif

  // TODO(gvisor.dev/issue/3153): Unlike Linux, gVisor does not accept a connection
  // when listen backlog is zero.
#if defined(__Fuchsia__)
  ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);
#endif

  // Accept the test client connection.
  fbl::unique_fd test_accept;
  ASSERT_TRUE(test_accept = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
      << strerror(errno);

  // Write data to unblock the socket read on the test client connection.
  ASSERT_EQ(write(test_accept.get(), &c, sizeof(c)), static_cast<ssize_t>(sizeof(c)))
      << strerror(errno);

  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);

  ASSERT_EQ(close(listener.release()), 0) << strerror(errno);
  ASSERT_EQ(close(test_accept.release()), 0) << strerror(errno);
  ASSERT_EQ(close(test_client.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectRefused) {
  fbl::unique_fd acptfd;
  ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  // No listen() on acptfd.

  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  int status = fcntl(connfd.get(), F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd.get(), F_SETFL, status | O_NONBLOCK));

  int ret;
  EXPECT_EQ(ret = connect(connfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    struct pollfd pfd = {
        .fd = connfd.get(),
        .events = POLLOUT,
    };
    int n = poll(&pfd, 1, kTimeout);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);

    int err;
    socklen_t optlen = sizeof(err);
    ASSERT_EQ(getsockopt(connfd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
    ASSERT_EQ(err, ECONNREFUSED);
    ASSERT_EQ(optlen, sizeof(err));
  }

  EXPECT_EQ(close(connfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, GetTcpInfo) {
  fbl::unique_fd connfd;
  ASSERT_TRUE(connfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  tcp_info info;
  socklen_t info_len = sizeof(tcp_info);
  ASSERT_GE(getsockopt(connfd.get(), SOL_TCP, TCP_INFO, (void*)&info, &info_len), 0)
      << strerror(errno);
  ASSERT_EQ(sizeof(tcp_info), info_len);

  ASSERT_EQ(0, close(connfd.release()));
}

// Test socket reads on disconnected stream sockets.
TEST(NetStreamTest, DisconnectedRead) {
  fbl::unique_fd socketfd;
  ASSERT_TRUE(socketfd = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  struct timeval tv = {
      // Use minimal non-zero timeout as we expect the blocking recv to return before it actually
      // starts reading. Without the timeout, the test could deadlock on a blocking recv, when the
      // underlying code is broken.
      .tv_usec = 1u,
  };
  EXPECT_EQ(setsockopt(socketfd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);
  // Test blocking socket read.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  // Test non blocking socket read.
  int flags;
  EXPECT_GE(flags = fcntl(socketfd.get(), F_GETFL, 0), 0) << strerror(errno);
  EXPECT_EQ(fcntl(socketfd.get(), F_SETFL, flags | O_NONBLOCK), 0) << strerror(errno);
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd.get(), nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  EXPECT_EQ(close(socketfd.release()), 0) << strerror(errno);
}

TEST(NetStreamTest, Shutdown) {
  fbl::unique_fd listener;
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  EXPECT_EQ(bind(listener.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(listener.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));
  EXPECT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

  fbl::unique_fd outbound;
  ASSERT_TRUE(outbound = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  // Wrap connect() in a future to enable a timeout.
  std::future<void> fut = std::async(std::launch::async, [&outbound, addr]() {
    EXPECT_EQ(connect(outbound.get(), (struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);
  });

  fbl::unique_fd inbound;
  ASSERT_TRUE(inbound = fbl::unique_fd(accept(listener.get(), NULL, NULL))) << strerror(errno);

  // Wait for connect() to finish.
  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);

  EXPECT_EQ(shutdown(inbound.get(), SHUT_WR), 0) << strerror(errno);

  struct pollfd pfd = {
      .fd = outbound.get(),
      .events = POLLRDHUP,
  };
  int n = poll(&pfd, 1, kTimeout);
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLRDHUP);

  EXPECT_EQ(close(listener.release()), 0) << strerror(errno);
  EXPECT_EQ(close(outbound.release()), 0) << strerror(errno);
  EXPECT_EQ(close(inbound.release()), 0) << strerror(errno);
}

class NetStreamSocketsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fbl::unique_fd listener;
    ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    ASSERT_EQ(bind(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

    ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_TRUE(server = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
        << strerror(errno);
    // We're done with the listener.
    ASSERT_EQ(close(listener.release()), 0) << strerror(errno);
  }
  fbl::unique_fd client;
  fbl::unique_fd server;
};

TEST_F(NetStreamSocketsTest, ResetOnFullReceiveBufferShutdown) {
  // Fill the receive buffer of the client socket.
  fill_stream_send_buf(server.get(), client.get());

  // Setting SO_LINGER to 0 and `close`ing the server socket should
  // immediately send a TCP RST.
  struct linger opt = {
      .l_onoff = 1,
      .l_linger = 0,
  };
  EXPECT_EQ(setsockopt(server.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
      << strerror(errno);

  // Close the server to trigger a TCP RST now that linger is 0.
  EXPECT_EQ(close(server.release()), 0) << strerror(errno);

  // Wait for the RST.
  struct pollfd pfd = {
      .fd = client.get(),
      .events = POLLHUP,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLHUP | POLLERR);

  // The socket is no longer connected.
  EXPECT_EQ(shutdown(client.get(), SHUT_RD), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  // Create another socket to ensure that the networking stack hasn't panicked.
  fbl::unique_fd test_sock;
  ASSERT_TRUE(test_sock = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
}

// Tests that a socket which has completed SHUT_RDWR responds to incoming data with RST.
TEST_F(NetStreamSocketsTest, ShutdownReset) {
  // This test is tricky. In Linux we could shutdown(SHUT_RDWR) the server socket, write() some data
  // on the client socket, and observe the server reply with RST. The SHUT_WR would move the server
  // socket state out of ESTABLISHED (to FIN-WAIT2 after sending FIN and receiving an ACK) and
  // SHUT_RD would close the receiver. Only when the server socket has transitioned out of
  // ESTABLISHED state. At this point, the server socket would respond to incoming data with RST.
  //
  // In Fuchsia this is more complicated because each socket is a distributed system (consisting of
  // netstack and fdio) wherein the socket state is eventually consistent. We must take care to
  // synchronize our actions with netstack's state as we're testing that netstack correctly sends a
  // RST in response to data received after shutdown(SHUT_RDWR).
  //
  // We can manipulate and inspect state using only shutdown() and poll(), both of which operate on
  // fdio state rather than netstack state. Combined with the fact that SHUT_RD is not observable by
  // the peer (i.e. doesn't cause any network traffic), means we are in a pickle.
  //
  // On the other hand, SHUT_WR does cause a FIN to be sent, which can be observed by the peer using
  // poll(POLLRDHUP). Note also that netstack observes SHUT_RD and SHUT_WR on different threads,
  // meaning that a race condition still exists. At the time of writing, this is the best we can do.

  // Change internal state to disallow further reads and writes. The state change propagates to
  // netstack at some future time. We have no way to observe that SHUT_RD has propagated (because it
  // propagates independently from SHUT_WR).
  ASSERT_EQ(shutdown(server.get(), SHUT_RDWR), 0) << strerror(errno);

  // Wait for the FIN to arrive at the client and for the state to propagate to the client's fdio.
  {
    struct pollfd pfd = {
        .fd = client.get(),
        .events = POLLRDHUP,
    };
    int n = poll(&pfd, 1, kTimeout);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLRDHUP);
  }

  // Send data from the client. The server should now very likely be in SHUT_RD and respond with
  // RST.
  char c;
  ASSERT_EQ(write(client.get(), &c, sizeof(c)), static_cast<ssize_t>(sizeof(c))) << strerror(errno);

  // Wait for the client to receive the RST and for the state to propagate through its fdio.
  struct pollfd pfd = {
      .fd = client.get(),
      .events = POLLHUP,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  EXPECT_EQ(pfd.revents, POLLHUP | POLLERR);
}

// ShutdownPendingWrite tests for all of the application writes that
// occurred before shutdown SHUT_WR, to be received by the remote.
TEST_F(NetStreamSocketsTest, ShutdownPendingWrite) {
  // Fill the send buffer of the server socket so that we have some
  // pending data waiting to be sent out to the remote.
  ssize_t wrote = fill_stream_send_buf(server.get(), client.get());

  // SHUT_WR should enqueue a FIN after all of the application writes.
  EXPECT_EQ(shutdown(server.get(), SHUT_WR), 0) << strerror(errno);

  // All client reads are expected to return here, including the last
  // read on receiving a FIN. Keeping a timeout for unexpected failures.
  struct timeval tv = {
      .tv_sec = kTimeout,
  };
  EXPECT_EQ(setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);

  ssize_t rcvd = 0;
  ssize_t ret;
  // Keep a large enough buffer to reduce the number of read calls, as
  // we expect the receive buffer to be filled up at this point.
  char buf[4096];
  // Each read would make room for the server to send out more data
  // that has been enqueued from successful server socket writes.
  while ((ret = read(client.get(), &buf, sizeof(buf))) > 0) {
    rcvd += ret;
  }
  // Expect the last read to return 0 after the stack sees a FIN.
  EXPECT_EQ(ret, 0) << strerror(errno);
  // Expect no data drops and all written data by server is received
  // by the client.
  EXPECT_EQ(rcvd, wrote);
}

enum class IOMethod {
  READ,
  READV,
  RECV,
  RECVFROM,
  RECVMSG,
  WRITE,
  WRITEV,
  SEND,
  SENDTO,
  SENDMSG,
};

constexpr const char* IOMethodToString(const IOMethod s) {
  switch (s) {
    case IOMethod::READ:
      return "Read";
    case IOMethod::READV:
      return "Readv";
    case IOMethod::RECV:
      return "Recv";
    case IOMethod::RECVFROM:
      return "Recvfrom";
    case IOMethod::RECVMSG:
      return "Recvmsg";
    case IOMethod::WRITE:
      return "Write";
    case IOMethod::WRITEV:
      return "Writev";
    case IOMethod::SEND:
      return "Send";
    case IOMethod::SENDTO:
      return "Sendto";
    case IOMethod::SENDMSG:
      return "Sendmsg";
  }
}

enum class CloseTarget {
  CLIENT,
  SERVER,
};

constexpr const char* CloseTargetToString(const CloseTarget s) {
  switch (s) {
    case CloseTarget::CLIENT:
      return "Client";
    case CloseTarget::SERVER:
      return "Server";
  }
}

using blockedIOParams = std::tuple<IOMethod, CloseTarget, bool>;

class BlockedIOTest : public ::testing::TestWithParam<blockedIOParams> {};

TEST_P(BlockedIOTest, CloseWhileBlocked) {
  auto const& [ioMethod, closeTarget, lingerEnabled] = GetParam();

  fbl::unique_fd client, server;
  {
    fbl::unique_fd listener;
    ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    ASSERT_EQ(bind(listener.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
        << strerror(errno);

    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(listener.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);

    ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
    ASSERT_EQ(connect(client.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_TRUE(server = fbl::unique_fd(accept(listener.get(), nullptr, nullptr)))
        << strerror(errno);
    // We're done with the listener.
    ASSERT_EQ(close(listener.release()), 0) << strerror(errno);
  }

  auto executeIO = [&, ioMethod = ioMethod]() {
    char c;
    switch (ioMethod) {
      case IOMethod::READ: {
        return read(client.get(), &c, sizeof(c));
      }
      case IOMethod::READV: {
        struct iovec iov = {
            .iov_base = &c,
            .iov_len = sizeof(c),
        };
        return readv(client.get(), &iov, 1);
      }
      case IOMethod::RECV: {
        return recv(client.get(), &c, sizeof(c), 0);
      }
      case IOMethod::RECVFROM: {
        return recvfrom(client.get(), &c, sizeof(c), 0, nullptr, 0);
      }
      case IOMethod::RECVMSG: {
        struct iovec iov = {
            .iov_base = &c,
            .iov_len = sizeof(c),
        };
        struct msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
        };
        return recvmsg(client.get(), &msg, 0);
      }
      case IOMethod::WRITE: {
        return write(client.get(), &c, sizeof(c));
      }
      case IOMethod::WRITEV: {
        struct iovec iov = {
            .iov_base = &c,
            .iov_len = sizeof(c),
        };
        return writev(client.get(), &iov, 1);
      }
      case IOMethod::SEND: {
        return send(client.get(), &c, sizeof(c), 0);
      }
      case IOMethod::SENDTO: {
        return sendto(client.get(), &c, sizeof(c), 0, nullptr, 0);
      }
      case IOMethod::SENDMSG: {
        struct iovec iov = {
            .iov_base = &c,
            .iov_len = sizeof(c),
        };
        struct msghdr msg = {
            .msg_iov = &iov,
            .msg_iovlen = 1,
        };
        return sendmsg(client.get(), &msg, 0);
      }
    }
  };

  bool isWrite;
  switch (ioMethod) {
    case IOMethod::READ:
    case IOMethod::READV:
    case IOMethod::RECV:
    case IOMethod::RECVFROM:
    case IOMethod::RECVMSG:
      isWrite = false;
      break;
    case IOMethod::WRITE:
    case IOMethod::WRITEV:
    case IOMethod::SEND:
    case IOMethod::SENDTO:
    case IOMethod::SENDMSG:
      isWrite = true;
      break;
  }

  // If linger is enabled, closing the socket will cause a TCP RST (by definition).
  bool closeRST = lingerEnabled;
  if (isWrite) {
    // Fill the send buffer of the client socket to cause write to block.
    fill_stream_send_buf(client.get(), server.get());
    // Buffes are full. Closing the socket will now cause a TCP RST.
    closeRST = true;
  }

#if defined(__Fuchsia__)
  bool isClient = closeTarget == CloseTarget::CLIENT;
#endif

  // While blocked in I/O, close the peer.
  std::latch fut_started(1);
  const auto fut = std::async(std::launch::async, [&]() {
    fut_started.count_down();

#if defined(__Fuchsia__)
    // TODO(fxbug.dev/45407): Linux considers the pending I/O to hold a reference to the file
    // descriptor, and we should too.
    if (isClient) {
      ASSERT_EQ(executeIO(), -1);
      EXPECT_EQ(errno, EBADF) << strerror(errno);
      return;
    }
#endif

    if (closeRST) {
      ASSERT_EQ(executeIO(), -1);
      EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
    } else {
      ASSERT_EQ(executeIO(), 0) << strerror(errno);
    }
  });
  fut_started.wait();
  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

  // When enabled, causes `close` to send a TCP RST.
  struct linger opt = {
      .l_onoff = lingerEnabled,
      .l_linger = 0,
  };

  switch (closeTarget) {
    case CloseTarget::CLIENT: {
      ASSERT_EQ(setsockopt(client.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);
      ASSERT_EQ(close(client.release()), 0) << strerror(errno);
#if defined(__Fuchsia__)
      // TODO(fxbug.dev/45407): Fuchsia incorrectly interrupts the pending I/O when the file
      // descriptor is closed.
      break;
#else
      // Closing the file descriptor does not interrupt the pending I/O.
      ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

      // Fallthrough to unblock the future.
#endif
    }
    case CloseTarget::SERVER: {
      ASSERT_EQ(setsockopt(server.get(), SOL_SOCKET, SO_LINGER, &opt, sizeof(opt)), 0)
          << strerror(errno);
      ASSERT_EQ(close(server.release()), 0) << strerror(errno);
      break;
    }
  }
  ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);

#if !defined(__Fuchsia__)
  // Linux generates SIGPIPE on stream writes when the peer has closed the connection. send{,to,msg}
  // support the MSG_NOSIGNAL flag to suppress this behaviour, but write and writev do not. We only
  // expect this during the second attempt, so we remove the default signal handler, make our
  // attempt, and then restore it.
  struct sigaction act = {
      .sa_handler = SIG_IGN,
  };

  struct sigaction oldact;
  if (isWrite) {
    ASSERT_EQ(sigaction(SIGPIPE, &act, &oldact), 0) << strerror(errno);
  }

  auto undo = fbl::MakeAutoCall([&]() {
    if (isWrite) {
      ASSERT_EQ(sigaction(SIGPIPE, &oldact, nullptr), 0) << strerror(errno);
    }
  });
#endif

  switch (closeTarget) {
    case CloseTarget::CLIENT: {
      ASSERT_EQ(executeIO(), -1);
      EXPECT_EQ(errno, EBADF) << strerror(errno);
      break;
    }
    case CloseTarget::SERVER: {
      if (isWrite) {
        ASSERT_EQ(executeIO(), -1);
        EXPECT_EQ(errno, EPIPE) << strerror(errno);
      } else {
        ASSERT_EQ(executeIO(), 0) << strerror(errno);
      }
      break;
    }
  }
}

std::string blockedIOParamsToString(const ::testing::TestParamInfo<blockedIOParams> info) {
  // NB: this is a freestanding function because structured binding declarations are not allowed in
  // lambdas.
  auto const& [ioMethod, closeTarget, lingerEnabled] = info.param;
  std::stringstream s;
  s << "close" << CloseTargetToString(closeTarget) << "Linger";
  if (lingerEnabled) {
    s << "Foreground";
  } else {
    s << "Background";
  }
  s << "During" << IOMethodToString(ioMethod);

  return s.str();
}

INSTANTIATE_TEST_SUITE_P(
    NetStreamTest, BlockedIOTest,
    ::testing::Combine(::testing::Values(IOMethod::READ, IOMethod::READV, IOMethod::RECV,
                                         IOMethod::RECVFROM, IOMethod::RECVMSG, IOMethod::WRITE,
                                         IOMethod::WRITEV, IOMethod::SEND, IOMethod::SENDTO,
                                         IOMethod::SENDMSG),
                       ::testing::Values(CloseTarget::CLIENT, CloseTarget::SERVER),
                       ::testing::Values(false, true)),
    blockedIOParamsToString);

// Use this routine to test blocking socket reads. On failure, this attempts to recover the blocked
// thread.
// Return value:
//      (1) actual length of read data on successful recv
//      (2) 0, when we abort a blocked recv
//      (3) -1, on failure of both of the above operations.
static ssize_t asyncSocketRead(int recvfd, int sendfd, char* buf, ssize_t len, int flags,
                               struct sockaddr_in* addr, socklen_t* addrlen, int socketType,
                               std::chrono::duration<double> timeout) {
  std::future<ssize_t> recv = std::async(std::launch::async, [recvfd, buf, len, flags]() {
    memset(buf, 0xdead, len);
    return recvfrom(recvfd, buf, len, flags, nullptr, nullptr);
  });

  if (recv.wait_for(timeout) == std::future_status::ready) {
    return recv.get();
  }

  // recover the blocked receiver thread
  switch (socketType) {
    case SOCK_STREAM: {
      // shutdown() would unblock the receiver thread with recv returning 0.
      EXPECT_EQ(shutdown(recvfd, SHUT_RD), 0) << strerror(errno);
      // We do not use 'timeout' because that maybe short here. We expect to succeed and hence use a
      // known large timeout to ensure the test does not hang in case underlying code is broken.
      EXPECT_EQ(recv.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);
      EXPECT_EQ(recv.get(), 0);
      break;
    }
    case SOCK_DGRAM: {
      // Send a 0 length payload to unblock the receiver.
      // This would ensure that the async-task deterministically exits before call to future`s
      // destructor. Calling close(.release()) on recvfd when the async task is blocked on recv(),
      // __does_not__ cause recv to return; this can result in undefined behavior, as the descriptor
      // can get reused. Instead of sending a valid packet to unblock the recv() task, we could call
      // shutdown(), but that returns ENOTCONN (unconnected) but still causing recv() to return.
      // shutdown() becomes unreliable for unconnected UDP sockets because, irrespective of the
      // effect of calling this call, it returns error.
      EXPECT_EQ(sendto(sendfd, nullptr, 0, 0, reinterpret_cast<struct sockaddr*>(addr), *addrlen),
                0)
          << strerror(errno);
      // We use a known large timeout for the same reason as for the above case.
      EXPECT_EQ(recv.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);
      EXPECT_EQ(recv.get(), 0);
      break;
    }
    default: {
      return -1;
    }
  }
  return 0;
}

class DatagramSendTest : public ::testing::TestWithParam<enum IOMethod> {};

TEST_P(DatagramSendTest, SendToIPv4MappedIPv6FromAF_INET) {
  enum IOMethod IOMethod = GetParam();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(fd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(fd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  struct sockaddr_in6 addr6 = {
      .sin6_family = AF_INET6,
      .sin6_port = addr.sin_port,
  };
  addr6.sin6_addr.s6_addr[10] = 0xff;
  addr6.sin6_addr.s6_addr[11] = 0xff;
  memcpy(&addr6.sin6_addr.s6_addr[12], &addr.sin_addr.s_addr, sizeof(addr.sin_addr.s_addr));

  char buf[INET6_ADDRSTRLEN];
  ASSERT_TRUE(IN6_IS_ADDR_V4MAPPED(&addr6.sin6_addr))
      << inet_ntop(addr6.sin6_family, &addr6.sin6_addr, buf, sizeof(buf));

  switch (IOMethod) {
    case IOMethod::SENDTO: {
      ASSERT_EQ(sendto(fd.get(), NULL, 0, 0, (const struct sockaddr*)&addr6, sizeof(addr6)), -1);
      ASSERT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
      break;
    }
    case IOMethod::SENDMSG: {
      struct msghdr msghdr = {
          .msg_name = &addr6,
          .msg_namelen = sizeof(addr6),
      };
      ASSERT_EQ(sendmsg(fd.get(), &msghdr, 0), -1);
      ASSERT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
}

TEST_P(DatagramSendTest, DatagramSend) {
  enum IOMethod IOMethod = GetParam();
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  EXPECT_EQ(bind(recvfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(recvfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));

  const std::string msg = "hello";
  char recvbuf[32] = {};
  struct iovec iov = {
      .iov_base = (void*)msg.data(),
      .iov_len = msg.size(),
  };
  struct msghdr msghdr = {
      .msg_name = &addr,
      .msg_namelen = addrlen,
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  switch (IOMethod) {
    case IOMethod::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)msg.size())
          << strerror(errno);
      break;
    }
    case IOMethod::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), (ssize_t)msg.size()) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  auto expect_success_timeout = std::chrono::milliseconds(kTimeout);
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0, &addr,
                            &addrlen, SOCK_DGRAM, expect_success_timeout),
            (ssize_t)msg.size());
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(std::string(recvbuf, msg.size()), msg);
  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

  // sendto/sendmsg on connected sockets does accept sockaddr input argument and
  // also lets the dest sockaddr be overridden from what was passed for connect.
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  EXPECT_EQ(connect(sendfd.get(), (struct sockaddr*)&addr, addrlen), 0) << strerror(errno);
  switch (IOMethod) {
    case IOMethod::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)msg.size())
          << strerror(errno);
      break;
    }
    case IOMethod::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), (ssize_t)msg.size()) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0, &addr,
                            &addrlen, SOCK_DGRAM, expect_success_timeout),
            (ssize_t)msg.size());
  EXPECT_EQ(std::string(recvbuf, msg.size()), msg);

  // Test sending to an address that is different from what we're connected to.
  addr.sin_port = htons(ntohs(addr.sin_port) + 1);
  switch (IOMethod) {
    case IOMethod::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)msg.size())
          << strerror(errno);
      break;
    }
    case IOMethod::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), (ssize_t)msg.size()) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  // Expect blocked receiver and try to recover it by sending a packet to the
  // original connected sockaddr.
  addr.sin_port = htons(ntohs(addr.sin_port) - 1);
  // As we expect failure, to keep the recv wait time minimal, we base it on the time taken for a
  // successful recv.
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0, &addr,
                            &addrlen, SOCK_DGRAM, success_rcv_duration * 10),
            0);

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSendTest,
                         ::testing::Values(IOMethod::SENDTO, IOMethod::SENDMSG),
                         [](const ::testing::TestParamInfo<IOMethod>& info) {
                           return IOMethodToString(info.param);
                         });

TEST(NetDatagramTest, DatagramConnectWrite) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(recvfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(sendfd.get(), (struct sockaddr*)&addr, addrlen), 0) << strerror(errno);
  ASSERT_EQ(write(sendfd.get(), msg, sizeof(msg)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_EQ(close(sendfd.release()), 0) << strerror(errno);

  struct pollfd pfd = {
      .fd = recvfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(recvfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramPartialRecv) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(recvfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  const char kTestMsg[] = "hello";
  const int kTestMsgSize = sizeof(kTestMsg);

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(kTestMsgSize, sendto(sendfd.get(), kTestMsg, kTestMsgSize, 0,
                                 reinterpret_cast<sockaddr*>(&addr), addrlen));

  char recv_buf[kTestMsgSize];

  // Read only first 2 bytes of the message. recv() is expected to discard the
  // rest.
  const int kPartialReadSize = 2;

  struct iovec iov = {
      .iov_base = recv_buf,
      .iov_len = kPartialReadSize,
  };
  struct msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };

  ASSERT_EQ(recvmsg(recvfd.get(), &msg, 0), kPartialReadSize);
  ASSERT_EQ(std::string(kTestMsg, kPartialReadSize), std::string(recv_buf, kPartialReadSize));
  EXPECT_EQ(MSG_TRUNC, msg.msg_flags);

  // Send the second packet.
  ASSERT_EQ(kTestMsgSize, sendto(sendfd.get(), kTestMsg, kTestMsgSize, 0,
                                 reinterpret_cast<sockaddr*>(&addr), addrlen));

  // Read the whole packet now.
  recv_buf[0] = 0;
  iov.iov_len = sizeof(recv_buf);
  ASSERT_EQ(recvmsg(recvfd.get(), &msg, 0), kTestMsgSize);
  ASSERT_EQ(std::string(kTestMsg, kTestMsgSize), std::string(recv_buf, kTestMsgSize));
  EXPECT_EQ(msg.msg_flags, 0);

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramPOLLOUT) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct pollfd pfd = {
      .fd = fd.get(),
      .events = POLLOUT,
  };
  int n = poll(&pfd, 1, kTimeout);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

// DatagramSendtoRecvfrom tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.
TEST(NetDatagramTest, DatagramSendtoRecvfrom) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };

  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(
      sendto(sendfd.get(), msg, sizeof(msg), 0, reinterpret_cast<struct sockaddr*>(&addr), addrlen),
      (ssize_t)sizeof(msg))
      << strerror(errno);

  char buf[sizeof(msg) + 1] = {};

  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(recvfrom(recvfd.get(), buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer),
                     &peerlen),
            (ssize_t)sizeof(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  ASSERT_EQ(
      sendto(recvfd.get(), buf, sizeof(msg), 0, reinterpret_cast<struct sockaddr*>(&peer), peerlen),
      (ssize_t)sizeof(msg))
      << strerror(errno);

  ASSERT_EQ(recvfrom(sendfd.get(), buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer),
                     &peerlen),
            (ssize_t)sizeof(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  char addrbuf[INET_ADDRSTRLEN], peerbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  ASSERT_EQ(close(sendfd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

// DatagramSendtoRecvfromV6 tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.
TEST(NetDatagramTest, DatagramSendtoRecvfromV6) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_LOOPBACK_INIT,
  };

  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(
      sendto(sendfd.get(), msg, sizeof(msg), 0, reinterpret_cast<struct sockaddr*>(&addr), addrlen),
      (ssize_t)sizeof(msg))
      << strerror(errno);

  char buf[sizeof(msg) + 1] = {};

  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(recvfrom(recvfd.get(), buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer),
                     &peerlen),
            (ssize_t)sizeof(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  ASSERT_EQ(
      sendto(recvfd.get(), buf, sizeof(msg), 0, reinterpret_cast<struct sockaddr*>(&peer), peerlen),
      (ssize_t)sizeof(msg))
      << strerror(errno);

  ASSERT_EQ(recvfrom(sendfd.get(), buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer),
                     &peerlen),
            (ssize_t)sizeof(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  char addrbuf[INET6_ADDRSTRLEN], peerbuf[INET6_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin6_family, &addr.sin6_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  ASSERT_EQ(close(sendfd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV4) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV6) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
      .sin6_addr = IN6ADDR_ANY_INIT,
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV6MappedV4) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_INET6,
  };
  addr.sin6_addr.s6_addr[10] = 0xff;
  addr.sin6_addr.s6_addr[11] = 0xff;

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV4) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_UNSPEC,
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const struct sockaddr*>(&addr),
                    offsetof(sockaddr_in, sin_family) + sizeof(addr.sin_family)),
            0)
      << strerror(errno);
  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV6) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  struct sockaddr_in6 addr = {
      .sin6_family = AF_UNSPEC,
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const struct sockaddr*>(&addr),
                    offsetof(sockaddr_in6, sin6_family) + sizeof(addr.sin6_family)),
            0)
      << strerror(errno);
  ASSERT_EQ(close(fd.release()), 0) << strerror(errno);
}

// Note: we choose 100 because the max number of fds per process is limited to
// 256.
const int32_t kListeningSockets = 100;

TEST(NetStreamTest, MultipleListeningSockets) {
  fbl::unique_fd listenfd[kListeningSockets];
  fbl::unique_fd connfd[kListeningSockets];

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  socklen_t addrlen = sizeof(addr);

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_TRUE(listenfd[i] = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    ASSERT_EQ(
        bind(listenfd[i].get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_EQ(listen(listenfd[i].get(), 10), 0) << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(getsockname(listenfd[i].get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen),
              0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_TRUE(connfd[i] = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    ASSERT_EQ(
        connect(connfd[i].get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(0, close(connfd[i].release()));
    ASSERT_EQ(0, close(listenfd[i].release()));
  }
}

// Socket tests across multiple socket-types, SOCK_DGRAM, SOCK_STREAM.
class NetSocketTest : public ::testing::TestWithParam<int> {};

// Test MSG_PEEK
// MSG_PEEK : Peek into the socket receive queue without moving the contents from it.
//
// TODO(fxbug.dev/33100): change this test to use recvmsg instead of recvfrom to exercise MSG_PEEK
// with scatter/gather.
TEST_P(NetSocketTest, SocketPeekTest) {
  int socketType = GetParam();
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  socklen_t addrlen = sizeof(addr);
  fbl::unique_fd sendfd;
  fbl::unique_fd recvfd;
  ssize_t expectReadLen = 0;
  char sendbuf[8] = {};
  char recvbuf[2 * sizeof(sendbuf)] = {};
  ssize_t sendlen = sizeof(sendbuf);

  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, socketType, 0))) << strerror(errno);
  // Setup the sender and receiver sockets.
  switch (socketType) {
    case SOCK_STREAM: {
      fbl::unique_fd acptfd;
      ASSERT_TRUE(acptfd = fbl::unique_fd(socket(AF_INET, socketType, 0))) << strerror(errno);
      EXPECT_EQ(bind(acptfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
                0)
          << strerror(errno);
      EXPECT_EQ(getsockname(acptfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
          << strerror(errno);
      EXPECT_EQ(addrlen, sizeof(addr));
      EXPECT_EQ(listen(acptfd.get(), 1), 0) << strerror(errno);
      EXPECT_EQ(
          connect(sendfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
      ASSERT_TRUE(recvfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr)))
          << strerror(errno);
      EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
      // Expect to read both the packets in a single recv() call.
      expectReadLen = sizeof(recvbuf);
      break;
    }
    case SOCK_DGRAM: {
      ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, socketType, 0))) << strerror(errno);
      EXPECT_EQ(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
                0)
          << strerror(errno);
      EXPECT_EQ(getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
          << strerror(errno);
      EXPECT_EQ(addrlen, sizeof(addr));
      // Expect to read single packet per recv() call.
      expectReadLen = sizeof(sendbuf);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
    }
  }

  // This test sends 2 packets with known values and validates MSG_PEEK across the 2 packets.
  sendbuf[0] = 0xab;
  sendbuf[6] = 0xce;

  // send 2 separate packets and test peeking across
  EXPECT_EQ(sendto(sendfd.get(), sendbuf, sizeof(sendbuf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), addrlen),
            sendlen)
      << strerror(errno);
  EXPECT_EQ(sendto(sendfd.get(), sendbuf, sizeof(sendbuf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), addrlen),
            sendlen)
      << strerror(errno);

  auto expect_success_timeout = std::chrono::milliseconds(kTimeout);
  auto start = std::chrono::steady_clock::now();
  // First peek on first byte.
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, 1, MSG_PEEK, &addr, &addrlen,
                            socketType, expect_success_timeout),
            1);
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(recvbuf[0], sendbuf[0]);

  // Second peek across first 2 packets and drain them from the socket receive queue.
  // Toggle the flags to MSG_PEEK every other iteration.
  ssize_t torecv = sizeof(recvbuf);
  for (int i = 0; torecv > 0; i++) {
    int flags = i % 2 ? 0 : MSG_PEEK;
    ssize_t readLen = 0;
    EXPECT_EQ(readLen = asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), flags,
                                        &addr, &addrlen, socketType, expect_success_timeout),
              expectReadLen);
    if (HasFailure()) {
      break;
    }
    EXPECT_EQ(recvbuf[0], sendbuf[0]);
    EXPECT_EQ(recvbuf[6], sendbuf[6]);
    // For SOCK_STREAM, we validate peek across 2 packets with a single recv call.
    if (readLen == sizeof(recvbuf)) {
      EXPECT_EQ(recvbuf[8], sendbuf[0]);
      EXPECT_EQ(recvbuf[14], sendbuf[6]);
    }
    if (flags != MSG_PEEK) {
      torecv -= readLen;
    }
  }

  // Third peek on empty socket receive buffer, expect failure.
  //
  // As we expect failure, to keep the recv wait time minimal, we base it on the time taken for a
  // successful recv.
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, 1, MSG_PEEK, &addr, &addrlen,
                            socketType, success_rcv_duration * 10),
            0);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetSocket, NetSocketTest, ::testing::Values(SOCK_DGRAM, SOCK_STREAM));

TEST_P(SocketKindTest, IoctlIndexNameLookupRoundTrip) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);

  // This test assumes index 1 is bound to a valid interface. In Fuchsia's test environment (the
  // component executing this test), 1 is always bound to "lo".
  struct ifreq ifr_iton = {
      .ifr_ifindex = 1,
  };
  // Set ifr_name to random chars to test ioctl correctly sets null terminator.
  memset(ifr_iton.ifr_name, 0xdead, IFNAMSIZ);
  ASSERT_EQ(strnlen(ifr_iton.ifr_name, IFNAMSIZ), (size_t)IFNAMSIZ);
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), 0) << strerror(errno);
  ASSERT_LT(strnlen(ifr_iton.ifr_name, IFNAMSIZ), (size_t)IFNAMSIZ);

  struct ifreq ifr_ntoi;
  strncpy(ifr_ntoi.ifr_name, ifr_iton.ifr_name, IFNAMSIZ);
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFINDEX, &ifr_ntoi), 0) << strerror(errno);
  EXPECT_EQ(ifr_ntoi.ifr_ifindex, 1);

  struct ifreq ifr_ntoi_err;
  memset(ifr_ntoi_err.ifr_name, 0xdead, IFNAMSIZ);
  // Although the first few bytes of ifr_name contain the correct name, there is no null terminator
  // and the remaining bytes are gibberish, should match no interfaces.
  memcpy(ifr_ntoi_err.ifr_name, ifr_iton.ifr_name, strnlen(ifr_iton.ifr_name, IFNAMSIZ));
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFINDEX, &ifr_ntoi_err), -1);
  EXPECT_EQ(errno, ENODEV) << strerror(errno);
}

TEST_P(SocketKindTest, IoctlIndexToNameNotFound) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);
  // Invalid ifindex "-1" should match no interfaces.
  struct ifreq ifr_iton = {
      .ifr_ifindex = -1,
  };
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), -1);
  EXPECT_EQ(errno, ENODEV) << strerror(errno);
}

TEST_P(SocketKindTest, IoctlNameToIndexNotFound) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);
  // Emtpy name should match no interface.
  struct ifreq ifr_ntoi = {
      .ifr_name = 0,
  };
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFINDEX, &ifr_ntoi), -1);
  EXPECT_EQ(errno, ENODEV) << strerror(errno);
}

TEST(SocketKindTest, IoctlNameIndexLookupForNonSocketFd) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open("/", O_RDONLY | O_DIRECTORY))) << strerror(errno);

  struct ifreq ifr_iton = {
      .ifr_ifindex = 1,
  };
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), -1);
  EXPECT_EQ(errno, ENOTTY) << strerror(errno);

  struct ifreq ifr_ntoi;
  strcpy(ifr_ntoi.ifr_name, "loblah");
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFINDEX, &ifr_ntoi), -1);
  EXPECT_EQ(errno, ENOTTY) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetSocket, SocketKindTest,
                         ::testing::Combine(::testing::Values(AF_INET, AF_INET6),
                                            ::testing::Values(SOCK_DGRAM, SOCK_STREAM)),
                         socketKindToString);

using DomainProtocol = std::tuple<int, int>;
class IcmpSocketTest : public ::testing::TestWithParam<DomainProtocol> {};

TEST_P(IcmpSocketTest, GetSockoptSoProtocol) {
#if !defined(__Fuchsia__)
  if (!IsRoot()) {
    GTEST_SKIP() << "This test requires root";
  }
#endif
  auto const& [domain, protocol] = GetParam();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(domain, SOCK_DGRAM, protocol))) << strerror(errno);

  int opt;
  socklen_t optlen = sizeof(opt);
  EXPECT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_PROTOCOL, &opt, &optlen), 0) << strerror(errno);
  EXPECT_EQ(opt, protocol);
}

INSTANTIATE_TEST_SUITE_P(NetSocket, IcmpSocketTest,
                         ::testing::Values(std::make_pair(AF_INET, IPPROTO_ICMP),
                                           std::make_pair(AF_INET6, IPPROTO_ICMPV6)));

TEST(NetDatagramTest, PingIpv4LoopbackAddresses) {
  const char msg[] = "hello";
  char addrbuf[INET_ADDRSTRLEN];
  std::array<int, 5> sampleAddrOctets = {0, 1, 100, 200, 255};
  for (auto i : sampleAddrOctets) {
    for (auto j : sampleAddrOctets) {
      for (auto k : sampleAddrOctets) {
        // Skip the subnet and broadcast addresses.
        if ((i == 0 && j == 0 && k == 0) || (i == 255 && j == 255 && k == 255)) {
          continue;
        }
        // loopback_addr = 127.i.j.k
        struct in_addr loopback_sin_addr = {
            .s_addr = htonl((127 << 24) + (i << 16) + (j << 8) + k),
        };
        const char* loopback_addrstr =
            inet_ntop(AF_INET, &loopback_sin_addr, addrbuf, sizeof(addrbuf));
        ASSERT_NE(nullptr, loopback_addrstr);

        fbl::unique_fd recvfd;
        ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        struct sockaddr_in rcv_addr = {
            .sin_family = AF_INET,
            .sin_addr = loopback_sin_addr,
        };
        ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const struct sockaddr*>(&rcv_addr),
                       sizeof(rcv_addr)),
                  0)
            << "recvaddr=" << loopback_addrstr << ": " << strerror(errno);

        socklen_t rcv_addrlen = sizeof(rcv_addr);
        ASSERT_EQ(
            getsockname(recvfd.get(), reinterpret_cast<struct sockaddr*>(&rcv_addr), &rcv_addrlen),
            0)
            << strerror(errno);
        ASSERT_EQ(sizeof(rcv_addr), rcv_addrlen);

        fbl::unique_fd sendfd;
        ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        struct sockaddr_in sendto_addr = {
            .sin_family = AF_INET,
            .sin_port = rcv_addr.sin_port,
            .sin_addr = loopback_sin_addr,
        };
        ASSERT_EQ(sendto(sendfd.get(), msg, sizeof(msg), 0, (struct sockaddr*)&sendto_addr,
                         sizeof(sendto_addr)),
                  (ssize_t)sizeof(msg))
            << "sendtoaddr=" << loopback_addrstr << ": " << strerror(errno);
        EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

        struct pollfd pfd = {
            .fd = recvfd.get(),
            .events = POLLIN,
        };
        int n = poll(&pfd, 1, kTimeout);
        ASSERT_GE(n, 0) << strerror(errno);
        ASSERT_EQ(n, 1);
        char buf[sizeof(msg) + 1] = {};
        ASSERT_EQ(read(recvfd.get(), buf, sizeof(buf)), (ssize_t)sizeof(msg)) << strerror(errno);
        ASSERT_STREQ(buf, msg);

        EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
      }
    }
  }
}

}  // namespace
