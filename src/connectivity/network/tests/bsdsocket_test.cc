// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fuchsia's BSD socket tests ensure that fdio and Netstack together produce
// POSIX-like behavior. This module contains tests that are generic over
// transport protocol.

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <future>
#include <latch>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "os.h"
#include "util.h"

namespace {

std::pair<sockaddr_storage, socklen_t> InitLoopbackAddr(const SocketDomain& domain) {
  sockaddr_storage ss;
  switch (domain.which()) {
    case SocketDomain::Which::IPv4:
      *(reinterpret_cast<sockaddr_in*>(&ss)) = LoopbackSockaddrV4(0);
      return {ss, sizeof(sockaddr_in)};
    case SocketDomain::Which::IPv6:
      *(reinterpret_cast<sockaddr_in6*>(&ss)) = LoopbackSockaddrV6(0);
      return {ss, sizeof(sockaddr_in6)};
  }
}

void ConnectSocketsOverLoopback(const SocketDomain& domain, const SocketType& socket_type,
                                fbl::unique_fd& sendfd, fbl::unique_fd& recvfd) {
  auto [addr, addrlen] = InitLoopbackAddr(domain);

  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(domain.Get(), socket_type.Get(), 0)))
      << strerror(errno);
  switch (socket_type.which()) {
    case SocketType::Which::Stream: {
      fbl::unique_fd acptfd;
      ASSERT_TRUE(acptfd = fbl::unique_fd(socket(domain.Get(), socket_type.Get(), 0)))
          << strerror(errno);
      EXPECT_EQ(bind(acptfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
          << strerror(errno);
      socklen_t found_len = addrlen;
      EXPECT_EQ(getsockname(acptfd.get(), reinterpret_cast<sockaddr*>(&addr), &found_len), 0)
          << strerror(errno);
      EXPECT_EQ(found_len, addrlen);
      EXPECT_EQ(listen(acptfd.get(), 0), 0) << strerror(errno);
      EXPECT_EQ(connect(sendfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
          << strerror(errno);
      ASSERT_TRUE(recvfd = fbl::unique_fd(accept(acptfd.get(), nullptr, nullptr)))
          << strerror(errno);
      EXPECT_EQ(close(acptfd.release()), 0) << strerror(errno);
      break;
    }
    case SocketType::Which::Dgram: {
      ASSERT_TRUE(recvfd = fbl::unique_fd(socket(domain.Get(), socket_type.Get(), 0)))
          << strerror(errno);
      EXPECT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
          << strerror(errno);
      socklen_t found_len = addrlen;
      EXPECT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &found_len), 0)
          << strerror(errno);
      EXPECT_EQ(found_len, addrlen);
      EXPECT_EQ(connect(sendfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
          << strerror(errno);

      EXPECT_EQ(getsockname(sendfd.get(), reinterpret_cast<sockaddr*>(&addr), &found_len), 0)
          << strerror(errno);
      EXPECT_EQ(found_len, addrlen);
      EXPECT_EQ(connect(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
          << strerror(errno);
      break;
    }
  }
}

// Test the error when a client's sandbox does not have access raw/packet sockets.
TEST(LocalhostTest, RawSocketsNotAvailable) {
  // No raw INET sockets.
  ASSERT_EQ(socket(AF_INET, SOCK_RAW, 0), -1);
  ASSERT_EQ(errno, EPROTONOSUPPORT) << strerror(errno);
  ASSERT_EQ(socket(AF_INET, SOCK_RAW, IPPROTO_UDP), -1);
  ASSERT_EQ(errno, EPERM) << strerror(errno);
  ASSERT_EQ(socket(AF_INET, SOCK_RAW, IPPROTO_RAW), -1);
  ASSERT_EQ(errno, EPERM) << strerror(errno);

  // No packet sockets.
  ASSERT_EQ(socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)), -1);
  ASSERT_EQ(errno, EPERM) << strerror(errno);
}

// TODO(https://fxbug.dev/90038): Delete once SockOptsTest is gone.
struct SockOption {
  int level;
  int option;

  bool operator==(const SockOption& other) const {
    return level == other.level && option == other.option;
  }
};

constexpr int INET_ECN_MASK = 3;

using SocketKind = std::tuple<SocketDomain, SocketType>;

std::string SocketKindToString(const testing::TestParamInfo<SocketKind>& info) {
  auto const& [domain, type] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << socketTypeToString(type);
  return oss.str();
}

// Share common functions for SocketKind based tests.
class SocketKindTest : public testing::TestWithParam<SocketKind> {
 protected:
  static fbl::unique_fd NewSocket() {
    auto const& [domain, type] = GetParam();
    return fbl::unique_fd(socket(domain.Get(), type.Get(), 0));
  }

  static std::pair<sockaddr_storage, socklen_t> LoopbackAddr() {
    auto const& [domain, protocol] = GetParam();
    return InitLoopbackAddr(domain);
  }
};

constexpr int kSockOptOn = 1;
constexpr int kSockOptOff = 0;

struct SocketOption {
  SocketOption(int level, std::string level_str, int name, std::string name_str)
      : level(level), level_str(level_str), name(name), name_str(name_str) {}

  int level;
  std::string level_str;
  int name;
  std::string name_str;
};

#define STRINGIFIED_SOCKOPT(level, name) SocketOption(level, #level, name, #name)

struct IntSocketOption {
  SocketOption option;
  bool is_boolean;
  int default_value;
  std::vector<int> valid_values;
  std::vector<int> invalid_values;
};

class SocketOptionTestBase : public testing::Test {
 public:
  SocketOptionTestBase(const SocketDomain& domain, const SocketType& type)
      : sock_domain_(domain), sock_type_(type) {}

 protected:
  void SetUp() override {
    ASSERT_TRUE(sock_ = fbl::unique_fd(socket(sock_domain_.Get(), sock_type_.Get(), 0)))
        << strerror(errno);
  }

  void TearDown() override { EXPECT_EQ(close(sock_.release()), 0) << strerror(errno); }

  bool IsOptionLevelSupportedByDomain(int level) const {
    if (kIsFuchsia) {
      // TODO(https://gvisor.dev/issues/6389): Remove once Fuchsia returns an error
      // when setting/getting IPv6 options on an IPv4 socket.
      return true;
    }
    // IPv6 options are only supported on AF_INET6 sockets.
    return sock_domain_.which() == SocketDomain::Which::IPv6 || level != IPPROTO_IPV6;
  }

  fbl::unique_fd const& sock() const { return sock_; }

 private:
  fbl::unique_fd sock_;
  const SocketDomain sock_domain_;
  const SocketType sock_type_;
};

std::string socketKindAndOptionToString(const SocketDomain& domain, const SocketType& type,
                                        SocketOption opt) {
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << socketTypeToString(type);
  oss << '_' << opt.level_str;
  oss << '_' << opt.name_str;
  return oss.str();
}

using SocketKindAndIntOption = std::tuple<SocketDomain, SocketType, IntSocketOption>;

std::string SocketKindAndIntOptionToString(
    const testing::TestParamInfo<SocketKindAndIntOption>& info) {
  auto const& [domain, type, int_opt] = info.param;
  return socketKindAndOptionToString(domain, type, int_opt.option);
}

// Test functionality common to every integer and pseudo-boolean socket option.
class IntSocketOptionTest : public SocketOptionTestBase,
                            public testing::WithParamInterface<SocketKindAndIntOption> {
 protected:
  IntSocketOptionTest()
      : SocketOptionTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())),
        opt_(std::get<2>(GetParam())) {}

  void SetUp() override {
    ASSERT_FALSE(opt_.valid_values.empty()) << "must have at least one valid value";
    SocketOptionTestBase::SetUp();
  }

  void TearDown() override { SocketOptionTestBase::TearDown(); }

  bool IsOptionCharCompatible() const {
    const int level = opt_.option.level;
    return level != IPPROTO_IPV6 && level != SOL_SOCKET;
  }

  IntSocketOption const& opt() const { return opt_; }

 private:
  const IntSocketOption opt_;
};

TEST_P(IntSocketOptionTest, Default) {
  int get = -1;
  socklen_t get_len = sizeof(get);
  const int r = getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len);

  if (IsOptionLevelSupportedByDomain(opt().option.level)) {
    ASSERT_EQ(r, 0) << strerror(errno);
    ASSERT_EQ(get_len, sizeof(get));
    EXPECT_EQ(get, opt().default_value);
  } else {
    ASSERT_EQ(r, -1);
    EXPECT_EQ(errno, ENOTSUP) << strerror(errno);
  }
}

