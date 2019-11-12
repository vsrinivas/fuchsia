// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/uio.h>

#include <array>
#include <future>
#include <thread>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "util.h"

namespace {

TEST(LocalhostTest, DatagramSocketIgnoresMsgWaitAll) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  ASSERT_EQ(bind(recvfd.get(), (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  ASSERT_EQ(recvfrom(recvfd.get(), nullptr, 0, MSG_WAITALL, nullptr, nullptr), -1);
  ASSERT_EQ(errno, EAGAIN) << strerror(errno);

  ASSERT_EQ(close(recvfd.release()), 0) << strerror(errno);
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
  EXPECT_EQ(setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, "loblahblahblah", sizeof(set_dev_unknown)), -1);
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
  int s;

  // No raw INET sockets.
  ASSERT_EQ(s = socket(AF_INET, SOCK_RAW, 0), -1);
  int expected = EPROTONOSUPPORT;
  ASSERT_EQ(errno, expected) << strerror(errno);

  // No packet sockets.
  ASSERT_EQ(s = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)), -1);
  expected =
#if defined(__Fuchsia__)
      EPFNOSUPPORT;
#else
      // Creating an AF_PACKET socket typically requires superuser privilege, so this should fail
      // outside of Fuchsia with a different errno.
      EPERM;
#endif
  ASSERT_EQ(errno, expected) << strerror(errno);
}

TEST(LocalhostTest, IP_ADD_MEMBERSHIP_INADDR_ANY) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  ip_mreqn param = {};
  param.imr_ifindex = 1;
  param.imr_multiaddr.s_addr = inet_addr("224.0.2.1");
  param.imr_address.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(setsockopt(s, SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0) << strerror(errno);
}

struct SockOption {
  int level;
  int option;
};

constexpr int INET_ECN_MASK = 3;

struct SocketKind {
  std::string description;
  int domain;
  int type;
  int protocol;
};

constexpr int kSockOptOn = 1;
constexpr int kSockOptOff = 0;

class SocketOptsTest : public ::testing::TestWithParam<SocketKind> {
 protected:
  SocketOptsTest() {
    std::cout << "Testing with " << GetParam().description.c_str() << " socket." << std::endl;
  }

  fbl::unique_fd NewSocket() const {
    SocketKind s = GetParam();
    return fbl::unique_fd(socket(s.domain, s.type, s.protocol));
  }

  bool IsTCP() const {
    return GetParam().protocol == IPPROTO_TCP;
  }

  bool IsIPv6() const {
    return GetParam().domain == AF_INET6;
  }

  SockOption GetTOSOption() {
    if (IsIPv6()) {
      return {.level = IPPROTO_IPV6, .option = IPV6_TCLASS};
    }
    return {.level = IPPROTO_IP, .option = IP_TOS};
  }

  SockOption GetMcastLoopOption() {
    if (IsIPv6()) {
      return {.level = IPPROTO_IPV6, .option = IPV6_MULTICAST_LOOP};
    }
    return {.level = IPPROTO_IP, .option = IP_MULTICAST_LOOP};
  }

  SockOption GetMcastTTLOption() {
    if (IsIPv6()) {
      return {.level = IPPROTO_IPV6, .option = IPV6_MULTICAST_HOPS};
    }
    return {.level = IPPROTO_IP, .option = IP_MULTICAST_TTL};
  }

  SockOption GetMcastIfOption() {
    if (IsIPv6()) {
      return {.level = IPPROTO_IPV6, .option = IPV6_MULTICAST_IF};
    }
    return {.level = IPPROTO_IP, .option = IP_MULTICAST_IF};
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
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidLargeTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = 256;
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), -1);
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, InvalidNegativeTtl) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = NewSocket()) << strerror(errno);

  int set = -2;
  socklen_t set_sz = sizeof(set);
  EXPECT_EQ(setsockopt(s.get(), IPPROTO_IP, IP_TTL, &set, set_sz), -1);
  EXPECT_EQ(errno, EINVAL);
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
  if (GetParam().domain == AF_INET) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, nullptr, set_sz), -1);
    EXPECT_EQ(errno, EFAULT) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, nullptr, set_sz), 0) << strerror(errno);
  }
  socklen_t get_sz = sizeof(int);
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, nullptr, &get_sz), -1);
  EXPECT_EQ(errno, EFAULT);
  int get = -1;
  EXPECT_EQ(getsockopt(s.get(), t.level, t.option, &get, nullptr), -1);
  EXPECT_EQ(errno, EFAULT);
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
  if (GetParam().domain == AF_INET) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL);
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
  if (GetParam().protocol == IPPROTO_TCP
#ifdef __linux__
      // gvisor-netstack`s implemention of setsockopt(..IPV6_TCLASS..)
      // clears the ECN bits from the TCLASS value. This keeps gvisor
      // in parity with the Linux test-hosts that run a custom kernel.
      // But that is not the behavior of vanilla Linux kernels.
      // This ifdef can be removed when we migrate away from gvisor-netstack.
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
  if (GetParam().domain == AF_INET) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL);
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
    if (GetParam().domain == AF_INET) {
      EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, i), 0) << strerror(errno);
      expect_tos = set;
      expect_sz = sizeof(uint8_t);
    } else {
      EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, i), -1);
      EXPECT_EQ(errno, EINVAL);
      expect_tos = kDefaultTOS;
      expect_sz = i;
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
  if (GetParam().domain == AF_INET) {
    expect = static_cast<uint8_t>(set);
    if (GetParam().protocol == IPPROTO_TCP) {
      expect &= ~INET_ECN_MASK;
    }
  } else {
    // On IPv6 TCLASS, setting -1 has the effect of resetting the
    // TrafficClass.
    expect = 0;
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
  if (GetParam().domain == AF_INET) {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), 0) << strerror(errno);
    expect = static_cast<uint8_t>(set);
    if (GetParam().protocol == IPPROTO_TCP) {
      expect &= ~INET_ECN_MASK;
    }
  } else {
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &set, set_sz), -1);
    EXPECT_EQ(errno, EINVAL);
    expect = 0;
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
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOffChar,
                         sizeof(kSockOptOffChar)), -1);
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
    EXPECT_EQ(setsockopt(s.get(), t.level, t.option, &kSockOptOnChar,
                         sizeof(kSockOptOnChar)), -1);
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
  EXPECT_EQ(errno, EINVAL);

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
  EXPECT_EQ(errno, EINVAL);

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

