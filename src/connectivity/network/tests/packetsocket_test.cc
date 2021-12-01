// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <netpacket/packet.h>

#ifdef __linux__
#include <sys/syscall.h>

#include <linux/capability.h>
#endif

#include <zircon/compiler.h>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

// sockaddr_ll ends with an 8 byte physical address field, but only the
// bytes that are used in the sockaddr_ll.sll_addr field are included in the
// address length. Seems Linux used to return the size of sockaddr_ll, but
// https://github.com/torvalds/linux/commit/0fb375fb9b93b7d822debc6a734052337ccfdb1f
// changed things to only return `sizeof(sockaddr_ll) - sizeof(sll.sll_addr)`.
#define ASSERT_ADDRLEN_AFTER_GETSOCKNAME(addrlen, halen) \
  ASSERT_EQ(addrlen, offsetof(sockaddr_ll, sll_addr) + halen)

using ::testing::Combine;
using ::testing::Values;

class PacketSocketTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
#ifdef __linux__
    struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
    struct __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3] = {};
    auto ret = syscall(SYS_capget, &header, &caps);
    ASSERT_GE(ret, 0) << strerror(errno);
    if ((caps[CAP_TO_INDEX(CAP_NET_RAW)].effective & CAP_TO_MASK(CAP_NET_RAW)) == 0) {
      have_packet_socket_capability_ = false;
    }
#endif
  }

  void SetUp() override {
    if (!have_packet_socket_capability_) {
      GTEST_SKIP() << "Do not have packet socket capability";
    }
  }

 private:
  static bool have_packet_socket_capability_;
};

bool PacketSocketTest::have_packet_socket_capability_ = true;

TEST_F(PacketSocketTest, ProtocolZeroAllowed) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_PACKET, SOCK_RAW, 0))) << strerror(errno);
}

int GetLoopbackIndex() { return if_nametoindex("lo"); }

TEST_F(PacketSocketTest, Connect) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_PACKET, SOCK_RAW, 0))) << strerror(errno);
  const sockaddr_ll bind_addr = {
      .sll_family = AF_PACKET,
      .sll_protocol = htons(ETH_P_IP),
      .sll_ifindex = GetLoopbackIndex(),
  };
  ASSERT_EQ(connect(fd.get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)),
            -1);
  ASSERT_EQ(errno, EOPNOTSUPP) << strerror(errno);
}

TEST_F(PacketSocketTest, Getpeername) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_PACKET, SOCK_RAW, 0))) << strerror(errno);
  sockaddr_ll addr;
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getpeername(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), -1);
  ASSERT_EQ(errno, EOPNOTSUPP) << strerror(errno);
}

TEST_F(PacketSocketTest, Getsockname) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_PACKET, SOCK_RAW, 0))) << strerror(errno);

  // Must provide non-null pointers.
  {
    socklen_t addrlen = 1;
    ASSERT_EQ(getsockname(fd.get(), nullptr, &addrlen), -1);
    EXPECT_EQ(addrlen, 1u);
    EXPECT_EQ(errno, EFAULT) << strerror(errno);
    errno = 0;
  }

  {
    sockaddr_ll addr;
    ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), nullptr), -1);
    EXPECT_EQ(errno, EFAULT) << strerror(errno);
    errno = 0;
  }

  {
    // addr should not be modified since we pass 0 for its length.
    sockaddr_ll addr;
    memset(&addr, 1, sizeof(addr));
    socklen_t addrlen = 0;
    ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_ADDRLEN_AFTER_GETSOCKNAME(addrlen, 0);
    sockaddr_ll expected;
    memset(&expected, 1, sizeof(expected));
    EXPECT_EQ(memcmp(&addr, &expected, sizeof(expected)), 0);
  }

  {
    // Socket is not bound so sll_addr should not be touched.
    sockaddr_ll addr;
    memset(&addr, 1, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_ADDRLEN_AFTER_GETSOCKNAME(addrlen, 0);
    sockaddr_ll expected = {.sll_family = AF_PACKET};
    memset(expected.sll_addr, 1, sizeof(expected.sll_addr));
    EXPECT_EQ(memcmp(&addr, &expected, sizeof(expected)), 0);
  }

  {
    // Only the required bytes from sll_addr should be modified. Loopback's
    // (ethernet) address size is smaller than sll_addr so the tail-end of
    // sll_addr should not be modified.
    const sockaddr_ll bind_addr = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_IP),
        .sll_ifindex = GetLoopbackIndex(),
    };
    ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)), 0)
        << strerror(errno);
    sockaddr_ll addr;
    memset(&addr, 1, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_ADDRLEN_AFTER_GETSOCKNAME(addrlen, ETH_ALEN);
    EXPECT_EQ(addr.sll_family, bind_addr.sll_family);
    EXPECT_EQ(ntohs(addr.sll_protocol), ntohs(bind_addr.sll_protocol));
    EXPECT_EQ(addr.sll_hatype, ARPHRD_LOOPBACK);
    EXPECT_EQ(addr.sll_ifindex, bind_addr.sll_ifindex);
    EXPECT_EQ(addr.sll_halen, ETH_ALEN);
    // Bound to loopback which has the all zeroes address.
    for (int i = 0; i < addr.sll_halen; ++i) {
      EXPECT_EQ(addr.sll_addr[i], 0) << "byte mismatch @ idx = " << i;
    }
    // The unused address bytes should not have been modified.
    ASSERT_LT(addr.sll_halen, sizeof(addr.sll_addr));
    for (size_t i = addr.sll_halen; i < sizeof(addr.sll_addr); ++i) {
      EXPECT_EQ(addr.sll_addr[i], 1) << "byte mismatch @ idx = " << i;
    }
  }
}