TEST_P(IntSocketOptionTest, SetValid) {
  for (int value : opt().valid_values) {
    SCOPED_TRACE("value=" + std::to_string(value));
    // Test each value in a lambda so we continue testing the other values if an ASSERT fails.
    [&]() {
      const int r =
          setsockopt(sock().get(), opt().option.level, opt().option.name, &value, sizeof(value));

      if (IsOptionLevelSupportedByDomain(opt().option.level)) {
        ASSERT_EQ(r, 0) << strerror(errno);
        int get = -1;
        socklen_t get_len = sizeof(get);
        ASSERT_EQ(getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len),
                  0)
            << strerror(errno);
        ASSERT_EQ(get_len, sizeof(get));
        EXPECT_EQ(get, opt().is_boolean ? static_cast<bool>(value) : value);
      } else {
        ASSERT_EQ(r, -1);
        EXPECT_EQ(errno, ENOPROTOOPT) << strerror(errno);
      }
    }();
  }
}

TEST_P(IntSocketOptionTest, SetInvalid) {
  for (int value : opt().invalid_values) {
    SCOPED_TRACE("value=" + std::to_string(value));
    // Test each value in a lambda so we continue testing the other values if an ASSERT fails.
    [&]() {
      const int r =
          setsockopt(sock().get(), opt().option.level, opt().option.name, &value, sizeof(value));

      if (IsOptionLevelSupportedByDomain(opt().option.level)) {
        ASSERT_EQ(r, -1);
        EXPECT_EQ(errno, EINVAL) << strerror(errno);

        // Confirm that no changes were made.
        int get = -1;
        socklen_t get_len = sizeof(get);
        ASSERT_EQ(getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len),
                  0)
            << strerror(errno);
        ASSERT_EQ(get_len, sizeof(get));
        EXPECT_EQ(get, opt().default_value);
      } else {
        ASSERT_EQ(r, -1);
        EXPECT_EQ(errno, ENOPROTOOPT) << strerror(errno);
      }
    }();
  }
}

TEST_P(IntSocketOptionTest, SetChar) {
  for (int value : opt().valid_values) {
    SCOPED_TRACE("value=" + std::to_string(value));
    // Test each value in a lambda so we continue testing the other values if an ASSERT fails.
    [&]() {
      int want;
      {
        const char set_char = static_cast<char>(value);
        if (static_cast<int>(set_char) != value) {
          // Skip values that don't fit in a char.
          return;
        }
        const int r = setsockopt(sock().get(), opt().option.level, opt().option.name, &set_char,
                                 sizeof(set_char));
        if (!IsOptionLevelSupportedByDomain(opt().option.level)) {
          ASSERT_EQ(r, -1);
          EXPECT_EQ(errno, ENOPROTOOPT) << strerror(errno);
          want = opt().default_value;
        } else if (!IsOptionCharCompatible()) {
          ASSERT_EQ(r, -1);
          EXPECT_EQ(errno, EINVAL) << strerror(errno);
          want = opt().default_value;
        } else {
          ASSERT_EQ(r, 0) << strerror(errno);
          want = opt().is_boolean ? static_cast<bool>(set_char) : set_char;
        }
      }

      {
        char get = -1;
        socklen_t get_len = sizeof(get);
        const int r =
            getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len);
        if (!IsOptionLevelSupportedByDomain(opt().option.level)) {
          ASSERT_EQ(r, -1);
          EXPECT_EQ(errno, ENOTSUP) << strerror(errno);
        } else {
          ASSERT_EQ(r, 0) << strerror(errno);
          ASSERT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, static_cast<char>(want));
        }
      }

      {
        int16_t get = -1;
        socklen_t get_len = sizeof(get);
        const int r =
            getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len);
        if (!IsOptionLevelSupportedByDomain(opt().option.level)) {
          ASSERT_EQ(r, -1);
          EXPECT_EQ(errno, ENOTSUP) << strerror(errno);
        } else if (!IsOptionCharCompatible()) {
          ASSERT_EQ(r, 0) << strerror(errno);
          ASSERT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, want);
        } else {
          ASSERT_EQ(r, 0) << strerror(errno);
          // Truncates size < 4 to 1 and only writes the low byte.
          // https://github.com/torvalds/linux/blob/2585cf9dfaa/net/ipv4/ip_sockglue.c#L1742-L1745
          ASSERT_EQ(get_len, sizeof(char));
          EXPECT_EQ(get, static_cast<int16_t>(uint16_t(-1) << 8) | want);
        }
      }

      {
        int get = -1;
        socklen_t get_len = sizeof(get);
        const int r =
            getsockopt(sock().get(), opt().option.level, opt().option.name, &get, &get_len);
        if (!IsOptionLevelSupportedByDomain(opt().option.level)) {
          ASSERT_EQ(r, -1);
          EXPECT_EQ(errno, ENOTSUP) << strerror(errno);
        } else {
          ASSERT_EQ(r, 0) << strerror(errno);
          ASSERT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, want);
        }
      }
    }();
  }
}

const std::vector<int> kBooleanOptionValidValues = {-2, -1, 0, 1, 2, 15, 255, 256};