INSTANTIATE_TEST_SUITE_P(LocalhostTest, SocketOptsTest,
                         ::testing::Values(
                              SocketKind{"IPv4 UDP", AF_INET, SOCK_DGRAM, IPPROTO_UDP},
                              SocketKind{"IPv4 TCP", AF_INET, SOCK_STREAM, IPPROTO_TCP},
                              SocketKind{"IPv6 UDP", AF_INET6, SOCK_DGRAM, IPPROTO_UDP},
                              SocketKind{"IPv6 TCP", AF_INET6, SOCK_STREAM, IPPROTO_TCP}));

TEST(LocalhostTest, IP_MULTICAST_IF_ifindex) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  ip_mreqn param_in = {};
  param_in.imr_ifindex = 1;
  ASSERT_EQ(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  in_addr param_out = {};
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &param_in, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  EXPECT_EQ(param_out.s_addr, INADDR_ANY);
}

TEST(LocalhostTest, IP_MULTICAST_IF_ifaddr) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  ip_mreqn param_in = {};
  param_in.imr_address.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  in_addr param_out = {};
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &param_out, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  EXPECT_EQ(param_out.s_addr, param_in.imr_address.s_addr);
}

TEST(LocalhostTest, IPV6_MULTICAST_IF_ifindex) {
  int s;
  ASSERT_GE(s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  int param_in = 1;
  ASSERT_EQ(setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  int param_out = 0;
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &param_out, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  EXPECT_EQ(param_out, 1);
}

class ReuseTest
    : public ::testing::TestWithParam<::testing::tuple<int /* type */, in_addr_t /* address */>> {};

TEST_P(ReuseTest, AllowsAddressReuse) {
  const int on = true;

  int s1;
  ASSERT_GE(s1 = socket(AF_INET, ::testing::get<0>(GetParam()), 0), 0) << strerror(errno);

  ASSERT_EQ(setsockopt(s1, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ::testing::get<1>(GetParam());
  ASSERT_EQ(bind(s1, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(s1, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  int s2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s2, 0) << strerror(errno);

  ASSERT_EQ(setsockopt(s2, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);

  ASSERT_EQ(bind(s2, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(LocalhostTest, ReuseTest,
                         ::testing::Combine(::testing::Values(SOCK_DGRAM, SOCK_STREAM),
                                            ::testing::Values(htonl(INADDR_LOOPBACK),
                                                              inet_addr("224.0.2.1"))));

TEST(LocalhostTest, Accept) {
  int serverfd;
  ASSERT_GE(serverfd = socket(AF_INET6, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in6 serveraddr = {};
  serveraddr.sin6_family = AF_INET6;
  serveraddr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  socklen_t serveraddrlen = sizeof(serveraddr);
  ASSERT_EQ(bind(serverfd, (sockaddr*)&serveraddr, serveraddrlen), 0) << strerror(errno);
  ASSERT_EQ(getsockname(serverfd, (sockaddr*)&serveraddr, &serveraddrlen), 0) << strerror(errno);
  ASSERT_EQ(serveraddrlen, sizeof(serveraddr));
  ASSERT_EQ(listen(serverfd, 1), 0) << strerror(errno);

  int clientfd;
  ASSERT_GE(clientfd = socket(AF_INET6, SOCK_STREAM, 0), 0) << strerror(errno);
  ASSERT_EQ(connect(clientfd, (sockaddr*)&serveraddr, serveraddrlen), 0) << strerror(errno);

  struct sockaddr_in connaddr;
  socklen_t connaddrlen = sizeof(connaddr);
  int connfd = accept(serverfd, (sockaddr*)&connaddr, &connaddrlen);
  ASSERT_GE(connfd, 0) << strerror(errno);
  ASSERT_GT(connaddrlen, sizeof(connaddr));
}

TEST(LocalhostTest, ConnectAFMismatchINET) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  addr.sin6_port = htons(1337);
  EXPECT_EQ(connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);
  EXPECT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
  EXPECT_EQ(close(s), 0) << strerror(errno);
}

TEST(LocalhostTest, ConnectAFMismatchINET6) {
  int s;
  ASSERT_GE(s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(1337);
  EXPECT_EQ(connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  EXPECT_EQ(close(s), 0) << strerror(errno);
}

TEST(NetStreamTest, ConnectTwice) {
  fbl::unique_fd client, listener;
  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);
  ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

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

TEST(LocalhostTest, GetAddrInfo) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

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
  int sockfd;
  ASSERT_GE(sockfd = socket(AF_INET6, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr sa;
  socklen_t len = sizeof(sa);
  ASSERT_EQ(getsockname(sockfd, &sa, &len), 0) << strerror(errno);
  ASSERT_GT(len, sizeof(sa));
  ASSERT_EQ(sa.sa_family, AF_INET6);
}

TEST(NetStreamTest, PeerClosedPOLLOUT) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  EXPECT_EQ(bind(acptfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(acptfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));

  EXPECT_EQ(listen(acptfd, 1), 0) << strerror(errno);

  int clientfd;
  EXPECT_GE(clientfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);
  EXPECT_EQ(connect(clientfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  int connfd;
  EXPECT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd), 0) << strerror(errno);

  fill_stream_send_buf(connfd, clientfd);

  EXPECT_EQ(close(clientfd), 0) << strerror(errno);

  struct pollfd pfd = {};
  pfd.fd = connfd;
  pfd.events = POLLOUT;
  int n = poll(&pfd, 1, kTimeout);
  EXPECT_GE(n, 0) << strerror(errno);
  EXPECT_EQ(n, 1);
#if defined(__linux__)
  EXPECT_EQ(pfd.revents, POLLOUT | POLLERR | POLLHUP);
#else
  // TODO(crbug.com/1005300): we should check that revents is exactly
  // OUT|ERR|HUP. Currently, this is a bit racey, and we might see OUT and HUP
  // but not ERR due to the hack in socket_server.go which references this same
  // bug.
  EXPECT_TRUE(pfd.revents & (POLLOUT | POLLHUP)) << pfd.revents;
#endif

  EXPECT_EQ(close(connfd), 0) << strerror(errno);
}

TEST(NetStreamTest, BlockingAcceptWrite) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd;
  ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(connfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

class TimeoutSockoptsTest : public ::testing::TestWithParam<int /* optname */> {};

TEST_P(TimeoutSockoptsTest, TimeoutSockopts) {
  int optname = GetParam();
  ASSERT_TRUE(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO);

  int socket_fd;
  ASSERT_GE(socket_fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  // Set the timeout.
  const struct timeval expected_tv = {
      .tv_sec = 39,
      // NB: for some reason, Linux's resolution is limited to 4ms.
      .tv_usec = 504000,
  };
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, optname, &expected_tv, sizeof(expected_tv)), 0)
      << strerror(errno);

  // Reading it back should work.
  struct timeval actual_tv;
  socklen_t optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, optname, &actual_tv, &optlen), 0) << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, expected_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, expected_tv.tv_usec);

  // Reading it back with too much space should work and set optlen.
  char actual_tv2_buffer[sizeof(struct timeval) * 2];
  memset(&actual_tv2_buffer, 44, sizeof(actual_tv2_buffer));
  optlen = sizeof(actual_tv2_buffer);
  struct timeval* actual_tv2 = (struct timeval*)&actual_tv2_buffer;
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, optname, actual_tv2, &optlen), 0) << strerror(errno);
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
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, optname, &actual_tv, &optlen),
#if defined(__linux__)
            0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv) - 7);
  struct timeval linux_expected_tv = expected_tv;
  memset(((char*)&linux_expected_tv) + optlen, 0, sizeof(linux_expected_tv) - optlen);
  EXPECT_EQ(memcmp(&actual_tv, &linux_expected_tv, sizeof(actual_tv)), 0);
#else
            -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
#endif

  // Setting it without enough space should fail gracefully.
  optlen = sizeof(expected_tv) - 1;  // Not big enough.
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, optname, &expected_tv, optlen), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  // Setting it with too much space should work okay.
  const struct timeval expected_tv2 = {
      .tv_sec = 42,
      .tv_usec = 0,
  };
  optlen = sizeof(expected_tv2) + 1;  // Too big.
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, optname, &expected_tv2, optlen), 0)
      << strerror(errno);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, optname, &actual_tv, &optlen), 0) << strerror(errno);
  EXPECT_EQ(optlen, sizeof(expected_tv2));
  EXPECT_EQ(actual_tv.tv_sec, expected_tv2.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, expected_tv2.tv_usec);

  // Disabling rcvtimeo by setting it to zero should work.
  const struct timeval zero_tv = {
      .tv_sec = 0,
      .tv_usec = 0,
  };
  optlen = sizeof(zero_tv);
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, optname, &zero_tv, optlen), 0) << strerror(errno);

  // Reading back the disabled timeout should work.
  memset(&actual_tv, 55, sizeof(actual_tv));
  optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, optname, &actual_tv, &optlen), 0) << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, zero_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, zero_tv.tv_usec);
}