void TestSendAndReceive(const socklen_t usable_src_addr_len) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_PACKET, SOCK_DGRAM, 0))) << strerror(errno);

  const sockaddr_ll bind_addr = {
      .sll_family = AF_PACKET,
      .sll_protocol = htons(ETH_P_IP),
      .sll_ifindex = GetLoopbackIndex(),
  };
  ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)), 0)
      << strerror(errno);

  sockaddr_ll dest_addr = bind_addr;
  // Loopback has the zero valued EUI48 address.
  dest_addr.sll_halen = ETH_ALEN;
  char send_buf[] = "this is a malformed L3 packet";
  ASSERT_EQ(sendto(fd.get(), send_buf, sizeof(send_buf), 0, reinterpret_cast<sockaddr*>(&dest_addr),
                   sizeof(dest_addr)),
            static_cast<ssize_t>(sizeof(send_buf)))
      << strerror(errno);

  struct {
    sockaddr_ll addr;
    char unused;
  } __PACKED src_addr;
  memset(&src_addr, 1, sizeof(src_addr));
  socklen_t src_addr_len = usable_src_addr_len;
  char recv_buf[sizeof(send_buf) + 1];
  ASSERT_EQ(recvfrom(fd.get(), recv_buf, sizeof(recv_buf), 0,
                     reinterpret_cast<sockaddr*>(&src_addr), &src_addr_len),
            static_cast<ssize_t>(sizeof(send_buf)))
      << strerror(errno);
  EXPECT_EQ(memcmp(recv_buf, send_buf, sizeof(send_buf)), 0);
  ASSERT_EQ(src_addr_len, sizeof(src_addr.addr));
  EXPECT_EQ(src_addr.addr.sll_family, AF_PACKET);
  EXPECT_EQ(ntohs(src_addr.addr.sll_protocol), ETH_P_IP);
  EXPECT_EQ(src_addr.addr.sll_ifindex, bind_addr.sll_ifindex);
  EXPECT_EQ(src_addr.addr.sll_pkttype, PACKET_HOST);
  EXPECT_EQ(src_addr.addr.sll_halen, ETH_ALEN);
  // Packet was sent through the loopback interface which has the all zeroes
  // address.
  for (int i = 0; i < src_addr.addr.sll_halen; ++i) {
    EXPECT_EQ(src_addr.addr.sll_addr[i], 0) << "byte mismatch @ idx = " << i;
  }
  ASSERT_LT(src_addr.addr.sll_halen, sizeof(src_addr.addr.sll_addr));
  if (sizeof(src_addr.addr) > usable_src_addr_len) {
    // Not all of src_addr was considered usable.
    const socklen_t unused = sizeof(src_addr.addr) - usable_src_addr_len;
    size_t i = src_addr.addr.sll_halen;
    for (; i < sizeof(src_addr.addr.sll_addr) - unused; ++i) {
      EXPECT_EQ(src_addr.addr.sll_addr[i], 0) << "byte mismatch @ idx = " << i;
    }
    for (; i < sizeof(src_addr.addr.sll_addr); ++i) {
      EXPECT_EQ(src_addr.addr.sll_addr[i], 1) << "byte mismatch @ idx = " << i;
    }
  }
  EXPECT_EQ(src_addr.unused, 1);
}

TEST_F(PacketSocketTest, SendAndReceiveAddrLenEqualSockaddrLLSize) {
  ASSERT_NO_FATAL_FAILURE(TestSendAndReceive(sizeof(sockaddr_ll)));
}

TEST_F(PacketSocketTest, SendAndReceiveAddrLenLessThanSockaddrLLSize) {
  ASSERT_NO_FATAL_FAILURE(TestSendAndReceive(sizeof(sockaddr_ll) - 1));
}

TEST_F(PacketSocketTest, SendAndReceiveAddrLenMoreThanSockaddrLLSize) {
  ASSERT_NO_FATAL_FAILURE(TestSendAndReceive(sizeof(sockaddr_ll) + 1));
}

// Fixture for tests parameterized by type and protocol.
class GenericPacketSocketTest : public PacketSocketTest,
                                public testing::WithParamInterface<std::tuple<int, uint16_t>> {
 protected:
  // Creates a socket to be used in tests.
  void SetUp() override {
    PacketSocketTest::SetUp();

    if (IsSkipped()) {
      return;
    }

    const auto& [type, protocol] = GetParam();

    ASSERT_TRUE(fd_ = fbl::unique_fd(socket(AF_PACKET, type, htons(protocol)))) << strerror(errno);
  }

  const fbl::unique_fd& fd() const { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_P(GenericPacketSocketTest, SockOptSoDomain) {
  int opt = -1;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_DOMAIN, &opt, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(opt));
  EXPECT_EQ(opt, AF_PACKET);
}

TEST_P(GenericPacketSocketTest, SockOptSoProtocol) {
  int opt = -1;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_PROTOCOL, &opt, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(opt));
  // SO_PROTOCOL doesn't have meaning for packet sockets.
  EXPECT_EQ(opt, 0);
}

TEST_P(GenericPacketSocketTest, SockOptSoType) {
  const auto& [type, protocol] = GetParam();

  int opt = -1;
  socklen_t optlen = sizeof(opt);
  ASSERT_EQ(getsockopt(fd().get(), SOL_SOCKET, SO_TYPE, &opt, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(int));
  EXPECT_EQ(opt, type);
}

INSTANTIATE_TEST_SUITE_P(AllPacketSocketTests, GenericPacketSocketTest,
                         Combine(Values(SOCK_DGRAM, SOCK_RAW),
                                 Values(0, ETH_P_ALL, ETH_P_IP, ETH_P_IPV6, ETH_P_ARP,
                                        ETH_P_PHONET)));

}  // namespace