// The tests below use valid and invalid values that attempt to cover normal use cases,
// min/max values, and invalid negative/large values.
// Special values (e.g. ones that reset an option to its default) have option-specific tests.
INSTANTIATE_TEST_SUITE_P(
    IntSocketOptionTests, IntSocketOptionTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Stream(), SocketType::Dgram()),
                     testing::Values(
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_MULTICAST_LOOP),
                             .is_boolean = true,
                             .default_value = 1,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_TOS),
                             .is_boolean = false,
                             .default_value = 0,
                             // The ECN (2 rightmost) bits may be cleared, so we use arbitrary
                             // values without these bits set. See CheckSkipECN test.
                             .valid_values = {0x04, 0xC0, 0xFC},
                             // Larger-than-byte values are accepted but the extra bits are
                             // merely ignored. See InvalidLargeTOS test.
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_RECVTOS),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_TTL),
                             .is_boolean = false,
                             .default_value = 64,
                             // -1 is not tested here, it is a special value which resets ttl to
                             // its default value.
                             .valid_values = {1, 2, 15, 255},
                             .invalid_values = {-2, 0, 256},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_RECVTTL),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         []() {
                           IntSocketOption opt = {
                               .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_MULTICAST_LOOP),
                               .is_boolean = true,
                               .default_value = 1,
                               .valid_values = kBooleanOptionValidValues,
                               .invalid_values = {},
                           };
                           if (!kIsFuchsia) {
                             // On Linux, this option only accepts 0 or 1. This is one of a kind.
                             // There seem to be no good reasons for it, so it should probably be
                             // fixed in Linux rather than in Fuchsia.
                             // https://github.com/torvalds/linux/blob/eec4df26e24/net/ipv6/ipv6_sockglue.c#L758
                             opt.valid_values = {0, 1};
                             opt.invalid_values = {-2, -1, 2, 15, 255, 256};
                           }
                           return opt;
                         }(),
                         []() {
                           IntSocketOption opt = {
                               .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_TCLASS),
                               .is_boolean = false,
                               .default_value = 0,
                               // -1 is not tested here, it is a special value which resets the
                               // traffic class to its default value.
                               .valid_values = {0, 1, 2, 15, 255},
                               .invalid_values = {-2, 256},
                           };
                           if (kIsFuchsia) {
                             // TODO(https://gvisor.dev/issues/6389): Remove once Fuchsia treats
                             // IPV6_TCLASS differently than IP_TOS. See CheckSkipECN test.
                             opt.valid_values = {0x04, 0xC0, 0xFC};
                           }
                           return opt;
                         }(),
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_RECVTCLASS),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_UNICAST_HOPS),
                             .is_boolean = false,
                             .default_value = 64,
                             // -1 is not tested here, it is a special value which resets ttl to
                             // its default value.
                             .valid_values = {0, 1, 2, 15, 255},
                             .invalid_values = {-2, 256},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_RECVHOPLIMIT),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_RECVPKTINFO),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(SOL_SOCKET, SO_NO_CHECK),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(SOL_SOCKET, SO_TIMESTAMP),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(SOL_SOCKET, SO_TIMESTAMPNS),
                             .is_boolean = true,
                             .default_value = 0,
                             .valid_values = kBooleanOptionValidValues,
                             .invalid_values = {},
                         })),
    SocketKindAndIntOptionToString);

// TODO(https://github.com/google/gvisor/issues/6972): Test multicast ttl options on SOCK_STREAM
// sockets. Right now it's complicated because setting these options on a stream socket silently
// fails (no error returned but no change observed).
INSTANTIATE_TEST_SUITE_P(
    DatagramIntSocketOptionTests, IntSocketOptionTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Dgram()),
                     testing::Values(
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_MULTICAST_TTL),
                             .is_boolean = false,
                             .default_value = 1,
                             // -1 is not tested here, it is a special value which
                             // resets the ttl to its default value.
                             .valid_values = {0, 1, 2, 15, 128, 255},
                             .invalid_values = {-2, 256},
                         },
                         IntSocketOption{
                             .option = STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_MULTICAST_HOPS),
                             .is_boolean = false,
                             .default_value = 1,
                             // -1 is not tested here, it is a special value which
                             // resets the hop limit to its default value.
                             .valid_values = {0, 1, 2, 15, 128, 255},
                             .invalid_values = {-2, 256},
                         })),
    SocketKindAndIntOptionToString);

using SocketKindAndOption = std::tuple<SocketDomain, SocketType, SocketOption>;

std::string SocketKindAndOptionToString(const testing::TestParamInfo<SocketKindAndOption>& info) {
  auto const& [domain, type, opt] = info.param;
  return socketKindAndOptionToString(domain, type, opt);
}

class SocketOptionSharedTest : public SocketOptionTestBase,
                               public testing::WithParamInterface<SocketKindAndOption> {
 protected:
  SocketOptionSharedTest()
      : SocketOptionTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())),
        opt_(std::get<2>(GetParam())) {}

  void SetUp() override { SocketOptionTestBase::SetUp(); }

  void TearDown() override { SocketOptionTestBase::TearDown(); }

  SocketOption opt() const { return opt_; }

 private:
  const SocketOption opt_;
};

using TtlHopLimitSocketOptionTest = SocketOptionSharedTest;

TEST_P(TtlHopLimitSocketOptionTest, ResetToDefault) {
  if (!IsOptionLevelSupportedByDomain(opt().level)) {
    GTEST_SKIP() << "Option not supported by socket domain";
  }

  constexpr int kDefaultTTL = 64;
  constexpr int kNonDefaultValue = kDefaultTTL + 1;
  ASSERT_EQ(setsockopt(sock().get(), opt().level, opt().name, &kNonDefaultValue,
                       sizeof(kNonDefaultValue)),
            0)
      << strerror(errno);

  // Coherence check.
  {
    int get = -1;
    socklen_t get_len = sizeof(get);
    ASSERT_EQ(getsockopt(sock().get(), opt().level, opt().name, &get, &get_len), 0)
        << strerror(errno);
    ASSERT_EQ(get_len, sizeof(get));
    EXPECT_EQ(get, kNonDefaultValue);
  }

  constexpr int kResetValue = -1;
  ASSERT_EQ(setsockopt(sock().get(), opt().level, opt().name, &kResetValue, sizeof(kResetValue)), 0)
      << strerror(errno);

  {
    int get = -1;
    socklen_t get_len = sizeof(get);
    ASSERT_EQ(getsockopt(sock().get(), opt().level, opt().name, &get, &get_len), 0)
        << strerror(errno);
    ASSERT_EQ(get_len, sizeof(get));
    EXPECT_EQ(get, kDefaultTTL);
  }
}

INSTANTIATE_TEST_SUITE_P(
    TtlHopLimitSocketOptionTests, TtlHopLimitSocketOptionTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Dgram(), SocketType::Stream()),
                     testing::Values(STRINGIFIED_SOCKOPT(IPPROTO_IP, IP_TTL),
                                     STRINGIFIED_SOCKOPT(IPPROTO_IPV6, IPV6_UNICAST_HOPS))),
    SocketKindAndOptionToString);

// TODO(https://fxbug.dev/90038): Use SocketOptionTestBase for these tests.
class SocketOptsTest : public SocketKindTest {
 protected:
  static bool IsTCP() { return std::get<1>(GetParam()).which() == SocketType::Which::Stream; }

  static bool IsIPv6() { return std::get<0>(GetParam()).which() == SocketDomain::Which::IPv6; }