INSTANTIATE_TEST_SUITE_P(NetStreamTest, TimeoutSockoptsTest,
                         ::testing::Values(SO_RCVTIMEO, SO_SNDTIMEO));

const int32_t kConnections = 100;

TEST(NetStreamTest, BlockingAcceptWriteMultiple) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, kConnections), 0) << strerror(errno);

  std::thread thrd[kConnections];
  std::string out[kConnections];
  const char* msg = "hello";

  for (int i = 0; i < kConnections; i++) {
    thrd[i] = std::thread(StreamConnectRead, &addr, &out[i], ntfyfd[1]);
  }

  for (int i = 0; i < kConnections; i++) {
    struct pollfd pfd = {acptfd, POLLIN, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int connfd;
    ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

    ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
    ASSERT_EQ(0, close(connfd));

    ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  }

  for (int i = 0; i < kConnections; i++) {
    thrd[i].join();
    EXPECT_STREQ(msg, out[i].c_str());
  }

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetStreamTest, BlockingAcceptDupWrite) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd;
  ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  int dupfd;
  ASSERT_GE(dupfd = dup(connfd), 0) << strerror(errno);
  ASSERT_EQ(0, close(connfd));

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(dupfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(dupfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptWrite) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd;
  ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(connfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingAcceptDupWrite) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd;
  ASSERT_GE(connfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  int dupfd;
  ASSERT_GE(dupfd = dup(connfd), 0) << strerror(errno);
  ASSERT_EQ(0, close(connfd));

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(dupfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(dupfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectWrite) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  std::string out;
  std::thread thrd(StreamAcceptRead, acptfd, &out, ntfyfd[1]);

  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  int ret;
  EXPECT_EQ(ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    struct pollfd pfd = {connfd, POLLOUT, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(0, getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen));
    ASSERT_EQ(0, val);
  }

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(connfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetStreamTest, NonBlockingConnectRead) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ASSERT_EQ(listen(acptfd, 10), 0) << strerror(errno);

  const char* msg = "hello";
  std::thread thrd(StreamAcceptWrite, acptfd, msg, ntfyfd[1]);

  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  int ret;
  EXPECT_EQ(ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    // Note: the success of connection can be detected with POLLOUT, but
    // we use POLLIN here to wait until some data is written by the peer.
    struct pollfd pfd = {connfd, POLLIN, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(0, getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen));
    ASSERT_EQ(0, val);

    std::string out;
    int n;
    char buf[4096];
    while ((n = read(connfd, buf, sizeof(buf))) > 0) {
      out.append(buf, n);
    }
    ASSERT_EQ(0, close(connfd));

    ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
    thrd.join();

    EXPECT_STREQ(msg, out.c_str());

    EXPECT_EQ(close(acptfd), 0) << strerror(errno);
    EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
    EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
  }
}

TEST(NetStreamTest, NonBlockingConnectRefused) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  // No listen() on acptfd.

  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  int ret;
  EXPECT_EQ(ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << strerror(errno);

    struct pollfd pfd = {connfd, POLLOUT, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(0, getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen));
    ASSERT_EQ(ECONNREFUSED, val);
  }

  EXPECT_EQ(close(connfd), 0) << strerror(errno);
  EXPECT_EQ(close(acptfd), 0) << strerror(errno);
}

TEST(NetStreamTest, GetTcpInfo) {
  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  tcp_info info;
  socklen_t info_len = sizeof(tcp_info);
  ASSERT_GE(getsockopt(connfd, SOL_TCP, TCP_INFO, (void*)&info, &info_len), 0) << strerror(errno);
  ASSERT_EQ(sizeof(tcp_info), info_len);

  ASSERT_EQ(0, close(connfd));
}

// Test socket reads on disconnected stream sockets.
TEST(NetStreamTest, DisconnectedRead) {
  int socketfd;
  ASSERT_GE(socketfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);
  struct timeval tv = {};
  // Use minimal non-zero timeout as we expect the blocking recv to return before it
  // actually starts reading. Without the timeout, the test could deadlock on a blocking
  // recv, when the underlying code is broken.
  tv.tv_usec = 1u;
  EXPECT_EQ(setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0) << strerror(errno);
  // Test blocking socket read.
  EXPECT_EQ(recvfrom(socketfd, nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd, nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);

  // Test non blocking socket read.
  int flags;
  EXPECT_GE(flags = fcntl(socketfd, F_GETFL, 0), 0) << strerror(errno);
  EXPECT_EQ(fcntl(socketfd, F_SETFL, flags | O_NONBLOCK), 0);
  EXPECT_EQ(recvfrom(socketfd, nullptr, 0, 0, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  // Test with MSG_PEEK.
  EXPECT_EQ(recvfrom(socketfd, nullptr, 0, MSG_PEEK, nullptr, nullptr), -1);
  EXPECT_EQ(errno, ENOTCONN) << strerror(errno);
  EXPECT_EQ(close(socketfd), 0) << strerror(errno);
}

TEST(NetStreamTest, Shutdown) {
  int listener;
  EXPECT_GE(listener = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  EXPECT_EQ(bind(listener, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(listener, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));
  EXPECT_EQ(listen(listener, 1), 0) << strerror(errno);

  int outbound;
  EXPECT_GE(outbound = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);
  // Wrap connect() in a future to enable a timeout.
  std::future<void> fut = std::async(std::launch::async, [outbound, addr]() {
    EXPECT_EQ(connect(outbound, (struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);
  });

  int inbound;
  EXPECT_GE(inbound = accept(listener, NULL, NULL), 0) << strerror(errno);

  // Wait for connect() to finish.
  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);

  EXPECT_EQ(shutdown(inbound, SHUT_WR), 0) << strerror(errno);

  struct pollfd fds = {};
  fds.fd = outbound;
  fds.events = POLLRDHUP;
  EXPECT_EQ(poll(&fds, 1, kTimeout), 1) << strerror(errno);
  EXPECT_EQ(fds.revents, POLLRDHUP);

  EXPECT_EQ(close(listener), 0) << strerror(errno);
  EXPECT_EQ(close(outbound), 0) << strerror(errno);
  EXPECT_EQ(close(inbound), 0) << strerror(errno);
}

enum sendMethod {
  WRITE,
  WRITEV,
  SEND,
  SENDTO,
  SENDMSG,
};

enum closeSocket {
  CLIENT,
  SERVER,
};

class SendSocketTest : public ::testing::TestWithParam<std::tuple<sendMethod, closeSocket>> {};

TEST_P(SendSocketTest, CloseWhileSending) {
  sendMethod whichMethod = std::get<0>(GetParam());
  closeSocket whichSocket = std::get<1>(GetParam());

  fbl::unique_fd client, server;
  {
    fbl::unique_fd listener;
    ASSERT_TRUE(listener = fbl::unique_fd(socket(AF_INET, SOCK_STREAM, 0))) << strerror(errno);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

  // Fill the send buffer of the client socket to trigger write to wait.
  fill_stream_send_buf(client.get(), server.get());

  // In the process of writing to the socket, close its peer socket with outstanding data to read,
  // ECONNRESET is expected; write to the socket after it's closed, EPIPE is expected.
  const auto fut = std::async(std::launch::async, [&]() {
    char buf[16];
    auto do_send = [&]() {
      switch (whichMethod) {
        case sendMethod::WRITE: {
          return write(client.get(), buf, sizeof(buf));
        }
        case sendMethod::WRITEV: {
          struct iovec iov = {};
          iov.iov_base = (void*)buf;
          iov.iov_len = sizeof(buf);
          return writev(client.get(), &iov, 1);
        }
        case sendMethod::SEND: {
          return send(client.get(), buf, sizeof(buf), 0);
        }
        case sendMethod::SENDTO: {
          return sendto(client.get(), buf, sizeof(buf), 0, nullptr, 0);
        }
        case sendMethod::SENDMSG: {
          struct iovec iov = {};
          iov.iov_base = (void*)buf;
          iov.iov_len = sizeof(buf);
          struct msghdr msg = {};
          msg.msg_iov = &iov;
          msg.msg_iovlen = 1;
          return sendmsg(client.get(), &msg, 0);
        }
        default:
          ADD_FAILURE() << "unsupported test parameter";
          return 0l;
      }
    };

    EXPECT_EQ(do_send(), -1);
    switch (whichSocket) {
      case closeSocket::CLIENT: {
        // On Linux, the pending I/O call is allowed to complete in spite of its argument having
        // been closed. See below for more detail.
#if defined(__linux__)
        EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
#else
        EXPECT_EQ(errno, EBADF) << strerror(errno);
#endif
        break;
      }
      case closeSocket::SERVER: {
        EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
        break;
      }
      default:
        ADD_FAILURE() << "unsupported test parameter";
    }

    // Linux generates SIGPIPE when the peer on a stream-oriented socket has closed the connection.
    // send{,to,msg} support the MSG_NOSIGNAL flag to suppress this behaviour, but write and writev
    // do not. We only expect this during the second attempt, so we remove the default signal
    // handler, make our attempt, and then restore it.
    {
#if defined(__linux__)
      struct sigaction act = {};
      act.sa_handler = SIG_IGN;

      struct sigaction oldact;
      ASSERT_EQ(sigaction(SIGPIPE, &act, &oldact), 0) << strerror(errno);

      auto undo = fbl::MakeAutoCall(
          [&]() { ASSERT_EQ(sigaction(SIGPIPE, &oldact, nullptr), 0) << strerror(errno); });
#endif
      ASSERT_EQ(do_send(), -1);
    }

    // The socket writes after the the peer socket is closed.
    switch (whichSocket) {
      case closeSocket::CLIENT: {
        EXPECT_EQ(errno, EBADF) << strerror(errno);
        break;
      }
      case closeSocket::SERVER: {
        EXPECT_EQ(errno, EPIPE) << strerror(errno);
        break;
      }
      default:
        ADD_FAILURE() << "unsupported test parameter";
    }
  });
  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);

  switch (whichSocket) {
    case closeSocket::CLIENT: {
      EXPECT_EQ(close(client.get()), 0) << strerror(errno);
      // This is weird! The I/O is allowed to proceed past the close call - at least on Linux.
      // Therefore we have to fallthrough to closing the server, which will actually unblock the
      // future.
      //
      // In Fuchsia, fdio will eagerly clean up all the resources associated with the file
      // descriptor.
#if defined(__linux__)
      EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
#else
      break;
#endif
    }
    case closeSocket::SERVER: {
      EXPECT_EQ(close(server.get()), 0) << strerror(errno);
      break;
    }
    default:
      ADD_FAILURE() << "unsupported test parameter";
  }

  EXPECT_EQ(fut.wait_for(std::chrono::milliseconds(50)), std::future_status::ready);
}

// TODO(35619): deflake and re-enable this test.
INSTANTIATE_TEST_SUITE_P(
    DISABLED_NetStreamTest, SendSocketTest,
    ::testing::Combine(::testing::Values(sendMethod::WRITE, sendMethod::WRITEV, sendMethod::SEND,
                                         sendMethod::SENDTO, sendMethod::SENDMSG),
                       ::testing::Values(closeSocket::CLIENT, closeSocket::SERVER)));

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
    ssize_t readlen;
    memset(buf, 0, len);
    readlen = recvfrom(recvfd, buf, len, flags, nullptr, nullptr);
    return readlen;
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
      // destructor. Calling close() on recvfd when the async task is blocked on recv(),
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

class DatagramSendTest : public ::testing::TestWithParam<enum sendMethod> {};

TEST_P(DatagramSendTest, DatagramSend) {
  enum sendMethod sendMethod = GetParam();
  int recvfd;
  ASSERT_GE(recvfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  EXPECT_EQ(bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(recvfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));

  const char* msg = "hello";
  char recvbuf[32] = {};
  struct iovec iov = {};
  iov.iov_base = (void*)msg;
  iov.iov_len = strlen(msg);
  struct msghdr msghdr = {};
  msghdr.msg_iov = &iov;
  msghdr.msg_iovlen = 1;
  msghdr.msg_name = &addr;
  msghdr.msg_namelen = addrlen;

  int sendfd;
  EXPECT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);
  switch (sendMethod) {
    case SENDTO: {
      EXPECT_EQ(sendto(sendfd, msg, strlen(msg), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)strlen(msg))
          << strerror(errno);
      break;
    }
    case SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd, &msghdr, 0), (ssize_t)strlen(msg)) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant " << sendMethod;
      break;
    }
  }
  auto expect_success_timeout = std::chrono::milliseconds(kTimeout);
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen,
                            SOCK_DGRAM, expect_success_timeout),
            (ssize_t)strlen(msg));
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_STREQ(recvbuf, msg);
  EXPECT_EQ(close(sendfd), 0) << strerror(errno);

  // sendto/sendmsg on connected sockets does accept sockaddr input argument and
  // also lets the dest sockaddr be overridden from what was passed for connect.
  EXPECT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);
  EXPECT_EQ(connect(sendfd, (struct sockaddr*)&addr, addrlen), 0) << strerror(errno);
  switch (sendMethod) {
    case SENDTO: {
      EXPECT_EQ(sendto(sendfd, msg, strlen(msg), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)strlen(msg))
          << strerror(errno);
      break;
    }
    case SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd, &msghdr, 0), (ssize_t)strlen(msg)) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant " << sendMethod;
      break;
    }
  }
  EXPECT_EQ(asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen,
                            SOCK_DGRAM, expect_success_timeout),
            (ssize_t)strlen(msg));
  EXPECT_STREQ(recvbuf, msg);

  // Test sending to an address that is different from what we're connected to.
  addr.sin_port = htons(ntohs(addr.sin_port) + 1);
  switch (sendMethod) {
    case SENDTO: {
      EXPECT_EQ(sendto(sendfd, msg, strlen(msg), 0, (struct sockaddr*)&addr, addrlen),
                (ssize_t)strlen(msg))
          << strerror(errno);
      break;
    }
    case SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd, &msghdr, 0), (ssize_t)strlen(msg)) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant " << sendMethod;
      break;
    }
  }
  // Expect blocked receiver and try to recover it by sending a packet to the
  // original connected sockaddr.
  addr.sin_port = htons(ntohs(addr.sin_port) - 1);
  // As we expect failure, to keep the recv wait time minimal, we base it on the time taken for a
  // successful recv.
  EXPECT_EQ(asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen,
                            SOCK_DGRAM, success_rcv_duration * 10),
            (ssize_t)0);

  EXPECT_EQ(close(sendfd), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSendTest, ::testing::Values(SENDTO, SENDMSG));