  static SockOption GetTOSOption() {
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

  static SockOption GetMcastTTLOption() {
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

  static SockOption GetMcastIfOption() {
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

  static SockOption GetRecvTOSOption() {
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

  constexpr static SockOption GetNoChecksum() {
    return {
        .level = SOL_SOCKET,
        .option = SO_NO_CHECK,
    };
  }

  constexpr static SockOption GetTimestamp() {
    return {
        .level = SOL_SOCKET,
        .option = SO_TIMESTAMP,
    };
  }

  constexpr static SockOption GetTimestampNs() {
    return {
        .level = SOL_SOCKET,
        .option = SO_TIMESTAMPNS,
    };
  }
};

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
    // Linux allows values larger than 255, though it only looks at the char part of the value.
    // https://github.com/torvalds/linux/blob/eec4df26e24/net/ipv4/ip_sockglue.c#L1047
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
  if (IsTCP()) {
    if (kIsFuchsia || !IsIPv6()) {
      // gvisor-netstack`s implementation of setsockopt(..IPV6_TCLASS..) clears
      // the ECN bits from the TCLASS value. This keeps gvisor in parity with
      // the Linux test-hosts that run a custom kernel. But that is not the
      // behavior of vanilla Linux kernels. This can be removed when we migrate
      // away from gvisor-netstack.
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
    EXPECT_EQ(get & ~(~0u << (get_sz * 8)), static_cast<uint>(expect_tos));
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

TEST_P(SocketOptsTest, SetUDPMulticastIfImrIfindex) {
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

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST_P(SocketOptsTest, SetUDPMulticastIfImrAddress) {
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
      .imr_address =
          {
              .s_addr = htonl(INADDR_LOOPBACK),
          },
  };
  ASSERT_EQ(setsockopt(s.get(), t.level, t.option, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  in_addr param_out;
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s.get(), t.level, t.option, &param_out, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  ASSERT_EQ(param_out.s_addr, param_in.imr_address.s_addr);

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

TEST_P(SocketOptsTest, UpdateAnyTimestampDisablesOtherTimestampOptions) {
  constexpr std::pair<SockOption, const char*> kOpts[] = {
      std::make_pair(GetTimestamp(), "SO_TIMESTAMP"),
      std::make_pair(GetTimestampNs(), "SO_TIMESTAMPNS"),
  };
  constexpr int optvals[] = {kSockOptOff, kSockOptOn};

  for (const auto& [opt_to_enable, opt_to_enable_name] : kOpts) {
    SCOPED_TRACE("Enable option " + std::string(opt_to_enable_name));
    for (const auto& [opt_to_update, opt_to_update_name] : kOpts) {
      SCOPED_TRACE("Update option " + std::string(opt_to_update_name));
      if (opt_to_enable == opt_to_update) {
        continue;
      }
      for (const int optval : optvals) {
        SCOPED_TRACE("Update value " + std::to_string(optval));
        fbl::unique_fd s;
        ASSERT_TRUE(s = NewSocket()) << strerror(errno);

        ASSERT_EQ(setsockopt(s.get(), opt_to_enable.level, opt_to_enable.option, &kSockOptOn,
                             sizeof(kSockOptOn)),
                  0)
            << strerror(errno);
        {
          int get = -1;
          socklen_t get_len = sizeof(get);
          ASSERT_EQ(getsockopt(s.get(), opt_to_enable.level, opt_to_enable.option, &get, &get_len),
                    0)
              << strerror(errno);
          EXPECT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, kSockOptOn);
        }

        ASSERT_EQ(
            setsockopt(s.get(), opt_to_update.level, opt_to_update.option, &optval, sizeof(optval)),
            0)
            << strerror(errno);
        {
          int get = -1;
          socklen_t get_len = sizeof(get);
          ASSERT_EQ(getsockopt(s.get(), opt_to_update.level, opt_to_update.option, &get, &get_len),
                    0)
              << strerror(errno);
          EXPECT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, optval);
        }

        // The initially enabled option should be disabled after the mutually exclusive option is
        // updated.
        {
          int get = -1;
          socklen_t get_len = sizeof(get);
          ASSERT_EQ(getsockopt(s.get(), opt_to_enable.level, opt_to_enable.option, &get, &get_len),
                    0)
              << strerror(errno);
          EXPECT_EQ(get_len, sizeof(get));
          EXPECT_EQ(get, kSockOptOff);
        }

        EXPECT_EQ(close(s.release()), 0) << strerror(errno);
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    LocalhostTest, SocketOptsTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Dgram(), SocketType::Stream())),
    SocketKindToString);

using TypeMulticast = std::tuple<SocketType, bool>;

std::string TypeMulticastToString(const testing::TestParamInfo<TypeMulticast>& info) {
  auto const& [type, multicast] = info.param;
  std::ostringstream oss;
  oss << socketTypeToString(type);
  if (multicast) {
    oss << "Multicast";
  } else {
    oss << "Loopback";
  }
  return oss.str();
}

class ReuseTest : public testing::TestWithParam<TypeMulticast> {};

TEST_P(ReuseTest, AllowsAddressReuse) {
  const int on = true;
  auto const& [type, multicast] = GetParam();

  if (kIsFuchsia && multicast && type.which() == SocketType::Which::Stream) {
    GTEST_SKIP() << "Cannot bind a TCP socket to a multicast address on Fuchsia";
  }

  sockaddr_in addr = LoopbackSockaddrV4(0);
  if (multicast) {
    int n = inet_pton(addr.sin_family, "224.0.2.1", &addr.sin_addr);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
  }

  fbl::unique_fd s1;
  ASSERT_TRUE(s1 = fbl::unique_fd(socket(AF_INET, type.Get(), 0))) << strerror(errno);

  // TODO(https://gvisor.dev/issue/3839): Remove this when binding to multicast works without group
  // membership. Must outlive the block below.
  fbl::unique_fd s;
  if (kIsFuchsia && type.which() != SocketType::Which::Dgram && multicast) {
    ASSERT_EQ(bind(s1.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), -1);
    ASSERT_EQ(errno, EADDRNOTAVAIL) << strerror(errno);
    ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);
    ip_mreqn param = {
        .imr_multiaddr = addr.sin_addr,
        .imr_address =
            {
                .s_addr = htonl(INADDR_ANY),
            },
        .imr_ifindex = 1,
    };
    ASSERT_EQ(setsockopt(s.get(), SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0)
        << strerror(errno);
  }

  ASSERT_EQ(setsockopt(s1.get(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);
  ASSERT_EQ(bind(s1.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(s1.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  fbl::unique_fd s2;
  ASSERT_TRUE(s2 = fbl::unique_fd(socket(AF_INET, type.Get(), 0))) << strerror(errno);
  ASSERT_EQ(setsockopt(s2.get(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0) << strerror(errno);
  ASSERT_EQ(bind(s2.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(LocalhostTest, ReuseTest,
                         testing::Combine(testing::Values(SocketType::Dgram(),
                                                          SocketType::Stream()),
                                          testing::Values(false, true)),
                         TypeMulticastToString);

class AddrKind {
 public:
  enum class Kind {
    V4,
    V6,
    V4MAPPEDV6,
  };

  explicit AddrKind(Kind kind) : kind_(kind) {}
  Kind Kind() const { return kind_; }

  constexpr const char* AddrKindToString() const {
    switch (kind_) {
      case Kind::V4:
        return "V4";
      case Kind::V6:
        return "V6";
      case Kind::V4MAPPEDV6:
        return "V4MAPPEDV6";
    }
  }

 private:
  const enum Kind kind_;
};

template <int socktype>
class SocketTest : public testing::TestWithParam<AddrKind> {
 protected:
  void SetUp() override {
    ASSERT_TRUE(sock_ = fbl::unique_fd(socket(Domain().Get(), socktype, 0))) << strerror(errno);
  }

  void TearDown() override { EXPECT_EQ(close(sock_.release()), 0) << strerror(errno); }

  const fbl::unique_fd& sock() { return sock_; }

  SocketDomain Domain() const {
    switch (GetParam().Kind()) {
      case AddrKind::Kind::V4:
        return SocketDomain::IPv4();
      case AddrKind::Kind::V6:
      case AddrKind::Kind::V4MAPPEDV6:
        return SocketDomain::IPv6();
    }
  }

  socklen_t AddrLen() const {
    if (Domain().which() == SocketDomain::Which::IPv4) {
      return sizeof(sockaddr_in);
    }
    return sizeof(sockaddr_in6);
  }

  virtual sockaddr_storage Address(uint16_t port) const = 0;

 private:
  fbl::unique_fd sock_;
};

template <int socktype>
class AnyAddrSocketTest : public SocketTest<socktype> {
 protected:
  sockaddr_storage Address(uint16_t port) const override {
    sockaddr_storage addr{
        .ss_family = this->Domain().Get(),
    };

    switch (this->GetParam().Kind()) {
      case AddrKind::Kind::V4: {
        auto sin = reinterpret_cast<sockaddr_in*>(&addr);
        sin->sin_addr.s_addr = htonl(INADDR_ANY);
        sin->sin_port = port;
        return addr;
      }
      case AddrKind::Kind::V6: {
        auto sin6 = reinterpret_cast<sockaddr_in6*>(&addr);
        sin6->sin6_addr = IN6ADDR_ANY_INIT;
        sin6->sin6_port = port;
        return addr;
      }
      case AddrKind::Kind::V4MAPPEDV6: {
        sockaddr_in v4_addr{
            .sin_port = port,
            .sin_addr =
                {
                    .s_addr = htonl(INADDR_ANY),
                },
        };
        *reinterpret_cast<sockaddr_in6*>(&addr) = MapIpv4SockaddrToIpv6Sockaddr(v4_addr);
        return addr;
      }
    }
  }
};

using AnyAddrStreamSocketTest = AnyAddrSocketTest<SOCK_STREAM>;
using AnyAddrDatagramSocketTest = AnyAddrSocketTest<SOCK_DGRAM>;

TEST_P(AnyAddrStreamSocketTest, Connect) {
  sockaddr_storage any = Address(0);
  socklen_t addrlen = AddrLen();
  ASSERT_EQ(connect(sock().get(), reinterpret_cast<const sockaddr*>(&any), addrlen), -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);

  // The error should have been consumed.
  int err;
  socklen_t optlen = sizeof(err);
  ASSERT_EQ(getsockopt(sock().get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(err));
  ASSERT_EQ(err, 0) << strerror(err);
}

TEST_P(AnyAddrDatagramSocketTest, Connect) {
  sockaddr_storage any = Address(0);
  socklen_t addrlen = AddrLen();
  EXPECT_EQ(connect(sock().get(), reinterpret_cast<const sockaddr*>(&any), addrlen), 0)
      << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(AnyAddrSocketTestStream, AnyAddrStreamSocketTest,
                         testing::Values(AddrKind::Kind::V4, AddrKind::Kind::V6,
                                         AddrKind::Kind::V4MAPPEDV6),
                         [](const auto info) { return info.param.AddrKindToString(); });
INSTANTIATE_TEST_SUITE_P(AnyAddrSocketTestDatagram, AnyAddrDatagramSocketTest,
                         testing::Values(AddrKind::Kind::V4, AddrKind::Kind::V6,
                                         AddrKind::Kind::V4MAPPEDV6),
                         [](const auto info) { return info.param.AddrKindToString(); });

enum class ShutdownEnd {
  Local,
  Remote,
};

enum class ExpectedPostShutdownReadResult {
  Success,
  Eagain,
};

enum class ReadType {
  Blocking,
  NonBlocking,
};

enum class ReadSocketState {
  WithPendingData,
  NoPendingData,
};

using ReadAfterShutdownTestCase =
    std::tuple<SocketDomain, SocketType, ShutdownEnd, ShutdownType, ReadType, ReadSocketState>;

ExpectedPostShutdownReadResult GetExpectedPostShutdownReadResult(
    const ReadAfterShutdownTestCase& test_case) {
  const auto& [domain, socket_type, which_end, shutdown_type, read_type, read_socket_state] =
      test_case;

  if (read_socket_state == ReadSocketState::WithPendingData) {
    // Post-shutdown reads always return pending data if it is present.
    return ExpectedPostShutdownReadResult::Success;
  }

  switch (socket_type.which()) {
    case SocketType::Which::Stream:
      if ((which_end == ShutdownEnd::Local && shutdown_type.which() == ShutdownType::Which::Read) ||
          (which_end == ShutdownEnd::Remote &&
           shutdown_type.which() == ShutdownType::Which::Write)) {
        return ExpectedPostShutdownReadResult::Success;
      }
      return ExpectedPostShutdownReadResult::Eagain;
    case SocketType::Which::Dgram:
      if (which_end == ShutdownEnd::Local && shutdown_type.which() == ShutdownType::Which::Read &&
          read_type == ReadType::Blocking) {
        return ExpectedPostShutdownReadResult::Success;
      }
      return ExpectedPostShutdownReadResult::Eagain;
      break;
  }
}

class ReadAfterShutdownTest : public testing::TestWithParam<ReadAfterShutdownTestCase> {};

TEST_P(ReadAfterShutdownTest, Success) {
  const auto& [domain, socket_type, which_end, shutdown_type, read_type, read_socket_state] =
      GetParam();

  if (kIsFuchsia && socket_type.which() == SocketType::Which::Dgram &&
      read_type == ReadType::Blocking && shutdown_type.which() == ShutdownType::Which::Read &&
      which_end == ShutdownEnd::Local && read_socket_state == ReadSocketState::NoPendingData) {
    // TODO(https://fxbug.dev/42041): Support blocking reads after shutdown for dgram sockets.
    GTEST_SKIP() << "Blocking dgram reads with no pending data hang on Fuchsia when the socket "
                    "is shutdown with SHUT_RD";
  }

  fbl::unique_fd remote;
  fbl::unique_fd local;
  ASSERT_NO_FATAL_FAILURE(ConnectSocketsOverLoopback(domain, socket_type, remote, local));

  char buf[] = "abc";
  if (read_socket_state == ReadSocketState::WithPendingData) {
    ASSERT_EQ(write(remote.get(), &buf, sizeof(buf)), ssize_t(sizeof(buf))) << strerror(errno);
    pollfd pfd = {
        .fd = local.get(),
        .events = POLLIN,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLIN);
  }

  int shutdown_fd = [&, which_end = which_end]() {
    switch (which_end) {
      case ShutdownEnd::Local:
        return local.get();
      case ShutdownEnd::Remote:
        return remote.get();
    }
  }();

  EXPECT_EQ(shutdown(shutdown_fd, shutdown_type.Get()), 0) << strerror(errno);

  if (socket_type.which() == SocketType::Which::Stream && which_end == ShutdownEnd::Remote &&
      shutdown_type.which() == ShutdownType::Which::Write) {
    // Give the TCP FIN time to propagate from `remote` to `local`.
    pollfd pfd = {
        .fd = local.get(),
        .events = POLLRDHUP,
    };
    int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(pfd.revents, POLLRDHUP);
  }

  char recv_buf[sizeof(buf) + 1];
  const int flags = read_type == ReadType::Blocking ? 0 : MSG_DONTWAIT;
  switch (GetExpectedPostShutdownReadResult(GetParam())) {
    case ExpectedPostShutdownReadResult::Success: {
      switch (read_socket_state) {
        case ReadSocketState::WithPendingData:
          EXPECT_EQ(recv(local.get(), &recv_buf, sizeof(recv_buf), flags), ssize_t(sizeof(buf)))
              << strerror(errno);
          EXPECT_EQ(std::string_view(recv_buf, sizeof(buf)), std::string_view(buf, sizeof(buf)));
          break;
        case ReadSocketState::NoPendingData:
          EXPECT_EQ(recv(local.get(), &recv_buf, sizeof(recv_buf), flags), 0) << strerror(errno);
          break;
      }
    } break;
    case ExpectedPostShutdownReadResult::Eagain: {
      switch (read_type) {
        case ReadType::Blocking: {
          timeval tv = {
              .tv_sec = 1,
          };
          EXPECT_EQ(setsockopt(local.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
              << strerror(errno);

          std::latch fut_started(1);
          const auto fut = std::async(std::launch::async, [&]() {
            fut_started.count_down();

            EXPECT_EQ(recv(local.get(), &recv_buf, sizeof(recv_buf), flags), -1) << strerror(errno);
            EXPECT_EQ(errno, EAGAIN);
          });
          fut_started.wait();
          ASSERT_NO_FATAL_FAILURE(AssertBlocked(fut));
        } break;
        case ReadType::NonBlocking:
          EXPECT_EQ(recv(local.get(), &recv_buf, sizeof(recv_buf), flags), -1) << strerror(errno);
          EXPECT_EQ(errno, EAGAIN);
          break;
      }
    } break;
  }
}

std::string ReadAfterShutdownTestCaseToString(
    const testing::TestParamInfo<ReadAfterShutdownTestCase>& info) {
  const auto& [domain, socket_type, which_end, shutdown_type, read_type, read_socket_state] =
      info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << socketTypeToString(socket_type);

  switch (which_end) {
    case ShutdownEnd::Local:
      oss << '_' << "Self";
      break;
    case ShutdownEnd::Remote:
      oss << '_' << "Peer";
      break;
  }

  switch (shutdown_type.which()) {
    case ShutdownType::Which::Read:
      oss << '_' << "SHUT_RD";
      break;
    case ShutdownType::Which::Write:
      oss << '_' << "SHUT_WR";
      break;
  }

  switch (read_type) {
    case ReadType::Blocking:
      oss << '_' << "BlockingRead";
      break;
    case ReadType::NonBlocking:
      oss << '_' << "NonBlockingRead";
      break;
  }

  switch (read_socket_state) {
    case ReadSocketState::WithPendingData:
      oss << '_' << "WithPendingData";
      break;
    case ReadSocketState::NoPendingData:
      oss << '_' << "NoPendingData";
      break;
  }

  return oss.str();
}

INSTANTIATE_TEST_SUITE_P(
    ReadAfterShutdownTests, ReadAfterShutdownTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Dgram(), SocketType::Stream()),
                     testing::Values(ShutdownEnd::Remote, ShutdownEnd::Local),
                     testing::Values(ShutdownType::Read(), ShutdownType::Write()),
                     testing::Values(ReadType::Blocking, ReadType::NonBlocking),
                     testing::Values(ReadSocketState::WithPendingData,
                                     ReadSocketState::NoPendingData)),
    ReadAfterShutdownTestCaseToString);

// Socket tests across multiple socket-types, SOCK_DGRAM, SOCK_STREAM.
class NetSocketTest : public testing::TestWithParam<SocketType> {};

// Test MSG_PEEK
// MSG_PEEK : Peek into the socket receive queue without moving the contents from it.
//
// TODO(https://fxbug.dev/90876): change this test to use recvmsg instead of recvfrom to exercise
// MSG_PEEK with scatter/gather.
TEST_P(NetSocketTest, SocketPeekTest) {
  const SocketType socket_type = GetParam();
  ssize_t expectReadLen = 0;
  char sendbuf[8] = {};
  char recvbuf[2 * sizeof(sendbuf)] = {};
  ssize_t sendlen = sizeof(sendbuf);

  fbl::unique_fd sendfd;
  fbl::unique_fd recvfd;
  ASSERT_NO_FATAL_FAILURE(
      ConnectSocketsOverLoopback(SocketDomain::IPv4(), socket_type, sendfd, recvfd));

  switch (socket_type.which()) {
    case SocketType::Which::Stream: {
      // Expect to read both the packets in a single recv() call.
      expectReadLen = sizeof(recvbuf);
      break;
    }
    case SocketType::Which::Dgram: {
      // Expect to read single packet per recv() call.
      expectReadLen = sizeof(sendbuf);
      break;
    }
  }

  // This test sends 2 packets with known values and validates MSG_PEEK across the 2 packets.
  sendbuf[0] = 0x56;
  sendbuf[6] = 0x78;

  // send 2 separate packets and test peeking across
  EXPECT_EQ(send(sendfd.get(), sendbuf, sizeof(sendbuf), 0), sendlen) << strerror(errno);
  EXPECT_EQ(send(sendfd.get(), sendbuf, sizeof(sendbuf), 0), sendlen) << strerror(errno);

  auto start = std::chrono::steady_clock::now();
  // First peek on first byte.
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, 1, MSG_PEEK, socket_type,
                            SocketDomain::IPv4(), kTimeout),
            1);
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(recvbuf[0], sendbuf[0]);

  // Second peek across first 2 packets and drain them from the socket receive queue.
  ssize_t torecv = sizeof(recvbuf);
  for (int i = 0; torecv > 0; i++) {
    int flags = i % 2 ? 0 : MSG_PEEK;
    ssize_t readLen = 0;
    // Retry socket read with MSG_PEEK to ensure all of the expected data is received.
    //
    // TODO(https://fxbug.dev/74639) : Use SO_RCVLOWAT instead of retry.
    do {
      readLen = asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), flags,
                                socket_type, SocketDomain::IPv4(), kTimeout);
      if (HasFailure()) {
        break;
      }
    } while (flags == MSG_PEEK && readLen < expectReadLen);
    EXPECT_EQ(readLen, expectReadLen);

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
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, 1, MSG_PEEK, socket_type,
                            SocketDomain::IPv4(), success_rcv_duration * 10),
            0);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetSocket, NetSocketTest,
                         testing::Values(SocketType::Dgram(), SocketType::Stream()));

TEST_P(SocketKindTest, IoctlInterfaceLookupRoundTrip) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);

  // This test assumes index 1 is bound to a valid interface. In Fuchsia's test environment (the
  // component executing this test), 1 is always bound to "lo".
  ifreq ifr_iton = {};
  ifr_iton.ifr_ifindex = 1;
  // Set ifr_name to random chars to test ioctl correctly sets null terminator.
  memset(ifr_iton.ifr_name, 0xde, IFNAMSIZ);
  ASSERT_EQ(strnlen(ifr_iton.ifr_name, IFNAMSIZ), (size_t)IFNAMSIZ);
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), 0) << strerror(errno);
  ASSERT_LT(strnlen(ifr_iton.ifr_name, IFNAMSIZ), (size_t)IFNAMSIZ);

  ifreq ifr_ntoi;
  strncpy(ifr_ntoi.ifr_name, ifr_iton.ifr_name, IFNAMSIZ);
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFINDEX, &ifr_ntoi), 0) << strerror(errno);
  EXPECT_EQ(ifr_ntoi.ifr_ifindex, 1);

  ifreq ifr_err;
  memset(ifr_err.ifr_name, 0xde, IFNAMSIZ);
  // Although the first few bytes of ifr_name contain the correct name, there is no null
  // terminator and the remaining bytes are gibberish, should match no interfaces.
  memcpy(ifr_err.ifr_name, ifr_iton.ifr_name, strnlen(ifr_iton.ifr_name, IFNAMSIZ));

  const struct {
    std::string name;
    int request;
  } requests[] = {
      {
          .name = "SIOCGIFINDEX",
          .request = SIOCGIFINDEX,
      },
      {
          .name = "SIOCGIFFLAGS",
          .request = SIOCGIFFLAGS,
      },
  };
  for (const auto& request : requests) {
    ASSERT_EQ(ioctl(fd.get(), request.request, &ifr_err), -1) << request.name;
    EXPECT_EQ(errno, ENODEV) << request.name << ": " << strerror(errno);
  }
}

TEST_P(SocketKindTest, IoctlFIONREAD) {
  auto const& [domain, socket_type] = GetParam();

  fbl::unique_fd recvfd;
  fbl::unique_fd sendfd;
  ASSERT_NO_FATAL_FAILURE(ConnectSocketsOverLoopback(domain, socket_type, sendfd, recvfd));

  char sendbuf[1];
  EXPECT_EQ(send(sendfd.get(), sendbuf, sizeof(sendbuf), 0), ssize_t(sizeof(sendbuf)))
      << strerror(errno);

  pollfd pfd = {
      .fd = recvfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  int num_readable;
  int res = ioctl(recvfd.get(), FIONREAD, &num_readable);

  if (kIsFuchsia && socket_type.which() == SocketType::Which::Dgram) {
    // TODO(https://fxbug.dev/42040): Support FIONREAD on Fuchsia.
    ASSERT_EQ(res, -1);
    EXPECT_EQ(errno, ENOTTY) << strerror(errno);
    return;
  }

  ASSERT_EQ(res, 0) << strerror(errno);
  ASSERT_GE(num_readable, 0);
  EXPECT_EQ(static_cast<size_t>(num_readable), sizeof(sendbuf));
}

TEST_P(SocketKindTest, IoctlInterfaceNotFound) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);

  // Invalid ifindex "-1" should match no interfaces.
  ifreq ifr_iton = {};
  ifr_iton.ifr_ifindex = -1;
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), -1);
  EXPECT_EQ(errno, ENODEV) << strerror(errno);

  // Empty name should match no interface.
  ifreq ifr = {};
  const struct {
    std::string name;
    int request;
  } requests[] = {
      {
          .name = "SIOCGIFINDEX",
          .request = SIOCGIFINDEX,
      },
      {
          .name = "SIOCGIFFLAGS",
          .request = SIOCGIFFLAGS,
      },
  };
  for (const auto& request : requests) {
    ASSERT_EQ(ioctl(fd.get(), request.request, &ifr), -1) << request.name;
    EXPECT_EQ(errno, ENODEV) << request.name << ": " << strerror(errno);
  }
}

template <typename F>
void TestGetname(const fbl::unique_fd& fd, F getname, const sockaddr* sa, const socklen_t sa_len) {
  ASSERT_EQ(getname(fd.get(), nullptr, nullptr), -1);
  EXPECT_EQ(errno, EFAULT) << strerror(errno);
  errno = 0;

  sockaddr_storage ss;
  ASSERT_EQ(getname(fd.get(), reinterpret_cast<sockaddr*>(&ss), nullptr), -1);
  EXPECT_EQ(errno, EFAULT) << strerror(errno);
  errno = 0;

  socklen_t len = 0;
  ASSERT_EQ(getname(fd.get(), nullptr, &len), 0) << strerror(errno);
  EXPECT_EQ(len, sa_len);

  len = 1;
  ASSERT_EQ(getname(fd.get(), nullptr, &len), -1);
  EXPECT_EQ(errno, EFAULT) << strerror(errno);
  EXPECT_EQ(len, 1u);
  errno = 0;

  sa_family_t family;
  len = sizeof(family);
  ASSERT_EQ(getname(fd.get(), reinterpret_cast<sockaddr*>(&family), &len), 0) << strerror(errno);
  ASSERT_EQ(len, sa_len);
  EXPECT_EQ(family, sa->sa_family);

  len = sa_len;
  ASSERT_EQ(getname(fd.get(), reinterpret_cast<sockaddr*>(&ss), &len), 0) << strerror(errno);
  ASSERT_EQ(len, sa_len);
  EXPECT_EQ(memcmp(&ss, sa, sa_len), 0);

  struct {
    sockaddr_storage ss;
    char unused;
  } ss_with_extra = {
      .unused = 0x44,
  };
  len = sizeof(ss_with_extra);
  ASSERT_EQ(getname(fd.get(), reinterpret_cast<sockaddr*>(&ss_with_extra), &len), 0)
      << strerror(errno);
  ASSERT_EQ(len, sa_len);
  EXPECT_EQ(memcmp(&ss, sa, sa_len), 0);
  EXPECT_EQ(ss_with_extra.unused, 0x44);
}

TEST_P(SocketKindTest, Getsockname) {
  auto [ss, len] = LoopbackAddr();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = NewSocket()) << strerror(errno);

  ASSERT_EQ(bind(fd.get(), reinterpret_cast<sockaddr*>(&ss), sizeof(ss)), 0) << strerror(errno);
  socklen_t ss_len = sizeof(ss);
  // Get the socket's local address so TestGetname can compare against it.
  ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&ss), &ss_len), 0) << strerror(errno);
  ASSERT_EQ(ss_len, len);

  ASSERT_NO_FATAL_FAILURE(TestGetname(fd, getsockname, reinterpret_cast<sockaddr*>(&ss), len));
}

TEST_P(SocketKindTest, Getpeername) {
  auto const& [domain, protocol] = GetParam();
  auto [ss, len] = LoopbackAddr();

  fbl::unique_fd listener;
  ASSERT_TRUE(listener = NewSocket()) << strerror(errno);
  ASSERT_EQ(bind(listener.get(), reinterpret_cast<sockaddr*>(&ss), sizeof(ss)), 0)
      << strerror(errno);
  socklen_t ss_len = sizeof(ss);
  ASSERT_EQ(getsockname(listener.get(), reinterpret_cast<sockaddr*>(&ss), &ss_len), 0)
      << strerror(errno);
  if (protocol.which() == SocketType::Which::Stream) {
    ASSERT_EQ(listen(listener.get(), 1), 0) << strerror(errno);
  }

  fbl::unique_fd client;
  ASSERT_TRUE(client = NewSocket()) << strerror(errno);
  ASSERT_EQ(connect(client.get(), reinterpret_cast<sockaddr*>(&ss), sizeof(ss)), 0)
      << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(TestGetname(client, getpeername, reinterpret_cast<sockaddr*>(&ss), len));
}

TEST(SocketKindTest, IoctlLookupForNonSocketFd) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open("/", O_RDONLY | O_DIRECTORY))) << strerror(errno);

  ifreq ifr_iton = {};
  ifr_iton.ifr_ifindex = 1;
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFNAME, &ifr_iton), -1);
  EXPECT_EQ(errno, ENOTTY) << strerror(errno);

  ifreq ifr;
  strcpy(ifr.ifr_name, "loblah");
  const struct {
    std::string name;
    int request;
  } requests[] = {
      {
          .name = "SIOCGIFINDEX",
          .request = SIOCGIFINDEX,
      },
      {
          .name = "SIOCGIFFLAGS",
          .request = SIOCGIFFLAGS,
      },
  };
  for (const auto& request : requests) {
    ASSERT_EQ(ioctl(fd.get(), request.request, &ifr), -1) << request.name;
    EXPECT_EQ(errno, ENOTTY) << request.name << ": " << strerror(errno);
  }
}

INSTANTIATE_TEST_SUITE_P(
    NetSocket, SocketKindTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(SocketType::Dgram(), SocketType::Stream())),
    SocketKindToString);

using DomainProtocol = std::tuple<SocketDomain, int>;
class IcmpSocketTest : public testing::TestWithParam<DomainProtocol> {
 protected:
  void SetUp() override {
    auto const& [domain, protocol] = GetParam();
    fd_ = fbl::unique_fd(socket(domain.Get(), SOCK_DGRAM, protocol));
    if (!kIsFuchsia && !fd_.is_valid()) {
      ASSERT_EQ(errno, EACCES) << strerror(errno);
      GTEST_SKIP() << "This test requires elevated privileges";
    }
    ASSERT_TRUE(fd_) << strerror(errno);
  }

  const fbl::unique_fd& fd() const { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_P(IcmpSocketTest, GetSockoptSoProtocol) {
  auto const& [domain, protocol] = GetParam();

  int opt;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_PROTOCOL, &opt, &optlen), 0) << strerror(errno);
  EXPECT_EQ(optlen, sizeof(opt));
  EXPECT_EQ(opt, protocol);
}

TEST_P(IcmpSocketTest, PayloadIdentIgnored) {
  auto const& [domain, protocol] = GetParam();

  constexpr short kBindIdent = 3;
  constexpr short kDestinationIdent = kBindIdent + 1;

  switch (domain.which()) {
    case SocketDomain::Which::IPv4: {
      const sockaddr_in bind_addr = LoopbackSockaddrV4(kBindIdent);
      ASSERT_EQ(bind(fd().get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)),
                0)
          << strerror(errno);
      const icmphdr pkt = []() {
        icmphdr pkt;
        // Populate with garbage to prove other fields are unused.
        memset(&pkt, 0x4a, sizeof(pkt));
        pkt.type = ICMP_ECHO;
        pkt.code = 0;
        return pkt;
      }();
      const sockaddr_in dst_addr = {
          .sin_family = bind_addr.sin_family,
          .sin_port = htons(kDestinationIdent),
          .sin_addr = bind_addr.sin_addr,
      };
      ASSERT_EQ(sendto(fd().get(), &pkt, sizeof(pkt), 0,
                       reinterpret_cast<const sockaddr*>(&dst_addr), sizeof(dst_addr)),
                ssize_t(sizeof(pkt)))
          << strerror(errno);

      struct {
        std::remove_const<decltype(pkt)>::type hdr;
        char unused;
      } hdr_with_extra = {
          .unused = 0x44,
      };
      memset(&hdr_with_extra.hdr, 0x4a, sizeof(hdr_with_extra.hdr));
      ASSERT_EQ(read(fd().get(), &hdr_with_extra, sizeof(hdr_with_extra)), ssize_t(sizeof(pkt)))
          << strerror(errno);
      EXPECT_EQ(hdr_with_extra.unused, 0x44);
      EXPECT_EQ(hdr_with_extra.hdr.type, 0);
      EXPECT_EQ(hdr_with_extra.hdr.code, 0);
      EXPECT_NE(hdr_with_extra.hdr.checksum, 0);
      EXPECT_EQ(htons(hdr_with_extra.hdr.un.echo.id), kBindIdent);
      EXPECT_EQ(hdr_with_extra.hdr.un.echo.sequence, pkt.un.echo.sequence);
    } break;
    case SocketDomain::Which::IPv6: {
      const sockaddr_in6 bind_addr = LoopbackSockaddrV6(kBindIdent);
      ASSERT_EQ(bind(fd().get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)),
                0)
          << strerror(errno);
      const icmp6_hdr pkt = []() {
        icmp6_hdr pkt;
        // Populate with garbage to prove other fields are unused.
        memset(&pkt, 0x4a, sizeof(pkt));
        pkt.icmp6_type = ICMP6_ECHO_REQUEST;
        pkt.icmp6_code = 0;
        return pkt;
      }();
      const sockaddr_in6 dst_addr = {
          .sin6_family = bind_addr.sin6_family,
          .sin6_port = htons(kDestinationIdent),
          .sin6_addr = bind_addr.sin6_addr,
      };
      ASSERT_EQ(sendto(fd().get(), &pkt, sizeof(pkt), 0,
                       reinterpret_cast<const sockaddr*>(&dst_addr), sizeof(dst_addr)),
                ssize_t(sizeof(pkt)))
          << strerror(errno);

      struct {
        std::remove_const<decltype(pkt)>::type hdr;
        char unused;
      } hdr_with_extra = {
          .unused = 0x44,
      };
      memset(&hdr_with_extra.hdr, 0x4a, sizeof(hdr_with_extra.hdr));
      ASSERT_EQ(read(fd().get(), &hdr_with_extra, sizeof(hdr_with_extra)), ssize_t(sizeof(pkt)))
          << strerror(errno);
      EXPECT_EQ(hdr_with_extra.unused, 0x44);
      EXPECT_EQ(hdr_with_extra.hdr.icmp6_type, ICMP6_ECHO_REPLY);
      EXPECT_EQ(hdr_with_extra.hdr.icmp6_code, 0);
      EXPECT_NE(hdr_with_extra.hdr.icmp6_cksum, 0);
      EXPECT_EQ(htons(hdr_with_extra.hdr.icmp6_id), kBindIdent);
      EXPECT_EQ(hdr_with_extra.hdr.icmp6_seq, pkt.icmp6_seq);
    } break;
  }
}

INSTANTIATE_TEST_SUITE_P(NetSocket, IcmpSocketTest,
                         testing::Values(std::make_pair(SocketDomain::IPv4(), IPPROTO_ICMP),
                                         std::make_pair(SocketDomain::IPv6(), IPPROTO_ICMPV6)));

}  // namespace