TEST(NetDatagramTest, DatagramConnectWrite) {
  int recvfd;
  ASSERT_GE(recvfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  ASSERT_EQ(bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::string out;
  std::thread thrd(DatagramRead, recvfd, &out, &addr, &addrlen, ntfyfd[1], kTimeout);

  const char* msg = "hello";

  int sendfd;
  ASSERT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);
  ASSERT_EQ(0, connect(sendfd, (struct sockaddr*)&addr, addrlen));
  ASSERT_EQ((ssize_t)strlen(msg), write(sendfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramPartialRecv) {
  int recvfd;
  ASSERT_GE(recvfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  ASSERT_EQ(bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);

  const char kTestMsg[] = "hello";
  const int kTestMsgSize = sizeof(kTestMsg);

  int sendfd;
  ASSERT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);
  ASSERT_EQ(kTestMsgSize,
            sendto(sendfd, kTestMsg, kTestMsgSize, 0, reinterpret_cast<sockaddr*>(&addr), addrlen));

  char recv_buf[kTestMsgSize];

  // Read only first 2 bytes of the message. recv() is expected to discard the
  // rest.
  const int kPartialReadSize = 2;

  struct iovec iov = {};
  iov.iov_base = recv_buf;
  iov.iov_len = kPartialReadSize;
  struct msghdr msg = {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  int recv_result = recvmsg(recvfd, &msg, 0);
  ASSERT_EQ(kPartialReadSize, recv_result);
  ASSERT_EQ(std::string(kTestMsg, kPartialReadSize), std::string(recv_buf, kPartialReadSize));
  EXPECT_EQ(MSG_TRUNC, msg.msg_flags);

  // Send the second packet.
  ASSERT_EQ(kTestMsgSize,
            sendto(sendfd, kTestMsg, kTestMsgSize, 0, reinterpret_cast<sockaddr*>(&addr), addrlen));

  // Read the whole packet now.
  recv_buf[0] = 0;
  iov.iov_len = sizeof(recv_buf);
  recv_result = recvmsg(recvfd, &msg, 0);
  ASSERT_EQ(kTestMsgSize, recv_result);
  ASSERT_EQ(std::string(kTestMsg, kTestMsgSize), std::string(recv_buf, kTestMsgSize));
  EXPECT_EQ(msg.msg_flags, 0);

  EXPECT_EQ(close(sendfd), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
}

// TODO port reuse

// DatagramSendtoRecvfrom tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

TEST(NetDatagramTest, DatagramSendtoRecvfrom) {
  int recvfd;
  ASSERT_GE(recvfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  ASSERT_EQ(bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWrite, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd;
  ASSERT_GE(sendfd = socket(AF_INET, SOCK_DGRAM, 0), 0) << strerror(errno);
  ASSERT_EQ(sendto(sendfd, msg, strlen(msg), 0, reinterpret_cast<struct sockaddr*>(&addr), addrlen),
            (ssize_t)strlen(msg))
      << strerror(errno);

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << strerror(errno);

  char buf[32];
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(
      recvfrom(sendfd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
      (ssize_t)strlen(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));

  char addrbuf[INET_ADDRSTRLEN], peerbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

// DatagramSendtoRecvfromV6 tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

TEST(NetDatagramTest, DatagramSendtoRecvfromV6) {
  int recvfd;
  ASSERT_GE(recvfd = socket(AF_INET6, SOCK_DGRAM, 0), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = IN6ADDR_LOOPBACK_INIT;

  ASSERT_EQ(bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWriteV6, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd;
  ASSERT_GE(sendfd = socket(AF_INET6, SOCK_DGRAM, 0), 0) << strerror(errno);
  ASSERT_EQ(sendto(sendfd, msg, strlen(msg), 0, reinterpret_cast<struct sockaddr*>(&addr), addrlen),
            (ssize_t)strlen(msg))
      << strerror(errno);

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << strerror(errno);

  char buf[32];
  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(
      recvfrom(sendfd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr*>(&peer), &peerlen),
      (ssize_t)strlen(msg))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));

  char addrbuf[INET6_ADDRSTRLEN], peerbuf[INET6_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin6_family, &addr.sin6_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[0]), 0) << strerror(errno);
  EXPECT_EQ(close(ntfyfd[1]), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV4) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  EXPECT_EQ(connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV6) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = IN6ADDR_ANY_INIT;

  EXPECT_EQ(connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectAnyV6MappedV4) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = {{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0}}};

  EXPECT_EQ(connect(fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  ASSERT_EQ(close(fd), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV4) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_UNSPEC;

  EXPECT_EQ(connect(fd, reinterpret_cast<const struct sockaddr*>(&addr),
                    offsetof(sockaddr_in, sin_family) + sizeof(addr.sin_family)),
            0)
      << strerror(errno);
  ASSERT_EQ(close(fd), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV6) {
  int fd;
  ASSERT_GE(fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_UNSPEC;

  EXPECT_EQ(connect(fd, reinterpret_cast<const struct sockaddr*>(&addr),
                    offsetof(sockaddr_in6, sin6_family) + sizeof(addr.sin6_family)),
            0)
      << strerror(errno);
  ASSERT_EQ(close(fd), 0) << strerror(errno);
}

// Note: we choose 100 because the max number of fds per process is limited to
// 256.
const int32_t kListeningSockets = 100;

TEST(NetStreamTest, MultipleListeningSockets) {
  int listenfd[kListeningSockets];
  int connfd[kListeningSockets];

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addrlen = sizeof(addr);

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_GE(listenfd[i] = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

    ASSERT_EQ(bind(listenfd[i], reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);

    ASSERT_EQ(listen(listenfd[i], 10), 0) << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(getsockname(listenfd[i], reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(addr));

    ASSERT_GE(connfd[i] = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

    ASSERT_EQ(connect(connfd[i], reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(0, close(connfd[i]));
    ASSERT_EQ(0, close(listenfd[i]));
  }
}

// Socket tests across multiple socket-types, SOCK_DGRAM, SOCK_STREAM.
class NetSocketTest : public ::testing::TestWithParam<int> {};

// Test MSG_PEEK
// MSG_PEEK : Peek into the socket receive queue without moving the contents from it.
//
// TODO(fxb.dev/33100): change this test to use recvmsg instead of recvfrom to exercise MSG_PEEK
// with scatter/gather.
TEST_P(NetSocketTest, SocketPeekTest) {
  int socketType = GetParam();
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addrlen = sizeof(addr);
  int sendfd;
  int recvfd;
  ssize_t expectReadLen = 0;
  char sendbuf[8] = {};
  char recvbuf[2 * sizeof(sendbuf)] = {};
  ssize_t sendlen = sizeof(sendbuf);

  ASSERT_GE(sendfd = socket(AF_INET, socketType, 0), 0) << strerror(errno);
  // Setup the sender and receiver sockets.
  switch (socketType) {
    case SOCK_STREAM: {
      int acptfd;
      EXPECT_GE(acptfd = socket(AF_INET, socketType, 0), 0) << strerror(errno);
      EXPECT_EQ(bind(acptfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
      EXPECT_EQ(getsockname(acptfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
          << strerror(errno);
      EXPECT_EQ(addrlen, sizeof(addr));
      EXPECT_EQ(listen(acptfd, 1), 0) << strerror(errno);
      EXPECT_EQ(connect(sendfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
      EXPECT_GE(recvfd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);
      EXPECT_EQ(close(acptfd), 0) << strerror(errno);
      // Expect to read both the packets in a single recv() call.
      expectReadLen = sizeof(recvbuf);
      break;
    }
    case SOCK_DGRAM: {
      EXPECT_GE(recvfd = socket(AF_INET, socketType, 0), 0) << strerror(errno);
      EXPECT_EQ(bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
      EXPECT_EQ(getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
          << strerror(errno);
      EXPECT_EQ(addrlen, sizeof(addr));
      // Expect to read single packet per recv() call.
      expectReadLen = sizeof(sendbuf);
      break;
    }
    default: {
      FAIL() << "unexpected test variant " << socketType;
    }
  }

  // This test sends 2 packets with known values and validates MSG_PEEK across the 2 packets.
  sendbuf[0] = 0xab;
  sendbuf[6] = 0xce;

  // send 2 separate packets and test peeking across
  EXPECT_EQ(sendto(sendfd, sendbuf, sizeof(sendbuf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), addrlen),
            sendlen)
      << strerror(errno);
  EXPECT_EQ(sendto(sendfd, sendbuf, sizeof(sendbuf), 0,
                   reinterpret_cast<const struct sockaddr*>(&addr), addrlen),
            sendlen)
      << strerror(errno);

  auto expect_success_timeout = std::chrono::milliseconds(kTimeout);
  auto start = std::chrono::steady_clock::now();
  // First peek on first byte.
  EXPECT_EQ(asyncSocketRead(recvfd, sendfd, recvbuf, 1, MSG_PEEK, &addr, &addrlen, socketType,
                            expect_success_timeout),
            1);
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(recvbuf[0], sendbuf[0]);

  // Second peek across first 2 packets and drain them from the socket receive queue.
  // Toggle the flags to MSG_PEEK every other iteration.
  ssize_t torecv = sizeof(recvbuf);
  for (int i = 0; torecv > 0; i++) {
    int flags = i % 2 ? 0 : MSG_PEEK;
    ssize_t readLen = 0;
    EXPECT_EQ(readLen = asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), flags, &addr,
                                        &addrlen, socketType, expect_success_timeout),
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
  EXPECT_EQ(asyncSocketRead(recvfd, sendfd, recvbuf, 1, MSG_PEEK, &addr, &addrlen, socketType,
                            success_rcv_duration * 10),
            0);
  EXPECT_EQ(recvbuf[0], 0);
  EXPECT_EQ(close(recvfd), 0) << strerror(errno);
  EXPECT_EQ(close(sendfd), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetSocket, NetSocketTest, ::testing::Values(SOCK_DGRAM, SOCK_STREAM));

TEST(NetDatagramTest, PingIpv4LoopbackAddresses) {
  const char* msg = "hello";
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
        struct in_addr loopback_sin_addr = {};
        loopback_sin_addr.s_addr = htonl((127 << 24) + (i << 16) + (j << 8) + k);
        const char* loopback_addrstr =
            inet_ntop(AF_INET, &loopback_sin_addr, addrbuf, sizeof(addrbuf));
        ASSERT_NE(nullptr, loopback_addrstr);

        fbl::unique_fd recvfd;
        ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        struct sockaddr_in rcv_addr = {};
        rcv_addr.sin_family = AF_INET;
        rcv_addr.sin_addr = loopback_sin_addr;
        ASSERT_EQ(bind(recvfd.get(),
                       reinterpret_cast<const struct sockaddr*>(&rcv_addr), sizeof(rcv_addr)), 0)
            << "recvaddr=" << loopback_addrstr << ": " << strerror(errno);

        socklen_t rcv_addrlen = sizeof(rcv_addr);
        ASSERT_EQ(getsockname(recvfd.get(),
                              reinterpret_cast<struct sockaddr*>(&rcv_addr), &rcv_addrlen), 0)
            << strerror(errno);
        ASSERT_EQ(sizeof(rcv_addr), rcv_addrlen);

        int tmpfd[2];
        ASSERT_EQ(0, pipe(tmpfd)) << strerror(errno);
        fbl::unique_fd ntfyfd[2] = {fbl::unique_fd(tmpfd[0]), fbl::unique_fd(tmpfd[1])};

        struct sockaddr_in src_addr = {};
        socklen_t src_addrlen = sizeof(src_addr);
        std::string out;
        std::thread thrd(DatagramRead, recvfd.get(), &out, &src_addr, &src_addrlen,
          ntfyfd[1].get(), kTimeout);

        fbl::unique_fd sendfd;
        ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        struct sockaddr_in sendto_addr = {};
        sendto_addr.sin_family = AF_INET;
        sendto_addr.sin_addr = loopback_sin_addr;
        sendto_addr.sin_port = rcv_addr.sin_port;
        ASSERT_EQ(sendto(sendfd.get(), msg, strlen(msg), 0, (struct sockaddr*)&sendto_addr,
                         sizeof(sendto_addr)),
                  (ssize_t)strlen(msg))
            << "sendtoaddr=" << loopback_addrstr << ": " << strerror(errno);
        EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

        ASSERT_EQ(true, WaitSuccess(ntfyfd[0].get(), kTimeout));
        thrd.join();

        EXPECT_STREQ(msg, out.c_str());

        EXPECT_EQ(close(ntfyfd[0].release()), 0) << strerror(errno);
        EXPECT_EQ(close(ntfyfd[1].release()), 0) << strerror(errno);
        EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
      }
    }
  }
}

}  // namespace
