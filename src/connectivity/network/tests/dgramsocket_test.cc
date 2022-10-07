// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Fuchsia's BSD socket tests ensure that fdio and Netstack together produce
// POSIX-like behavior. This module contains tests that exclusively test
// SOCK_DGRAM sockets.

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

#include <array>
#include <future>
#include <latch>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#if defined(__Fuchsia__)
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/status.h>

#include "src/connectivity/network/netstack/udp_serde/udp_serde.h"
#endif

#include "os.h"
#include "util.h"

// TODO(C++20): Remove this; std::chrono::duration defines operator<< in c++20. See
// https://en.cppreference.com/w/cpp/chrono/duration/operator_ltlt.
namespace std::chrono {
template <class Rep, class Period>
void PrintTo(const std::chrono::duration<Rep, Period>& duration, std::ostream* os) {
  *os << std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() << "ns";
}
}  // namespace std::chrono

namespace {

#if defined(__Fuchsia__)
// Saturates `recvfd`'s receive buffers by writing `sendbuf` to `sendfd` N times using `io_method`.
// `sendbuf` is resized and N picked to ensure that:
//   - `recvfd`'s associated zircon socket contains `sizeof(kernel buf) - remainder` bytes, where
//       0 < `remainder` < payload size.
//   - `recvfd`'s associated Netstack receive buffer contains `M * payload size` bytes, where
//     `M` > 0.
// After completion, payloads read out of `recvfd` may be compared against `sendbuf`.
void FillRxBuffersLeavingRemainderInZirconSocket(const fbl::unique_fd& recvfd,
                                                 const fbl::unique_fd& sendfd,
                                                 const IOMethod& io_method,
                                                 std::vector<char>& sendbuf) {
  if (!std::getenv(kFastUdpEnvVar)) {
    FAIL() << "Zircon sockets are only used in Fast UDP";
  }
  // Start with the maximum datagram payload size, derived as:
  // 65535 bytes (max IP packet size) - 20 bytes (IPv4 header) - 8 bytes (UDP header)
  size_t payload_size = 65507;
  size_t recv_capacity;
  ASSERT_NO_FATAL_FAILURE(RxCapacity(recvfd.get(), recv_capacity));
  zx_info_socket_t zx_socket_info;
  ASSERT_NO_FATAL_FAILURE(ZxSocketInfoDgram(recvfd.get(), zx_socket_info));

  // Pick a payload size which is less than the maximum datagram payload size and ensures
  // that the zircon socket has a remainder, as described above.
  payload_size = std::min(payload_size, zx_socket_info.rx_buf_max - kRxUdpPreludeSize);
  while (payload_size > 0) {
    size_t total_size = payload_size + kRxUdpPreludeSize;
    if (zx_socket_info.rx_buf_max % total_size != 0) {
      break;
    }
    --payload_size;
  }
  if (payload_size == 0) {
    FAIL() << "couldn't find valid UDP payload size for which (zx_socket_info.rx_buf_max % "
              "payload size) != 0";
  }

  // It's possible that the receiver's Netstack receive buffer will fill up even when its zircon
  // socket still has space (because the shuttling routines have lagged). When this happens, the
  // receiver will drop inbound packets; if enough packets are dropped, we might fail to fill up
  // the zircon socket. To avoid this scenario, send significantly more than the receiver's total
  // Rx capacity.
  size_t payload_count = (2 * recv_capacity) / payload_size;

  sendbuf = std::vector<char>(payload_size, 'a');
  while (payload_count > 0) {
    ASSERT_EQ(io_method.ExecuteIO(sendfd.get(), sendbuf.data(), sendbuf.size()),
              ssize_t(sendbuf.size()))
        << strerror(errno);
    --payload_count;
  }
}
#endif  // defined(__Fuchsia__)

void SetUpBoundAndConnectedDatagramSockets(const SocketDomain& domain, fbl::unique_fd& bindfd,
                                           fbl::unique_fd& connectfd) {
  ASSERT_TRUE(bindfd = fbl::unique_fd(socket(domain.Get(), SOCK_DGRAM, 0))) << strerror(errno);

  auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(domain);
  ASSERT_EQ(bind(bindfd.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen), 0)
      << strerror(errno);

  {
    socklen_t bound_addrlen = addrlen;
    ASSERT_EQ(getsockname(bindfd.get(), reinterpret_cast<sockaddr*>(&addr), &bound_addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, bound_addrlen);
  }

  ASSERT_TRUE(connectfd = fbl::unique_fd(socket(domain.Get(), SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(connectfd.get(), reinterpret_cast<sockaddr*>(&addr), addrlen), 0)
      << strerror(errno);
}

void ExpectNoPollin(int fd) {
  pollfd pfd = {
      .fd = fd,
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(std::chrono::seconds(1)).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 0);
}

template <typename T>
void SendWithCmsg(int sock, char* buf, size_t buf_size, int cmsg_level, int cmsg_type,
                  T cmsg_value) {
  iovec iov = {
      .iov_base = buf,
      .iov_len = buf_size,
  };

  std::array<uint8_t, CMSG_SPACE(sizeof(cmsg_value))> control;
  msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control.data(),
      .msg_controllen = CMSG_LEN(sizeof(cmsg_value)),
  };

  // Manually add control message.
  cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  ASSERT_NE(cmsg, nullptr);
  *cmsg = {
      .cmsg_len = CMSG_LEN(sizeof(cmsg_value)),
      .cmsg_level = cmsg_level,
      .cmsg_type = cmsg_type,
  };
  memcpy(CMSG_DATA(cmsg), &cmsg_value, sizeof(cmsg_value));

  const ssize_t r = sendmsg(sock, &msg, 0);
  ASSERT_NE(r, -1) << strerror(errno);
  ASSERT_EQ(r, ssize_t(buf_size));
}

TEST(LocalhostTest, SendToZeroPort) {
  sockaddr_in addr = LoopbackSockaddrV4(0);
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(sendto(fd.get(), nullptr, 0, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            -1);
  ASSERT_EQ(errno, EINVAL) << strerror(errno);

  addr.sin_port = htons(1234);
  ASSERT_EQ(sendto(fd.get(), nullptr, 0, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)),
            0)
      << strerror(errno);
}

TEST(LocalhostTest, DatagramSocketIgnoresMsgWaitAll) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  ASSERT_EQ(recvfrom(recvfd.get(), nullptr, 0, MSG_WAITALL, nullptr, nullptr), -1);
  ASSERT_EQ(errno, EAGAIN) << strerror(errno);

  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, DatagramSocketSendMsgNameLenTooBig) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = {
      .sin_family = AF_INET,
  };

  msghdr msg = {
      .msg_name = &addr,
      .msg_namelen = sizeof(sockaddr_storage) + 1,
  };

  ASSERT_EQ(sendmsg(fd.get(), &msg, 0), -1);
  ASSERT_EQ(errno, EINVAL) << strerror(errno);

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, DatagramSocketAtOOBMark) {
  fbl::unique_fd client;
  ASSERT_TRUE(client = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  // sockatmark is not supported on datagram sockets on Linux or Fuchsia.
  // It is on macOS.
  EXPECT_EQ(sockatmark(client.get()), -1);
  // This should be ENOTTY per POSIX:
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/sockatmark.html
  EXPECT_EQ(errno, ENOTTY) << strerror(errno);
}

TEST(LocalhostTest, BindToDevice) {
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
  {
    int ret = setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, set_dev_unknown, 2);
    if (kIsFuchsia) {
      EXPECT_EQ(ret, 0) << strerror(errno);
    } else {
      // We may get EPERM if we lack sufficient privileges.
      EXPECT_TRUE(ret == 0 || errno == EPERM) << strerror(errno);
    }
  }

  // Bind to unknown name should fail.
  EXPECT_EQ(
      setsockopt(fd.get(), SOL_SOCKET, SO_BINDTODEVICE, set_dev_unknown, sizeof(set_dev_unknown)),
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

TEST(LocalhostTest, IpAddMembershipAny) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  ip_mreqn param = {
      .imr_address =
          {
              .s_addr = htonl(INADDR_ANY),
          },
      .imr_ifindex = 1,
  };
  int n = inet_pton(AF_INET, "224.0.2.1", &param.imr_multiaddr.s_addr);
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  ASSERT_EQ(setsockopt(s.get(), SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0)
      << strerror(errno);

  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, ConnectAFMismatchINET) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  sockaddr_in6 addr = LoopbackSockaddrV6(1337);
  EXPECT_EQ(connect(s.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), -1);
  EXPECT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST(LocalhostTest, ConnectAFMismatchINET6) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(1337);
  EXPECT_EQ(connect(s.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  EXPECT_EQ(close(s.release()), 0) << strerror(errno);
}

TEST(DatagramSocketTest, UnsupportedOps) {
  fbl::unique_fd s;
  ASSERT_TRUE(s = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);
  EXPECT_EQ(listen(s.get(), 0), -1);
  EXPECT_EQ(errno, EOPNOTSUPP) << strerror(errno);
  EXPECT_EQ(accept(s.get(), nullptr, nullptr), -1);
  EXPECT_EQ(errno, EOPNOTSUPP) << strerror(errno);
}

class IOMethodTest : public testing::TestWithParam<IOMethod> {};

TEST_P(IOMethodTest, NullptrFaultDGRAM) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)))
      << strerror(errno);
  const sockaddr_in addr = LoopbackSockaddrV4(1235);

  ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  ASSERT_EQ(connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  DoNullPtrIO(fd, fd, GetParam(), true);
}

INSTANTIATE_TEST_SUITE_P(IOMethodTests, IOMethodTest, testing::ValuesIn(kAllIOMethods),
                         [](const auto info) { return info.param.IOMethodToString(); });

class IOReadingMethodTest : public testing::TestWithParam<IOMethod> {};

TEST_P(IOReadingMethodTest, DatagramSocketErrorWhileBlocked) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  {
    // Connect to an existing remote but on a port that is not being used.
    sockaddr_in addr = LoopbackSockaddrV4(1337);
    ASSERT_EQ(connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
        << strerror(errno);
  }

  std::latch fut_started(1);
  const auto fut = std::async(std::launch::async, [&, read_method = GetParam()]() {
    fut_started.count_down();

    char bytes[1];
    // Block while waiting for data to be received.
    ASSERT_EQ(read_method.ExecuteIO(fd.get(), bytes, sizeof(bytes)), -1);
    ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);
  });
  fut_started.wait();
  ASSERT_NO_FATAL_FAILURE(AssertBlocked(fut));

  {
    // Precondition sanity check: no pending events on the socket.
    pollfd pfd = {
        .fd = fd.get(),
    };
    int n = poll(&pfd, 1, 0);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 0);
  }

  char bytes[1];
  // Send a UDP packet to trigger a port unreachable response.
  ASSERT_EQ(send(fd.get(), bytes, sizeof(bytes), 0), ssize_t(sizeof(bytes))) << strerror(errno);
  // The blocking recv call should terminate with an error.
  ASSERT_EQ(fut.wait_for(kTimeout), std::future_status::ready);

  {
    // Postcondition sanity check: no pending events on the socket, the POLLERR should've been
    // cleared by the read_method call.
    pollfd pfd = {
        .fd = fd.get(),
    };
    int n = poll(&pfd, 1, 0);
    ASSERT_GE(n, 0) << strerror(errno);
    ASSERT_EQ(n, 0);
  }

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(IOReadingMethodTests, IOReadingMethodTest,
                         testing::ValuesIn(kRecvIOMethods),
                         [](const testing::TestParamInfo<IOMethod>& info) {
                           return info.param.IOMethodToString();
                         });

class DatagramSocketErrBase {
 protected:
  static void SetUpSocket(fbl::unique_fd& fd, bool nonblocking) {
    int flags = 0;
    if (nonblocking) {
      flags |= SOCK_NONBLOCK;
    }

    ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM | flags, 0))) << strerror(errno);
    ASSERT_NO_FATAL_FAILURE(BindLoopback(fd));
    ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  }

  static void BindLoopback(const fbl::unique_fd& fd) {
    {
      sockaddr_in addr = LoopbackSockaddrV4(0);
      ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
    }
  }

  static void ConnectTo(const fbl::unique_fd& send_fd, const fbl::unique_fd& fd) {
    {
      sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);
      ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
          << strerror(errno);
      ASSERT_EQ(addrlen, sizeof(sockaddr_in));

      ASSERT_EQ(connect(send_fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
          << strerror(errno);
    }
  }

  static void PollForPollerr(const fbl::unique_fd& fd) {
    pollfd pfd = {
        .fd = fd.get(),
    };
    const int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(pfd.revents & POLLERR, POLLERR);
  }

  static void TriggerICMPUnreachable(const fbl::unique_fd& fd) {
    ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachableNoPoll(fd));
    ASSERT_NO_FATAL_FAILURE(PollForPollerr(fd));
  }

  static void SendToUnreachableAddr(const fbl::unique_fd& fd, bool connect) {
    fbl::unique_fd other_fd;
    ASSERT_NO_FATAL_FAILURE(SetUpSocket(other_fd, false));
    if (connect) {
      ASSERT_NO_FATAL_FAILURE(ConnectTo(fd, other_fd));
    }

    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    ASSERT_EQ(getsockname(other_fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
        << strerror(errno);

    // Closing this socket ensures that `fd` ends up connected to an unbound port.
    ASSERT_EQ(close(other_fd.release()), 0) << strerror(errno);

    char bytes[1];
    EXPECT_EQ(sendto(fd.get(), bytes, sizeof(bytes), 0, reinterpret_cast<const sockaddr*>(&addr),
                     addrlen),
              ssize_t(sizeof(bytes)))
        << strerror(errno);
  }

  static void TriggerICMPUnreachableNoPoll(const fbl::unique_fd& fd) {
    SendToUnreachableAddr(fd, true);
  }

  static void CheckNoPendingEvents(
      const fbl::unique_fd& fd,
      std::chrono::duration<int, std::chrono::milliseconds::period> timeout = {}) {
    {
      pollfd pfd = {
          .fd = fd.get(),
          .events = std::numeric_limits<decltype(pfd.events)>::max() &
                    ~(POLLOUT | POLLWRNORM | POLLWRBAND),
      };
      const int n = poll(&pfd, 1, timeout.count());
      ASSERT_GE(n, 0) << strerror(errno);
      EXPECT_EQ(n, 0);
    }
  }
};

class DatagramSocketErrTest : public DatagramSocketErrBase, public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpSocket(sendfd_, false));
    ASSERT_NO_FATAL_FAILURE(SetUpSocket(recvfd_, false));
  }

  void TearDown() override {
    EXPECT_EQ(close(sendfd_.release()), 0) << strerror(errno);
    EXPECT_EQ(close(recvfd_.release()), 0) << strerror(errno);
  }

  const fbl::unique_fd& sendfd() const { return sendfd_; }

  const fbl::unique_fd& recvfd() const { return recvfd_; }

 private:
  fbl::unique_fd sendfd_;
  fbl::unique_fd recvfd_;
};

TEST_F(DatagramSocketErrTest, IcmpErrorsPropagatedDuringIOSpamSend) {
  // Under the hood, Fuchsia sends datagram payloads using an asynchronous loop routine[1]
  // that consumes errors surfaced by the networking library used internally by the Netstack.
  // This test validates that those errors are correctly propagated to the client (rather than
  // dropped on the floor) by triggering an ICMP error while the loop routine is processing a
  // heavy load of outgoing payloads.
  //
  // The goal is to exercise and validate the following scenario:
  //
  //   1) Client sends payload on a socket
  //   2) ICMP error arrives
  //   3) Loop routine asynchronously enqueues payload into the Netstack and consumes
  //      the ICMP error
  //   4) The error is propagated to the client and the payload is successfully sent
  //
  // [1]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket
  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd().get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(sockaddr_in));

  // Trigger an ICMP error _without_ waiting for POLLERR. This makes it possible
  // for a `send` below to enqueue a payload into the zircon socket before the error
  // is signaled on the socket.
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachableNoPoll(sendfd()));

  size_t total_errors = 0;
  size_t total_sent = 0;
  constexpr char buf[] = "b";
  while (total_errors == 0 || total_sent == 0) {
    ssize_t res =
        sendto(sendfd().get(), buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&addr), addrlen);
    if (res < 0) {
      total_errors++;
      EXPECT_EQ(errno, ECONNREFUSED);
    } else {
      total_sent++;
      EXPECT_EQ(res, ssize_t(sizeof(buf)));
    }
  }

  EXPECT_EQ(total_errors, static_cast<size_t>(1));
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(sendfd()));

  // Expect that the loop routine successfully sent all outgoing packets in addition
  // to returning the error.
  for (size_t i = 0; i < total_sent; i++) {
    char recv_buf[sizeof(buf) + 1];
    EXPECT_EQ(read(recvfd().get(), recv_buf, sizeof(recv_buf)), ssize_t(sizeof(buf)))
        << strerror(errno);
    EXPECT_EQ(std::string_view(recv_buf, sizeof(buf)), std::string_view(buf, sizeof(buf)));
  }
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(recvfd()));
}

TEST_F(DatagramSocketErrTest, IcmpErrorsPropagatedDuringIOSpamRecv) {
  // Under the hood, Fuchsia receives datagram payloads using an asynchronous loop routine[1]
  // that consumes errors surfaced by the networking library used internally by the Netstack.
  // This test validates that those errors are correctly propagated to the client (rather than
  // dropped on the floor) by triggering an ICMP error while the loop routine is processing a
  // heavy load of incoming payloads.
  //
  // The goal is to exercise and validate the following scenario:
  //
  //   1) ICMP error arrives on a socket
  //   2) Payload arrives on a socket
  //   3) Loop routine dequeues payload from the Netstack, consuming the ICMP error
  //   4) Both the payload and the error are propagated to the client
  //
  // [1]: https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket
  ASSERT_NO_FATAL_FAILURE(ConnectTo(sendfd(), recvfd()));
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(recvfd()));
  ASSERT_NO_FATAL_FAILURE(ConnectTo(recvfd(), sendfd()));
  constexpr char buf[] = "b";
  EXPECT_EQ(send(sendfd().get(), buf, sizeof(buf), 0), ssize_t(sizeof(buf))) << strerror(errno);

  size_t total_errors = 0;
  size_t total_received = 0;
  char recv_buf[sizeof(buf) + 1];
  while (total_errors == 0 || total_received == 0) {
    ssize_t res = read(recvfd().get(), recv_buf, sizeof(recv_buf));
    if (res < 0) {
      total_errors++;
      EXPECT_EQ(errno, ECONNREFUSED);
    } else {
      total_received++;
      EXPECT_EQ(res, ssize_t(sizeof(buf)));
      EXPECT_EQ(std::string_view(recv_buf, sizeof(buf)), std::string_view(buf, sizeof(buf)));
    }
  }

  EXPECT_EQ(total_errors, static_cast<size_t>(1));
  EXPECT_EQ(total_received, static_cast<size_t>(1));
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(recvfd()));
}

// Validate that ICMP errors can only be observed on datagram sockets when:
//  (1) the socket is connected, AND
//  (2) the error is triggered by a send to the connected address.
class IcmpErrorTest : public DatagramSocketErrBase, public testing::Test {
 protected:
  void SetUp() override { ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd_, false)); }

  void TearDown() override { EXPECT_EQ(close(fd_.release()), 0) << strerror(errno); }

  const fbl::unique_fd& fd() const { return fd_; }

 private:
  fbl::unique_fd fd_;
};

TEST_F(IcmpErrorTest, ErrObservableWhenConnectedSocketSendsToConnectedAddr) {
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd()));

  char bytes[1];
  EXPECT_EQ(send(fd().get(), bytes, sizeof(bytes), 0), -1);
  EXPECT_EQ(errno, ECONNREFUSED) << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd()));
}

TEST_F(IcmpErrorTest, ErrNotObservableOnUnconnectedSocket) {
  ASSERT_NO_FATAL_FAILURE(SendToUnreachableAddr(fd(), /*connect=*/false));

  // Ensure that there is no error observable on the socket.
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd(), std::chrono::milliseconds(kTimeout)));
}

TEST_F(IcmpErrorTest, ErrNotObservableWhenConnectedSocketSendsToUnconnectedAddr) {
  fbl::unique_fd other_fd;
  ASSERT_TRUE(other_fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(BindLoopback(other_fd));
  ASSERT_NO_FATAL_FAILURE(ConnectTo(fd(), other_fd));

  // Send to a different address than the one the socket is connected to.
  ASSERT_NO_FATAL_FAILURE(SendToUnreachableAddr(fd(), /*connect=*/false));

  // Ensure that there is no error observable on the socket.
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd(), std::chrono::milliseconds(kTimeout)));
  EXPECT_EQ(close(other_fd.release()), 0) << strerror(errno);
}

std::string nonBlockingToString(bool nonblocking) {
  if (nonblocking) {
    return "NonBlocking";
  }
  return "Blocking";
}

class DatagramSocketErrWithNonBlockingOptionTest : public DatagramSocketErrBase,
                                                   public testing::TestWithParam<bool> {};

TEST_P(DatagramSocketErrWithNonBlockingOptionTest, ClearsErrWithGetSockOpt) {
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, GetParam()));
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));

  // Clear error using `getsockopt`.
  int err;
  socklen_t optlen = sizeof(err);
  ASSERT_EQ(getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &err, &optlen), 0) << strerror(errno);
  ASSERT_EQ(optlen, sizeof(err));
  EXPECT_EQ(err, ECONNREFUSED) << strerror(err);

  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSocketErrWithNonBlockingOptionTest,
                         testing::Values(false, true),
                         [](const testing::TestParamInfo<bool>& info) {
                           return nonBlockingToString(info.param);
                         });

using IOMethodNonBlockingOptionParams = std::tuple<IOMethod, bool>;

class DatagramSocketErrWithIOMethodBase : public DatagramSocketErrBase {
 protected:
  static void ExpectConnectionRefusedErr(const fbl::unique_fd& fd, const IOMethod& io_method) {
    char bytes[1];
    EXPECT_EQ(io_method.ExecuteIO(fd.get(), bytes, sizeof(bytes)), -1);
    EXPECT_EQ(errno, ECONNREFUSED) << strerror(errno);
  }
};

class DatagramSocketErrWithIOMethodNonBlockingOptionTest
    : public DatagramSocketErrWithIOMethodBase,
      public testing::TestWithParam<IOMethodNonBlockingOptionParams> {};

TEST_P(DatagramSocketErrWithIOMethodNonBlockingOptionTest, ClearsErrWithIO) {
  fbl::unique_fd fd;
  const auto& [io_method, nonblocking] = GetParam();
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, nonblocking));
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));
  ASSERT_NO_FATAL_FAILURE(ExpectConnectionRefusedErr(fd, io_method));
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST_P(DatagramSocketErrWithIOMethodNonBlockingOptionTest,
       ClearsErrWithIOAfterSendCacheInvalidated) {
  // Datagram sockets using the Fast UDP protocol [1] use a single mechanism to
  // 1) check for errors and 2) check the validity of elements in their cache.
  // Here, we validate that signaled/sticky errors take precedence over cache
  // errors.
  //
  // [1] https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket
  fbl::unique_fd fd;
  const auto& [io_method, nonblocking] = GetParam();
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, nonblocking));
  // Send to an unreachable port, which causes an ICMP error to be
  // returned on the socket. In addition, it causes the socket to cache the
  // destination address.
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));
  // Connecting the socket to a new destination invalidates the cached address.
  ASSERT_NO_FATAL_FAILURE(ConnectTo(fd, fd));
  // Expect socket I/O returns the received error.
  ASSERT_NO_FATAL_FAILURE(ExpectConnectionRefusedErr(fd, io_method));
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

#if defined(__Fuchsia__)
TEST_P(DatagramSocketErrWithIOMethodNonBlockingOptionTest, ClearsErrWithIOAfterTransfer) {
  fbl::unique_fd fd;
  const auto& [io_method, nonblocking] = GetParam();
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, nonblocking));
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));

  // Create a second client from the existing file descriptor, and bind it to a
  // new file descriptor. Now we have two file descriptors in the same process
  // sharing a single netstack socket (and therefore zircon socket).
  zx_handle_t handle;
  zx_status_t status = fdio_fd_transfer(fd.release(), &handle);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
  fbl::unique_fd second_fd;
  status = fdio_fd_create(handle, second_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  // Expect that socket I/O returns the asynchronously received error. In the
  // case of Tx I/O methods, this validates that such errors are returned even
  // when the destination cache [1] is empty.
  //
  // [1] https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket
  ASSERT_NO_FATAL_FAILURE(ExpectConnectionRefusedErr(second_fd, io_method));

  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(second_fd));
  EXPECT_EQ(close(second_fd.release()), 0) << strerror(errno);
}
#endif

std::string IOMethodNonBlockingOptionParamsToString(
    const testing::TestParamInfo<IOMethodNonBlockingOptionParams> info) {
  auto const& [io_method, nonblocking] = info.param;
  std::stringstream s;
  s << nonBlockingToString(nonblocking);
  s << io_method.IOMethodToString();
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSocketErrWithIOMethodNonBlockingOptionTest,
                         testing::Combine(testing::ValuesIn(kAllIOMethods),
                                          testing::Values(false, true)),
                         IOMethodNonBlockingOptionParamsToString);

class DatagramSocketErrWithIOMethodAndReceivedDatagramBase
    : public DatagramSocketErrWithIOMethodBase {
 protected:
  static void ExpectPollin(const fbl::unique_fd& fd) {
    pollfd pfd = {
        .fd = fd.get(),
        .events = POLLIN,
    };
    const int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    ASSERT_GE(n, 0) << strerror(errno);
    EXPECT_EQ(n, 1);
    ASSERT_EQ(pfd.revents & POLLIN, POLLIN)
        << "expect pfd.revents contains POLLIN, found: " << pfd.revents;
  }
};

class DatagramSocketErrWithIOMethodTest
    : public DatagramSocketErrWithIOMethodAndReceivedDatagramBase,
      public testing::TestWithParam<IOMethod> {};

TEST_P(DatagramSocketErrWithIOMethodTest, ClearsErrWithIOAfterDatagramReceived) {
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, false));
  fbl::unique_fd send_fd;
  ASSERT_TRUE(send_fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(ConnectTo(send_fd, fd));

  // Send a datagram to `fd`.
  constexpr char send_buf[] = "abc";
  ASSERT_EQ(send(send_fd.get(), send_buf, sizeof(send_buf), 0), ssize_t(sizeof(send_buf)))
      << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(ExpectPollin(fd));
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));
  ASSERT_NO_FATAL_FAILURE(ExpectConnectionRefusedErr(fd, GetParam()));

  // Now that the error has been consumed, consume the datagram.
  char recv_buf[sizeof(send_buf) + 1];
  ASSERT_EQ(read(fd.get(), recv_buf, sizeof(recv_buf)), ssize_t(sizeof(send_buf)))
      << strerror(errno);
  EXPECT_EQ(std::string_view(recv_buf, sizeof(send_buf)),
            std::string_view(send_buf, sizeof(send_buf)));

  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(send_fd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSocketErrWithIOMethodTest,
                         testing::ValuesIn(kRecvIOMethods),
                         [](const testing::TestParamInfo<IOMethod>& info) {
                           return info.param.IOMethodToString();
                         });

using IOMethodCmsgCacheInvalidationParams = std::tuple<IOMethod, bool>;

class DatagramSocketErrWithIOMethodCmsgCacheInvalidationTest
    : public DatagramSocketErrWithIOMethodAndReceivedDatagramBase,
      public testing::TestWithParam<IOMethodCmsgCacheInvalidationParams> {};

TEST_P(DatagramSocketErrWithIOMethodCmsgCacheInvalidationTest, ClearsErrWithIOWithCmsgCache) {
  // Datagram sockets using the Fast UDP protocol
  // (https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket)
  // use a single mechanism to 1) check for errors and 2) check the validity of elements
  // in their cache. Here, we validate that signaled/sticky errors take precedence
  // over cache errors.
  fbl::unique_fd fd;
  const auto& [io_method, request_cmsg] = GetParam();
  ASSERT_NO_FATAL_FAILURE(SetUpSocket(fd, false));
  fbl::unique_fd send_fd;
  ASSERT_TRUE(send_fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(ConnectTo(send_fd, fd));

  constexpr int kTtl = 42;
  char send_buf[] = "abc";
  ASSERT_EQ(setsockopt(send_fd.get(), SOL_IP, IP_TTL, &kTtl, sizeof(kTtl)), 0) << strerror(errno);
  ASSERT_EQ(send(send_fd.get(), send_buf, sizeof(send_buf), 0), ssize_t(sizeof(send_buf)))
      << strerror(errno);
  char control[CMSG_SPACE(sizeof(kTtl)) + 1];
  char recv_buf[sizeof(send_buf) + 1];
  iovec iovec = {
      .iov_base = recv_buf,
      .iov_len = sizeof(recv_buf),
  };
  msghdr msghdr = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = sizeof(control),
  };

  // Receive a datagram while providing space for control messages. This causes
  // the socket to look up and cache the set of requested control messages.
  EXPECT_EQ(recvmsg(fd.get(), &msghdr, 0), ssize_t(sizeof(send_buf))) << strerror(errno);
  EXPECT_EQ(std::string_view(recv_buf, sizeof(send_buf)),
            std::string_view(send_buf, sizeof(send_buf)));
  EXPECT_EQ(msghdr.msg_controllen, 0u);
  EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);

  ASSERT_EQ(send(send_fd.get(), send_buf, sizeof(send_buf), 0), ssize_t(sizeof(send_buf)))
      << strerror(errno);
  ASSERT_NO_FATAL_FAILURE(ExpectPollin(fd));

  // Send to an unreachable port, which causes an ICMP error to be
  // returned on the socket.
  ASSERT_NO_FATAL_FAILURE(TriggerICMPUnreachable(fd));

  // Requesting a new cmsg invalidates the cache.
  if (request_cmsg) {
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(fd.get(), SOL_IP, IP_RECVTTL, &kOne, sizeof(kOne)), 0) << strerror(errno);
  }

  // Expect socket I/O returns the received error.
  ASSERT_NO_FATAL_FAILURE(ExpectConnectionRefusedErr(fd, io_method));

  msghdr = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = sizeof(control),
  };
  EXPECT_EQ(recvmsg(fd.get(), &msghdr, 0), ssize_t(sizeof(send_buf))) << strerror(errno);
  EXPECT_EQ(std::string_view(recv_buf, sizeof(send_buf)),
            std::string_view(send_buf, sizeof(send_buf)));

  // Expect that a cmsg is returned with the datagram iff it was previously requested.
  if (request_cmsg) {
    EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kTtl)));
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
    ASSERT_NE(cmsg, nullptr);
    EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kTtl)));
    EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
    EXPECT_EQ(cmsg->cmsg_type, IP_TTL);
    int recv_ttl;
    memcpy(&recv_ttl, CMSG_DATA(cmsg), sizeof(recv_ttl));
    EXPECT_EQ(recv_ttl, kTtl);
  } else {
    EXPECT_EQ(msghdr.msg_controllen, 0u);
    EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
  }
  ASSERT_NO_FATAL_FAILURE(CheckNoPendingEvents(fd));
  EXPECT_EQ(close(send_fd.release()), 0) << strerror(errno);
}

std::string IOMethodCmsgCacheInvalidationParamsToString(
    const testing::TestParamInfo<IOMethodCmsgCacheInvalidationParams> info) {
  auto const& [io_method, invalidate_cmsg_cache] = info.param;
  std::stringstream s;
  if (invalidate_cmsg_cache) {
    s << "InvalidCmsgCache";
  } else {
    s << "ValidCmsgCache";
  }
  s << io_method.IOMethodToString();
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSocketErrWithIOMethodCmsgCacheInvalidationTest,
                         testing::Combine(testing::ValuesIn(kRecvIOMethods),
                                          testing::Values(false, true)),
                         IOMethodCmsgCacheInvalidationParamsToString);

class DatagramSendTest : public testing::TestWithParam<IOMethod> {};

TEST_P(DatagramSendTest, SendToIPv4MappedIPv6FromIPv4) {
  auto io_method = GetParam();

  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  sockaddr_in6 addr6 = MapIpv4SockaddrToIpv6Sockaddr(addr);

  switch (io_method.Op()) {
    case IOMethod::Op::SENDTO: {
      ASSERT_EQ(
          sendto(fd.get(), nullptr, 0, 0, reinterpret_cast<const sockaddr*>(&addr6), sizeof(addr6)),
          -1);
      ASSERT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
      break;
    }
    case IOMethod::Op::SENDMSG: {
      msghdr msghdr = {
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
  auto io_method = GetParam();
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  EXPECT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  EXPECT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  EXPECT_EQ(addrlen, sizeof(addr));

  std::string msg = "hello";
  char recvbuf[32] = {};
  iovec iov = {
      .iov_base = msg.data(),
      .iov_len = msg.size(),
  };
  msghdr msghdr = {
      .msg_name = &addr,
      .msg_namelen = addrlen,
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  switch (io_method.Op()) {
    case IOMethod::Op::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&addr),
                       addrlen),
                ssize_t(msg.size()))
          << strerror(errno);
      break;
    }
    case IOMethod::Op::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), ssize_t(msg.size())) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0,
                            SocketType::Dgram(), SocketDomain::IPv4(), kTimeout),
            ssize_t(msg.size()));
  auto success_rcv_duration = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(std::string_view(recvbuf, msg.size()), msg);
  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

  // sendto/sendmsg on connected sockets does accept sockaddr input argument and
  // also lets the dest sockaddr be overridden from what was passed for connect.
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  EXPECT_EQ(connect(sendfd.get(), reinterpret_cast<sockaddr*>(&addr), addrlen), 0)
      << strerror(errno);
  switch (io_method.Op()) {
    case IOMethod::Op::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&addr),
                       addrlen),
                ssize_t(msg.size()))
          << strerror(errno);
      break;
    }
    case IOMethod::Op::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), ssize_t(msg.size())) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0,
                            SocketType::Dgram(), SocketDomain::IPv4(), kTimeout),
            ssize_t(msg.size()));
  EXPECT_EQ(std::string_view(recvbuf, msg.size()), msg);

  // Test sending to an address that is different from what we're connected to.
  //
  // We connect to a port that was emphemerally assigned which may fall anywhere
  // in [16000, UINT16_MAX] on gVisor's netstack-based platforms[1] or
  // [32768, 60999] on Linux platforms[2]. Adding 1 to UINT16_MAX will overflow
  // and result in a new port value of 0 so we always subtract by 1 as both
  // platforms that this test runs on will assign a port that will not
  // "underflow" when subtracting by 1 (as the port is always at least 1).
  // Previously, we added by 1 and this resulted in a test flake on Fuchsia
  // (gVisor netstack-based). See https://fxbug.dev/84431 for more details.
  //
  // [1]:
  // https://github.com/google/gvisor/blob/570ca571805d6939c4c24b6a88660eefaf558ae7/pkg/tcpip/ports/ports.go#L242
  //
  // [2]: default ip_local_port_range setting, as per
  //      https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt
  const uint16_t orig_sin_port = addr.sin_port;
  addr.sin_port = htons(ntohs(orig_sin_port) - 1);
  switch (io_method.Op()) {
    case IOMethod::Op::SENDTO: {
      EXPECT_EQ(sendto(sendfd.get(), msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&addr),
                       addrlen),
                ssize_t(msg.size()))
          << strerror(errno);
      break;
    }
    case IOMethod::Op::SENDMSG: {
      EXPECT_EQ(sendmsg(sendfd.get(), &msghdr, 0), ssize_t(msg.size())) << strerror(errno);
      break;
    }
    default: {
      FAIL() << "unexpected test variant";
      break;
    }
  }
  // Expect blocked receiver and try to recover it by sending a packet to the
  // original connected sockaddr.
  addr.sin_port = orig_sin_port;
  // As we expect failure, to keep the recv wait time minimal, we base it on the time taken for a
  // successful recv.
  EXPECT_EQ(asyncSocketRead(recvfd.get(), sendfd.get(), recvbuf, sizeof(recvbuf), 0,
                            SocketType::Dgram(), SocketDomain::IPv4(), success_rcv_duration * 10),
            0);

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramTest, DatagramSendTest,
                         testing::Values(IOMethod::Op::SENDTO, IOMethod::Op::SENDMSG),
                         [](const testing::TestParamInfo<IOMethod>& info) {
                           return info.param.IOMethodToString();
                         });

TEST(NetDatagramTest, DatagramConnectWrite) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(connect(sendfd.get(), reinterpret_cast<sockaddr*>(&addr), addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(write(sendfd.get(), msg, sizeof(msg)), ssize_t(sizeof(msg))) << strerror(errno);
  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

  pollfd pfd = {
      .fd = recvfd.get(),
      .events = POLLIN,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);
  char buf[sizeof(msg) + 1] = {};
  ASSERT_EQ(read(recvfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
  ASSERT_STREQ(buf, msg);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramPartialRecv) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);

  constexpr std::string_view kTestMsg = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  auto check_recv = [&sendfd, &recvfd, &kTestMsg, &addr, &addrlen](
                        size_t recv_buf_size, int flags, ssize_t expected_recvmsg_returnvalue,
                        int expected_msg_flags) {
    char recv_buf[kTestMsg.size()];

    iovec iov = {
        .iov_base = recv_buf,
        .iov_len = recv_buf_size,
    };
    // TODO(https://github.com/google/sanitizers/issues/1455): The size of this
    // array should be 0 or 1, but ASAN's recvmsg interceptor incorrectly encodes
    // that recvmsg writes [msg_name:][:msg_namelen'] (prime indicates value
    // after recvmsg returns), while the actual behavior is that
    // [msg_name:][:min(msg_namelen, msg_namelen'] is written.
    uint8_t from[sizeof(addr) + 1];
    msghdr msg = {
        .msg_name = from,
        .msg_namelen = sizeof(from),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    ASSERT_EQ(sendto(sendfd.get(), kTestMsg.data(), kTestMsg.size(), 0,
                     reinterpret_cast<sockaddr*>(&addr), addrlen),
              ssize_t(kTestMsg.size()));
    ASSERT_EQ(recvmsg(recvfd.get(), &msg, flags), expected_recvmsg_returnvalue);
    ASSERT_EQ(msg.msg_namelen, sizeof(addr));
    ASSERT_EQ(std::string_view(recv_buf, recv_buf_size), kTestMsg.substr(0, recv_buf_size));
    ASSERT_EQ(msg.msg_flags, expected_msg_flags);
  };

  // Partial read returns partial length and `MSG_TRUNC`.
  ASSERT_NO_FATAL_FAILURE(check_recv(kTestMsg.size() - 1, 0, kTestMsg.size() - 1, MSG_TRUNC));

  // Partial read with `MSG_TRUNC` flags returns full message length and
  // `MSG_TRUNC`.
  ASSERT_NO_FATAL_FAILURE(
      check_recv(kTestMsg.size() - 1, MSG_TRUNC, ssize_t(kTestMsg.size()), MSG_TRUNC));

  // Full read always returns full length and no `MSG_TRUNC`.
  ASSERT_NO_FATAL_FAILURE(check_recv(kTestMsg.size(), 0, ssize_t(kTestMsg.size()), 0));
  ASSERT_NO_FATAL_FAILURE(check_recv(kTestMsg.size(), MSG_TRUNC, ssize_t(kTestMsg.size()), 0));

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);
  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, POLLOUT) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  pollfd pfd = {
      .fd = fd.get(),
      .events = POLLOUT,
  };
  int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
  ASSERT_GE(n, 0) << strerror(errno);
  ASSERT_EQ(n, 1);

  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

// DatagramSendtoRecvfrom tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.
TEST(NetDatagramTest, DatagramSendtoRecvfrom) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in addr = LoopbackSockaddrV4(0);
  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(sendto(sendfd.get(), msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&addr), addrlen),
            ssize_t(sizeof(msg)))
      << strerror(errno);

  char buf[sizeof(msg) + 1] = {};

  sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(
      recvfrom(recvfd.get(), buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&peer), &peerlen),
      ssize_t(sizeof(msg)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  ASSERT_EQ(sendto(recvfd.get(), buf, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&peer), peerlen),
            ssize_t(sizeof(msg)))
      << strerror(errno);

  ASSERT_EQ(
      recvfrom(sendfd.get(), buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&peer), &peerlen),
      ssize_t(sizeof(msg)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  char addrbuf[INET_ADDRSTRLEN], peerbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

// DatagramSendtoRecvfromV6 tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.
TEST(NetDatagramTest, DatagramSendtoRecvfromV6) {
  fbl::unique_fd recvfd;
  ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  sockaddr_in6 addr = LoopbackSockaddrV6(0);
  ASSERT_EQ(bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  const char msg[] = "hello";

  fbl::unique_fd sendfd;
  ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(sendto(sendfd.get(), msg, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&addr), addrlen),
            ssize_t(sizeof(msg)))
      << strerror(errno);

  char buf[sizeof(msg) + 1] = {};

  sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(
      recvfrom(recvfd.get(), buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&peer), &peerlen),
      ssize_t(sizeof(msg)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  ASSERT_EQ(sendto(recvfd.get(), buf, sizeof(msg), 0, reinterpret_cast<sockaddr*>(&peer), peerlen),
            ssize_t(sizeof(msg)))
      << strerror(errno);

  ASSERT_EQ(
      recvfrom(sendfd.get(), buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&peer), &peerlen),
      ssize_t(sizeof(msg)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));
  ASSERT_STREQ(msg, buf);

  char addrbuf[INET6_ADDRSTRLEN], peerbuf[INET6_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin6_family, &addr.sin6_addr, addrbuf, sizeof(addrbuf));
  ASSERT_NE(addrstr, nullptr);
  const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
  ASSERT_NE(peerstr, nullptr);
  ASSERT_STREQ(peerstr, addrstr);

  EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramSendtoV4RecvfromV6) {
  sockaddr_in addr4 = LoopbackSockaddrV4(0);
  sockaddr_in6 addr6 = MapIpv4SockaddrToIpv6Sockaddr(addr4);

  fbl::unique_fd recv_fd;
  ASSERT_TRUE(recv_fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(bind(recv_fd.get(), reinterpret_cast<const sockaddr*>(&addr6), sizeof(addr6)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr6);
  ASSERT_EQ(getsockname(recv_fd.get(), reinterpret_cast<sockaddr*>(&addr6), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr6));

  fbl::unique_fd send_fd;
  ASSERT_TRUE(send_fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  addr4.sin_port = addr6.sin6_port;
  constexpr char send_buf[] = "abc";
  ASSERT_EQ(sendto(send_fd.get(), send_buf, sizeof(send_buf), 0,
                   reinterpret_cast<sockaddr*>(&addr4), addrlen),
            ssize_t(sizeof(send_buf)))
      << strerror(errno);

  char recv_buf[sizeof(send_buf) + 1];
  sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(recvfrom(recv_fd.get(), recv_buf, sizeof(recv_buf), 0,
                     reinterpret_cast<sockaddr*>(&peer), &peerlen),
            ssize_t(sizeof(send_buf)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));

  char addrbuf[INET6_ADDRSTRLEN];
  char peerbuf[INET6_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr6.sin6_family, &addr6.sin6_addr, addrbuf, sizeof(addrbuf));
  const char* peerstr = inet_ntop(peer.sin6_family, &peer.sin6_addr, peerbuf, sizeof(peerbuf));
  EXPECT_STREQ(peerstr, addrstr);

  EXPECT_EQ(close(send_fd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recv_fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, DatagramSendtoV6RecvfromV4) {
  sockaddr_in addr = LoopbackSockaddrV4(0);
  fbl::unique_fd recv_fd;
  ASSERT_TRUE(recv_fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  ASSERT_EQ(bind(recv_fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(recv_fd.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  sockaddr_in6 addr6 = MapIpv4SockaddrToIpv6Sockaddr(addr);

  fbl::unique_fd send_fd;
  ASSERT_TRUE(send_fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

  constexpr char send_buf[] = "abc";
  ASSERT_EQ(sendto(send_fd.get(), send_buf, sizeof(send_buf), 0,
                   reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6)),
            ssize_t(sizeof(send_buf)))
      << strerror(errno);

  char recv_buf[sizeof(send_buf) + 1];
  sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ(recvfrom(recv_fd.get(), recv_buf, sizeof(recv_buf), 0,
                     reinterpret_cast<sockaddr*>(&peer), &peerlen),
            ssize_t(sizeof(send_buf)))
      << strerror(errno);
  ASSERT_EQ(peerlen, sizeof(peer));

  char addrbuf[INET_ADDRSTRLEN];
  char peerbuf[INET_ADDRSTRLEN];
  const char* addrstr = inet_ntop(addr.sin_family, &addr.sin_addr, addrbuf, sizeof(addrbuf));
  const char* peerstr = inet_ntop(peer.sin_family, &peer.sin_addr, peerbuf, sizeof(peerbuf));
  EXPECT_STREQ(peerstr, addrstr);

  EXPECT_EQ(close(send_fd.release()), 0) << strerror(errno);

  EXPECT_EQ(close(recv_fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV4) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  sockaddr_in addr = {
      .sin_family = AF_UNSPEC,
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
                    offsetof(sockaddr_in, sin_family) + sizeof(addr.sin_family)),
            0)
      << strerror(errno);
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(NetDatagramTest, ConnectUnspecV6) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP))) << strerror(errno);

  sockaddr_in6 addr = {
      .sin6_family = AF_UNSPEC,
  };

  EXPECT_EQ(connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr),
                    offsetof(sockaddr_in6, sin6_family) + sizeof(addr.sin6_family)),
            0)
      << strerror(errno);
  EXPECT_EQ(close(fd.release()), 0) << strerror(errno);
}

TEST(IoctlTest, IoctlGetInterfaceFlags) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  ifreq ifr_ntof = {};
  {
    constexpr char name[] = "lo";
    memcpy(ifr_ntof.ifr_name, name, sizeof(name));
  }
  ASSERT_EQ(ioctl(fd.get(), SIOCGIFFLAGS, &ifr_ntof), 0) << strerror(errno);
  const struct {
    std::string name;
    uint16_t bitmask;
    bool value;
  } flags[] = {
      {
          .name = "IFF_UP",
          .bitmask = IFF_UP,
          .value = true,
      },
      {
          .name = "IFF_LOOPBACK",
          .bitmask = IFF_LOOPBACK,
          .value = true,
      },
      {
          .name = "IFF_RUNNING",
          .bitmask = IFF_RUNNING,
          .value = true,
      },
      {
          .name = "IFF_PROMISC",
          .bitmask = IFF_PROMISC,
          .value = false,
      },
  };
  for (const auto& flag : flags) {
    EXPECT_EQ(static_cast<bool>(ifr_ntof.ifr_flags & flag.bitmask), flag.value)
        << std::bitset<16>(ifr_ntof.ifr_flags) << ", " << std::bitset<16>(flag.bitmask);
  }
  // Don't check strict equality of `ifr_ntof.ifr_flags` with expected flag
  // values, except on Fuchsia, because gVisor does not set all the interface
  // flags that Linux does.
  if (kIsFuchsia) {
    uint16_t expected_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING | IFF_MULTICAST;
    ASSERT_EQ(ifr_ntof.ifr_flags, expected_flags)
        << std::bitset<16>(ifr_ntof.ifr_flags) << ", " << std::bitset<16>(expected_flags);
  }
}

TEST(IoctlTest, IoctlGetInterfaceAddressesNullIfConf) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  ASSERT_EQ(ioctl(fd.get(), SIOCGIFCONF, nullptr), -1);
  ASSERT_EQ(errno, EFAULT) << strerror(errno);
}

TEST(IoctlTest, IoctlGetInterfaceAddressesPartialRecord) {
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

  // Get the interface configuration information, but only pass an `ifc_len`
  // large enough to hold a partial `struct ifreq`, and ensure that the buffer
  // is not overwritten.
  constexpr char kGarbage = 0xa;
  ifreq ifr;
  memset(&ifr, kGarbage, sizeof(ifr));
  ifconf ifc = {};
  ifc.ifc_len = sizeof(ifr) - 1;
  ifc.ifc_req = &ifr;

  ASSERT_EQ(ioctl(fd.get(), SIOCGIFCONF, &ifc), 0) << strerror(errno);
  ASSERT_EQ(ifc.ifc_len, 0);
  char* buffer = reinterpret_cast<char*>(&ifr);
  for (size_t i = 0; i < sizeof(ifr); i++) {
    EXPECT_EQ(buffer[i], kGarbage) << i;
  }
}

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
        in_addr loopback_sin_addr = {
            .s_addr = htonl((127 << 24) + (i << 16) + (j << 8) + k),
        };
        const char* loopback_addrstr =
            inet_ntop(AF_INET, &loopback_sin_addr, addrbuf, sizeof(addrbuf));
        ASSERT_NE(nullptr, loopback_addrstr);

        fbl::unique_fd recvfd;
        ASSERT_TRUE(recvfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        sockaddr_in rcv_addr = {
            .sin_family = AF_INET,
            .sin_addr = loopback_sin_addr,
        };
        ASSERT_EQ(
            bind(recvfd.get(), reinterpret_cast<const sockaddr*>(&rcv_addr), sizeof(rcv_addr)), 0)
            << "recvaddr=" << loopback_addrstr << ": " << strerror(errno);

        socklen_t rcv_addrlen = sizeof(rcv_addr);
        ASSERT_EQ(getsockname(recvfd.get(), reinterpret_cast<sockaddr*>(&rcv_addr), &rcv_addrlen),
                  0)
            << strerror(errno);
        ASSERT_EQ(sizeof(rcv_addr), rcv_addrlen);

        fbl::unique_fd sendfd;
        ASSERT_TRUE(sendfd = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);
        sockaddr_in sendto_addr = {
            .sin_family = AF_INET,
            .sin_port = rcv_addr.sin_port,
            .sin_addr = loopback_sin_addr,
        };
        ASSERT_EQ(sendto(sendfd.get(), msg, sizeof(msg), 0,
                         reinterpret_cast<sockaddr*>(&sendto_addr), sizeof(sendto_addr)),
                  ssize_t(sizeof(msg)))
            << "sendtoaddr=" << loopback_addrstr << ": " << strerror(errno);
        EXPECT_EQ(close(sendfd.release()), 0) << strerror(errno);

        pollfd pfd = {
            .fd = recvfd.get(),
            .events = POLLIN,
        };
        int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
        ASSERT_GE(n, 0) << strerror(errno);
        ASSERT_EQ(n, 1);
        char buf[sizeof(msg) + 1] = {};
        ASSERT_EQ(read(recvfd.get(), buf, sizeof(buf)), ssize_t(sizeof(msg))) << strerror(errno);
        ASSERT_STREQ(buf, msg);

        EXPECT_EQ(close(recvfd.release()), 0) << strerror(errno);
      }
    }
  }
}

#if defined(__Fuchsia__)

using DomainAndIOMethod = std::tuple<SocketDomain, IOMethod>;

std::string DomainAndIOMethodToString(const testing::TestParamInfo<DomainAndIOMethod>& info) {
  auto const& [domain, io_method] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << io_method.IOMethodToString();
  return oss.str();
}

class IOSendingMethodTest : public testing::TestWithParam<DomainAndIOMethod> {};

TEST_P(IOSendingMethodTest, CloseTerminatesWithRxZirconSocketRemainder) {
  if (!std::getenv(kFastUdpEnvVar)) {
    GTEST_SKIP() << "Zircon sockets are only used in Fast UDP";
  }
  // Fast datagram sockets on Fuchsia use multiple buffers to store inbound payloads,
  // some of which are in Netstack memory and some of which are in kernel memory.
  // Bytes are shuttled between these buffers using routines that spin until
  // the socket is closed. Furthermore, `close()`ing a socket blocks until all
  // of these routines exit.
  //
  // One edge case arises when the kernel buffers have free space, but not so much
  // space that they can accept the next datagram payload. In this case, the netstack
  // routines need to be smart enough to terminate rather than continually trying to
  // enqueue the next payload in an infinite loop.
  //
  // This test verifies that behavior.
  auto const& [domain, io_method] = GetParam();
  fbl::unique_fd recvfd;
  fbl::unique_fd sendfd;
  ASSERT_NO_FATAL_FAILURE(SetUpBoundAndConnectedDatagramSockets(domain, recvfd, sendfd));

  std::vector<char> buf;
  ASSERT_NO_FATAL_FAILURE(
      FillRxBuffersLeavingRemainderInZirconSocket(recvfd, sendfd, io_method, buf));

  struct close_task {
    const std::string name;
    fbl::unique_fd fd;
    std::optional<int> result;
    std::thread action;
  } close_tasks[] = {
      {.name = "recvfd", .fd = std::move(recvfd)},
      {.name = "sendfd", .fd = std::move(sendfd)},
  };

  std::latch done{std::size(close_tasks)};

  for (close_task& task : close_tasks) {
    task.action = std::thread([&task, &done]() {
      task.result = close(task.fd.release());
      done.count_down();
    });
  }

  // Expect that both calls to `close()` return without blocking indefinitely.
  const auto close_both = std::async(std::launch::async, [&done]() { done.wait(); });
  ASSERT_EQ(close_both.wait_for(kTimeout), std::future_status::ready);

  for (close_task& task : close_tasks) {
    ASSERT_TRUE(task.result.has_value()) << " close(" << task.name << ") failed to terminate";
    ASSERT_EQ(task.result.value(), 0) << " close(" << task.name << ") returned error";
  }

  for (close_task& task : close_tasks) {
    task.action.join();
  }
}

TEST_P(IOSendingMethodTest, ReadWithRxZirconSocketRemainder) {
  if (!std::getenv(kFastUdpEnvVar)) {
    GTEST_SKIP() << "Zircon sockets are only used in Fast UDP";
  }

  // Fast datagram sockets on Fuchsia use multiple buffers to store inbound payloads,
  // some of which are in Netstack memory and some of which are in kernel memory.
  // Bytes are shuttled between these buffers using goroutines.
  //
  // One edge case arises when the kernel buffers have free space, but not so much
  // space that they can accept the next datagram payload. In this case, the netstack
  // routines wait until the kernel object can accept the entire payload, in an
  // operation known as a threshold wait.
  //
  // This test exercises this scenario.
  auto const& [domain, io_method] = GetParam();
  fbl::unique_fd recvfd;
  fbl::unique_fd sendfd;
  ASSERT_NO_FATAL_FAILURE(SetUpBoundAndConnectedDatagramSockets(domain, recvfd, sendfd));

  std::vector<char> buf;
  ASSERT_NO_FATAL_FAILURE(
      FillRxBuffersLeavingRemainderInZirconSocket(recvfd, sendfd, io_method, buf));

  zx_info_socket_t zx_socket_info;
  ASSERT_NO_FATAL_FAILURE(ZxSocketInfoDgram(recvfd.get(), zx_socket_info));

  std::vector<char> recvbuf;
  recvbuf.resize(buf.size() + 1);

  // Read a number of bytes exceeding the capacity of the Rx kernel buffer. For these
  // reads to succeed, the Rx routine must have successfully performed a threshold
  // wait.
  size_t bytes_read = 0;
  while (bytes_read <= zx_socket_info.rx_buf_max) {
    ASSERT_EQ(read(recvfd.get(), recvbuf.data(), recvbuf.size()), ssize_t(buf.size()))
        << strerror(errno);
    EXPECT_EQ(std::string_view(recvbuf.data(), buf.size()),
              std::string_view(buf.data(), buf.size()));
    bytes_read += buf.size();
  }
}

INSTANTIATE_TEST_SUITE_P(IOSendingMethodTests, IOSendingMethodTest,
                         testing::Combine(testing::Values(SocketDomain::IPv4(),
                                                          SocketDomain::IPv6()),
                                          testing::ValuesIn(kSendIOMethods)),
                         DomainAndIOMethodToString);
#endif  // defined(__Fuchsia__)

std::pair<sockaddr_storage, socklen_t> AnySockaddrAndSocklenForDomain(const SocketDomain& domain) {
  sockaddr_storage addr{
      .ss_family = domain.Get(),
  };
  switch (domain.which()) {
    case SocketDomain::Which::IPv4: {
      auto& sin = *reinterpret_cast<sockaddr_in*>(&addr);
      sin.sin_addr.s_addr = htonl(INADDR_ANY);
      return std::make_pair(addr, sizeof(sin));
    }
    case SocketDomain::Which::IPv6: {
      auto& sin6 = *reinterpret_cast<sockaddr_in6*>(&addr);
      sin6.sin6_addr = IN6ADDR_ANY_INIT;
      return std::make_pair(addr, sizeof(sin6));
    }
  }
}

class AllDomainTests : public testing::TestWithParam<SocketDomain> {};

TEST_P(AllDomainTests, SendToAnyAddr) {
  auto const& domain = GetParam();
  fbl::unique_fd recv_fd;
  ASSERT_TRUE(recv_fd = fbl::unique_fd(socket(domain.Get(), SOCK_DGRAM, 0))) << strerror(errno);

  auto [sock_addr, socklen] = AnySockaddrAndSocklenForDomain(domain);
  ASSERT_EQ(bind(recv_fd.get(), reinterpret_cast<const sockaddr*>(&sock_addr), sizeof(sock_addr)),
            0)
      << strerror(errno);

  socklen_t addrlen = socklen;
  ASSERT_EQ(getsockname(recv_fd.get(), reinterpret_cast<sockaddr*>(&sock_addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, socklen);

  const char sendbuf[] = "hello";
  fbl::unique_fd send_fd;
  ASSERT_TRUE(send_fd = fbl::unique_fd(socket(domain.Get(), SOCK_DGRAM, 0))) << strerror(errno);
  ASSERT_EQ(sendto(send_fd.get(), sendbuf, sizeof(sendbuf), 0,
                   reinterpret_cast<sockaddr*>(&sock_addr), socklen),
            ssize_t(sizeof(sendbuf)))
      << strerror(errno);

  char recvbuf[sizeof(sendbuf) + 1];
  ASSERT_EQ(read(recv_fd.get(), recvbuf, sizeof(recvbuf)), ssize_t(sizeof(sendbuf)))
      << strerror(errno);
  ASSERT_STREQ(recvbuf, sendbuf);
}

INSTANTIATE_TEST_SUITE_P(AllDomainTests, AllDomainTests,
                         testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                         [](const auto info) {
                           return std::string(socketDomainToString(info.param));
                         });

class NetDatagramSocketsTestBase {
 protected:
  void SetUpDatagramSockets(const SocketDomain& domain) {
    ASSERT_NO_FATAL_FAILURE(SetUpBoundAndConnectedDatagramSockets(domain, bound_, connected_));
  }

  void TearDownDatagramSockets() {
    EXPECT_EQ(close(connected_.release()), 0) << strerror(errno);
    EXPECT_EQ(close(bound_.release()), 0) << strerror(errno);
  }

  const fbl::unique_fd& bound() const { return bound_; }

  const fbl::unique_fd& connected() const { return connected_; }

 private:
  fbl::unique_fd bound_;
  fbl::unique_fd connected_;
};

enum class SendZeroBytesTestCase {
  NullBuffer,
  ZeroBufferLen,
};

using DomainAndIOMethodAndSendZeroBytesTestCase =
    std::tuple<SocketDomain, IOMethod, SendZeroBytesTestCase>;

std::string DomainAndIOMethodAndSendZeroBytesTestCaseToString(
    const testing::TestParamInfo<DomainAndIOMethodAndSendZeroBytesTestCase>& info) {
  auto const& [domain, io_method, test_case] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << io_method.IOMethodToString();
  switch (test_case) {
    case SendZeroBytesTestCase::NullBuffer:
      oss << '_' << "NullBuffer";
      break;
    case SendZeroBytesTestCase::ZeroBufferLen:
      oss << '_' << "ZeroBufferLen";
      break;
  }
  return oss.str();
}

class IOSendingZeroBytesMethodTest
    : public NetDatagramSocketsTestBase,
      public testing::TestWithParam<DomainAndIOMethodAndSendZeroBytesTestCase> {
  void SetUp() override {
    auto const& [domain, io_method, test_case] = GetParam();
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain));
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(IOSendingZeroBytesMethodTest, ZeroLengthPayload) {
  const auto& [domain, io_method, test_case] = GetParam();
  char buf[1];
  char* data;
  switch (test_case) {
    case SendZeroBytesTestCase::NullBuffer:
      data = nullptr;
      break;
    case SendZeroBytesTestCase::ZeroBufferLen:
      data = buf;
      break;
  }
  EXPECT_EQ(io_method.ExecuteIO(connected().get(), data, 0), 0) << strerror(errno);

  // TODO(https://fxbug.dev/103497): Match Linux behavior when calling `writev` with zero length
  // payloads.
  if (!kIsFuchsia && io_method.Op() == IOMethod::Op::WRITEV) {
    ASSERT_NO_FATAL_FAILURE(ExpectNoPollin(bound().get()));
    return;
  }

  EXPECT_EQ(read(bound().get(), buf, sizeof(buf)), 0);
}

INSTANTIATE_TEST_SUITE_P(IOSendingZeroBytesMethodTests, IOSendingZeroBytesMethodTest,
                         testing::Combine(testing::Values(SocketDomain::IPv4(),
                                                          SocketDomain::IPv6()),
                                          testing::ValuesIn(kSendIOMethods),
                                          testing::Values(SendZeroBytesTestCase::NullBuffer,
                                                          SendZeroBytesTestCase::ZeroBufferLen)),
                         DomainAndIOMethodAndSendZeroBytesTestCaseToString);

enum class SendZeroBytesVectorizedTestCase {
  NullIovecPointer,
  ZeroIovCnt,
};

using DomainAndVectorizedIOMethodAndSendZeroBytesVectorizedTestCase =
    std::tuple<SocketDomain, VectorizedIOMethod, SendZeroBytesVectorizedTestCase>;

std::string DomainAndVectorizedIOMethodAndSendZeroBytesVectorizedTestCaseToString(
    const testing::TestParamInfo<DomainAndVectorizedIOMethodAndSendZeroBytesVectorizedTestCase>&
        info) {
  auto const& [domain, io_method, test_case] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << io_method.IOMethodToString();
  switch (test_case) {
    case SendZeroBytesVectorizedTestCase::NullIovecPointer:
      oss << '_' << "NullIovecPointer";
      break;
    case SendZeroBytesVectorizedTestCase::ZeroIovCnt:
      oss << '_' << "ZeroIovCnt";
      break;
  }
  return oss.str();
}

class VectorizedIOSendingZeroBytesMethodTest
    : public NetDatagramSocketsTestBase,
      public testing::TestWithParam<DomainAndVectorizedIOMethodAndSendZeroBytesVectorizedTestCase> {
  void SetUp() override {
    auto const& [domain, io_method, test_case] = GetParam();
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain));
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(VectorizedIOSendingZeroBytesMethodTest, ZeroLengthPayload) {
  const auto& [domain, io_method, test_case] = GetParam();
  char buf[1];
  iovec* iov_ptr;
  size_t iov_size;
  std::vector<iovec> iovecs;

  switch (test_case) {
    case SendZeroBytesVectorizedTestCase::NullIovecPointer:
      iov_ptr = nullptr;
      iov_size = 0;
      break;
    case SendZeroBytesVectorizedTestCase::ZeroIovCnt:
      iovecs.push_back({
          .iov_base = buf,
          .iov_len = sizeof(buf),
      });
      iov_ptr = iovecs.data();
      iov_size = 0;
      break;
  }

  EXPECT_EQ(io_method.ExecuteIO(connected().get(), iov_ptr, iov_size), 0) << strerror(errno);

  // TODO(https://fxbug.dev/103497): Match Linux behavior when calling `writev` with zero length
  // payloads.
  if (!kIsFuchsia && io_method.Op() == VectorizedIOMethod::Op::WRITEV) {
    ASSERT_NO_FATAL_FAILURE(ExpectNoPollin(bound().get()));
    return;
  }

  EXPECT_EQ(read(bound().get(), buf, sizeof(buf)), 0);
}

INSTANTIATE_TEST_SUITE_P(
    VectorizedIOSendingZeroBytesMethodTests, VectorizedIOSendingZeroBytesMethodTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(VectorizedIOMethod::Op::WRITEV,
                                     VectorizedIOMethod::Op::SENDMSG),
                     testing::Values(SendZeroBytesVectorizedTestCase::NullIovecPointer,
                                     SendZeroBytesVectorizedTestCase::ZeroIovCnt)),
    DomainAndVectorizedIOMethodAndSendZeroBytesVectorizedTestCaseToString);

struct Cmsg {
  Cmsg(int level, std::string level_str, int type, std::string type_str)
      : level(level), level_str(level_str), type(type), type_str(type_str) {}

  int level;
  std::string level_str;
  int type;
  std::string type_str;
};

#define STRINGIFIED_CMSG(level, type) Cmsg(level, #level, type, #type)

struct CmsgSocketOption {
  Cmsg cmsg;
  socklen_t cmsg_size;
  // The option and the control message always share the same level, so we only need the name of the
  // option here.
  int optname_to_enable_receive;
};

std::ostream& operator<<(std::ostream& oss, const CmsgSocketOption& cmsg_opt) {
  return oss << cmsg_opt.cmsg.level_str << '_' << cmsg_opt.cmsg.type_str;
}

class NetDatagramSocketsCmsgTestBase : public NetDatagramSocketsTestBase {
 protected:
  template <typename F>
  void ReceiveAndCheckMessage(const char* sent_buf, ssize_t sent_buf_len, void* control,
                              socklen_t control_len, F check) const {
    ASSERT_NO_FATAL_FAILURE(
        ReceiveAndCheckMessageBase(sent_buf, sent_buf_len, control, control_len, check));
  }

  template <typename F>
  void ReceiveAndCheckMessageBase(const char* sent_buf, ssize_t sent_buf_len, void* control,
                                  socklen_t control_len, F check) const {
    char recv_buf[sent_buf_len + 1];
    iovec iovec = {
        .iov_base = recv_buf,
        .iov_len = sizeof(recv_buf),
    };
    msghdr msghdr = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &iovec,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = control_len,
    };
    ASSERT_EQ(recvmsg(bound().get(), &msghdr, 0), ssize_t(sent_buf_len)) << strerror(errno);
    ASSERT_EQ(memcmp(recv_buf, sent_buf, sent_buf_len), 0);
    check(msghdr);
  }
};

enum class EnableCmsgReceiveTime { AfterSocketSetup, BetweenSendAndRecv };

std::string_view enableCmsgReceiveTimeToString(EnableCmsgReceiveTime enable_cmsg_receive_time) {
  switch (enable_cmsg_receive_time) {
    case EnableCmsgReceiveTime::AfterSocketSetup:
      return "AfterSocketSetup";
    case EnableCmsgReceiveTime::BetweenSendAndRecv:
      return "BetweenSendAndRecv";
  }
}

using SocketDomainAndOptionAndEnableCmsgReceiveTime =
    std::tuple<SocketDomain, CmsgSocketOption, EnableCmsgReceiveTime>;

std::string SocketDomainAndOptionAndEnableCmsgReceiveTimeToString(
    const testing::TestParamInfo<SocketDomainAndOptionAndEnableCmsgReceiveTime>& info) {
  auto const& [domain, cmsg_opt, enable_cmsg_receive_time] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << cmsg_opt;
  oss << '_' << enableCmsgReceiveTimeToString(enable_cmsg_receive_time);
  return oss.str();
}

class NetDatagramSocketsCmsgRecvTestBase : public NetDatagramSocketsCmsgTestBase {
 protected:
  void SetUpDatagramSockets(const SocketDomain& domain,
                            EnableCmsgReceiveTime enable_cmsg_receive_time) {
    enable_cmsg_receive_time_ = enable_cmsg_receive_time;
    ASSERT_NO_FATAL_FAILURE(NetDatagramSocketsCmsgTestBase::SetUpDatagramSockets(domain));
    if (enable_cmsg_receive_time_ == EnableCmsgReceiveTime::AfterSocketSetup) {
      ASSERT_NO_FATAL_FAILURE(EnableReceivingCmsg());
    }
  }

  virtual void EnableReceivingCmsg() const = 0;

  template <typename F>
  void ReceiveAndCheckMessage(const char* sent_buf, ssize_t sent_buf_len, void* control,
                              socklen_t control_len, F check) const {
    if (enable_cmsg_receive_time_ == EnableCmsgReceiveTime::BetweenSendAndRecv) {
      // Ensure the packet is ready to be read by the client when the
      // control message is requested; this lets us test that control
      // messages are applied to all subsequent incoming payloads.
      pollfd pfd = {
          .fd = bound().get(),
          .events = POLLIN,
      };
      int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
      ASSERT_GE(n, 0) << strerror(errno);
      ASSERT_EQ(n, 1);
      ASSERT_EQ(pfd.revents, POLLIN);

      ASSERT_NO_FATAL_FAILURE(EnableReceivingCmsg());
    }
    ASSERT_NO_FATAL_FAILURE(
        ReceiveAndCheckMessageBase(sent_buf, sent_buf_len, control, control_len, check));
  }

  template <typename F>
  void SendAndCheckReceivedMessage(void* control, socklen_t control_len, F check) {
    constexpr char send_buf[] = "hello";

    ASSERT_EQ(send(connected().get(), send_buf, sizeof(send_buf), 0), ssize_t(sizeof(send_buf)))
        << strerror(errno);

    ReceiveAndCheckMessage(send_buf, sizeof(send_buf), control, control_len, check);
  }

 private:
  EnableCmsgReceiveTime enable_cmsg_receive_time_;
};

class NetDatagramSocketsCmsgRecvTest
    : public NetDatagramSocketsCmsgRecvTestBase,
      public testing::TestWithParam<SocketDomainAndOptionAndEnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    auto const& [domain, cmsg_sockopt, enable_cmsg_receive_time] = GetParam();

    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain, enable_cmsg_receive_time));
  }

  void EnableReceivingCmsg() const override {
    auto const& [domain, cmsg_sockopt, enable_cmsg_receive_time] = GetParam();
    // Enable the specified socket option.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), cmsg_sockopt.cmsg.level,
                         cmsg_sockopt.optname_to_enable_receive, &kOne, sizeof(kOne)),
              0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgRecvTest, NullPtrNoControlMessages) {
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(nullptr, 1337, [](msghdr& msghdr) {
    // The msg_controllen field should be reset when the control buffer is null, even when no
    // control messages are enabled.
    EXPECT_EQ(msghdr.msg_controllen, 0u);
    EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, NullControlBuffer) {
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(nullptr, 1337, [](msghdr& msghdr) {
    // The msg_controllen field should be reset when the control buffer is null.
    EXPECT_EQ(msghdr.msg_controllen, 0u);
    EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, OneByteControlLength) {
  char control[1];
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(control, sizeof(control), [](msghdr& msghdr) {
    // If there is not enough space to store the cmsghdr, then nothing is stored.
    EXPECT_EQ(msghdr.msg_controllen, 0u);
    EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, ZeroControlLength) {
  char control[1];
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(control, 0, [](msghdr& msghdr) {
    // The msg_controllen field should remain zero when no messages were written.
    EXPECT_EQ(msghdr.msg_controllen, 0u);
    EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, FailureDoesNotResetControlLength) {
  char recvbuf[1];
  iovec iovec = {
      .iov_base = recvbuf,
      .iov_len = sizeof(recvbuf),
  };
  char control[1337];
  msghdr msghdr = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = sizeof(control),
  };
  ASSERT_EQ(recvmsg(bound().get(), &msghdr, MSG_DONTWAIT), -1);
  EXPECT_EQ(errno, EWOULDBLOCK) << strerror(errno);
  // The msg_controllen field should be left unchanged when recvmsg() fails for any reason.
  EXPECT_EQ(msghdr.msg_controllen, sizeof(control));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, TruncatedMessageMinimumValidSize) {
  // A control message can be truncated if there is at least enough space to store the cmsghdr.
  char control[sizeof(cmsghdr)];
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(control, sizeof(cmsghdr), [](msghdr& msghdr) {
    if (kIsFuchsia) {
      // TODO(https://fxbug.dev/86146): Add support for truncated control messages (MSG_CTRUNC).
      EXPECT_EQ(msghdr.msg_controllen, 0u);
      EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
    } else {
      ASSERT_EQ(msghdr.msg_controllen, sizeof(control));
      EXPECT_EQ(msghdr.msg_flags, MSG_CTRUNC);
      cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
      ASSERT_NE(cmsg, nullptr);
      EXPECT_EQ(cmsg->cmsg_len, sizeof(control));
      auto const& cmsg_sockopt = std::get<1>(GetParam());
      EXPECT_EQ(cmsg->cmsg_level, cmsg_sockopt.cmsg.level);
      EXPECT_EQ(cmsg->cmsg_type, cmsg_sockopt.cmsg.type);
    }
  }));
}

TEST_P(NetDatagramSocketsCmsgRecvTest, TruncatedMessageByOneByte) {
  auto const& cmsg_sockopt = std::get<1>(GetParam());
  char control[CMSG_LEN(cmsg_sockopt.cmsg_size) - 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, socklen_t(sizeof(control)), [&](msghdr& msghdr) {
        if (kIsFuchsia) {
          // TODO(https://fxbug.dev/86146): Add support for truncated control messages (MSG_CTRUNC).
          EXPECT_EQ(msghdr.msg_controllen, 0u);
          EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
        } else {
          ASSERT_EQ(msghdr.msg_controllen, sizeof(control));
          EXPECT_EQ(msghdr.msg_flags, MSG_CTRUNC);
          cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
          ASSERT_NE(cmsg, nullptr);
          EXPECT_EQ(cmsg->cmsg_len, sizeof(control));
          EXPECT_EQ(cmsg->cmsg_level, cmsg_sockopt.cmsg.level);
          EXPECT_EQ(cmsg->cmsg_type, cmsg_sockopt.cmsg.type);
        }
      }));
}

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgRecvTests, NetDatagramSocketsCmsgRecvTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_SOCKET, SO_TIMESTAMP),
                             .cmsg_size = sizeof(timeval),
                             .optname_to_enable_receive = SO_TIMESTAMP,
                         },
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_SOCKET, SO_TIMESTAMPNS),
                             .cmsg_size = sizeof(timespec),
                             .optname_to_enable_receive = SO_TIMESTAMPNS,
                         }),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                     EnableCmsgReceiveTime::BetweenSendAndRecv)),
    SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgRecvIPv4Tests, NetDatagramSocketsCmsgRecvTest,
    testing::Combine(testing::Values(SocketDomain::IPv4()),
                     testing::Values(
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TOS),
                             .cmsg_size = sizeof(uint8_t),
                             .optname_to_enable_receive = IP_RECVTOS,
                         },
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TTL),
                             .cmsg_size = sizeof(int),
                             .optname_to_enable_receive = IP_RECVTTL,
                         }),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                     EnableCmsgReceiveTime::BetweenSendAndRecv)),
    SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgRecvIPv6Tests, NetDatagramSocketsCmsgRecvTest,
    testing::Combine(testing::Values(SocketDomain::IPv6()),
                     testing::Values(
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_TCLASS),
                             .cmsg_size = sizeof(int),
                             .optname_to_enable_receive = IPV6_RECVTCLASS,
                         },
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_HOPLIMIT),
                             .cmsg_size = sizeof(int),
                             .optname_to_enable_receive = IPV6_RECVHOPLIMIT,
                         },
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_PKTINFO),
                             .cmsg_size = sizeof(in6_pktinfo),
                             .optname_to_enable_receive = IPV6_RECVPKTINFO,
                         }),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                     EnableCmsgReceiveTime::BetweenSendAndRecv)),
    SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

// Tests in this suite assume that control messages are requested after setup only. Create
// a new class that can be parameterized in order to fulfill this expectation.
class NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTest : public NetDatagramSocketsCmsgRecvTest {};

TEST_P(NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTest, DisableReceiveSocketOption) {
  // The SetUp enables the receipt of the parametrized control message. Confirm that we initially
  // receive the control message, and then check that disabling the receive option does exactly
  // just that.
  auto const& cmsg_sockopt = std::get<1>(GetParam());

  {
    char control[CMSG_SPACE(cmsg_sockopt.cmsg_size) + 1];
    ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(
        control, socklen_t(sizeof(control)), [cmsg_sockopt](msghdr& msghdr) {
          EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(cmsg_sockopt.cmsg_size));
          cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
          ASSERT_NE(cmsg, nullptr);
          EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(cmsg_sockopt.cmsg_size));
          EXPECT_EQ(cmsg->cmsg_level, cmsg_sockopt.cmsg.level);
          EXPECT_EQ(cmsg->cmsg_type, cmsg_sockopt.cmsg.type);
          EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
        }));
  }

  constexpr int kZero = 0;
  ASSERT_EQ(setsockopt(bound().get(), cmsg_sockopt.cmsg.level,
                       cmsg_sockopt.optname_to_enable_receive, &kZero, sizeof(kZero)),
            0)
      << strerror(errno);

  {
    char control[CMSG_SPACE(cmsg_sockopt.cmsg_size) + 1];
    ASSERT_NO_FATAL_FAILURE(
        SendAndCheckReceivedMessage(control, socklen_t(sizeof(control)), [](msghdr& msghdr) {
          EXPECT_EQ(msghdr.msg_controllen, 0u);
          EXPECT_EQ(CMSG_FIRSTHDR(&msghdr), nullptr);
        }));
  }
}

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTests,
    NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_SOCKET, SO_TIMESTAMP),
                             .cmsg_size = sizeof(timeval),
                             .optname_to_enable_receive = SO_TIMESTAMP,
                         },
                         CmsgSocketOption{
                             .cmsg = STRINGIFIED_CMSG(SOL_SOCKET, SO_TIMESTAMPNS),
                             .cmsg_size = sizeof(timespec),
                             .optname_to_enable_receive = SO_TIMESTAMPNS,
                         }),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup)),
    SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgRequestOnSetupOnlyRecvIPv4Tests,
                         NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTest,
                         testing::Combine(testing::Values(SocketDomain::IPv4()),
                                          testing::Values(
                                              CmsgSocketOption{
                                                  .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TOS),
                                                  .cmsg_size = sizeof(uint8_t),
                                                  .optname_to_enable_receive = IP_RECVTOS,
                                              },
                                              CmsgSocketOption{
                                                  .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TTL),
                                                  .cmsg_size = sizeof(int),
                                                  .optname_to_enable_receive = IP_RECVTTL,
                                              }),
                                          testing::Values(EnableCmsgReceiveTime::AfterSocketSetup)),
                         SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgRequestOnSetupOnlyRecvIPv6Tests,
                         NetDatagramSocketsCmsgRequestOnSetupOnlyRecvTest,
                         testing::Combine(testing::Values(SocketDomain::IPv6()),
                                          testing::Values(
                                              CmsgSocketOption{
                                                  .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_TCLASS),
                                                  .cmsg_size = sizeof(int),
                                                  .optname_to_enable_receive = IPV6_RECVTCLASS,
                                              },
                                              CmsgSocketOption{
                                                  .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_HOPLIMIT),
                                                  .cmsg_size = sizeof(int),
                                                  .optname_to_enable_receive = IPV6_RECVHOPLIMIT,
                                              },
                                              CmsgSocketOption{
                                                  .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_PKTINFO),
                                                  .cmsg_size = sizeof(in6_pktinfo),
                                                  .optname_to_enable_receive = IPV6_RECVPKTINFO,
                                              }),
                                          testing::Values(EnableCmsgReceiveTime::AfterSocketSetup)),
                         SocketDomainAndOptionAndEnableCmsgReceiveTimeToString);

class NetDatagramSocketsCmsgSendTest : public NetDatagramSocketsCmsgTestBase,
                                       public testing::TestWithParam<SocketDomain> {
 protected:
  void SetUp() override { ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(GetParam())); }

  cmsghdr OrdinaryControlMessage() {
    return {
        // SOL_SOCKET/SCM_RIGHTS is used for general cmsg tests, because SOL_SOCKET messages are
        // supported on every socket type, and the SCM_RIGHTS control message is a no-op.
        // https://github.com/torvalds/linux/blob/42eb8fdac2f/net/core/sock.c#L2628
        .cmsg_len = CMSG_LEN(0),
        .cmsg_level = SOL_SOCKET,
        .cmsg_type = SCM_RIGHTS,
    };
  }
};

TEST_P(NetDatagramSocketsCmsgSendTest, NullControlBufferWithNonZeroLength) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = nullptr,
      .msg_controllen = 1,
  };

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), -1);
  ASSERT_EQ(errno, EFAULT) << strerror(errno);
}

TEST_P(NetDatagramSocketsCmsgSendTest, NonNullControlBufferWithZeroLength) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  uint8_t send_control[1];
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = send_control,
      .msg_controllen = 0,
  };

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), ssize_t(sizeof(send_buf)))
      << strerror(errno);

  ASSERT_NO_FATAL_FAILURE(
      ReceiveAndCheckMessage(send_buf, sizeof(send_buf), nullptr, 0, [](msghdr& recv_msghdr) {
        EXPECT_EQ(recv_msghdr.msg_controllen, 0u);
        ASSERT_EQ(CMSG_FIRSTHDR(&recv_msghdr), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgSendTest, ValidCmsg) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  cmsghdr cmsg = OrdinaryControlMessage();
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = &cmsg,
      .msg_controllen = cmsg.cmsg_len,
  };

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), ssize_t(sizeof(send_buf)))
      << strerror(errno);
  uint8_t recv_control[CMSG_SPACE(0)];
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(send_buf, sizeof(send_buf), recv_control,
                                                 sizeof(recv_control), [](msghdr& recv_msghdr) {
                                                   EXPECT_EQ(recv_msghdr.msg_controllen, 0u);
                                                   ASSERT_EQ(CMSG_FIRSTHDR(&recv_msghdr), nullptr);
                                                 }));
}

TEST_P(NetDatagramSocketsCmsgSendTest, CmsgLengthOutOfBounds) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  cmsghdr cmsg = OrdinaryControlMessage();
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = &cmsg,
      .msg_controllen = cmsg.cmsg_len,
  };
  cmsg.cmsg_len++;

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), -1);
  ASSERT_EQ(errno, EINVAL) << strerror(errno);
}

TEST_P(NetDatagramSocketsCmsgSendTest, ControlBufferSmallerThanCmsgHeader) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  cmsghdr cmsg = OrdinaryControlMessage();
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = &cmsg,
      .msg_controllen = sizeof(cmsg) - 1,
  };
  // The control message header would fail basic validation. But because the control buffer length
  // is too small, the control message should be ignored.
  cmsg.cmsg_len = 0;

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), ssize_t(sizeof(send_buf)));
  uint8_t recv_control[CMSG_SPACE(0)];
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(send_buf, sizeof(send_buf), recv_control,
                                                 sizeof(recv_control), [](msghdr& recv_msghdr) {
                                                   EXPECT_EQ(recv_msghdr.msg_controllen, 0u);
                                                   ASSERT_EQ(CMSG_FIRSTHDR(&recv_msghdr), nullptr);
                                                 }));
}

TEST_P(NetDatagramSocketsCmsgSendTest, CmsgLengthSmallerThanCmsgHeader) {
  char send_buf[] = "hello";
  iovec iovec = {
      .iov_base = send_buf,
      .iov_len = sizeof(send_buf),
  };
  cmsghdr cmsg = OrdinaryControlMessage();
  const msghdr send_msghdr = {
      .msg_iov = &iovec,
      .msg_iovlen = 1,
      .msg_control = &cmsg,
      .msg_controllen = cmsg.cmsg_len,
  };
  // It is invalid to have a control message header with a size smaller than itself.
  cmsg.cmsg_len = sizeof(cmsg) - 1;

  ASSERT_EQ(sendmsg(connected().get(), &send_msghdr, 0), -1);
  ASSERT_EQ(errno, EINVAL) << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgSendTests, NetDatagramSocketsCmsgSendTest,
                         testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                         [](const auto info) {
                           return std::string(socketDomainToString(info.param));
                         });

using SocketDomainAndEnableCmsgReceiveTime = std::tuple<SocketDomain, EnableCmsgReceiveTime>;

std::string SocketDomainAndEnableCmsgReceiveTimeToString(
    const testing::TestParamInfo<SocketDomainAndEnableCmsgReceiveTime>& info) {
  auto const& [domain, enable_cmsg_receive_time] = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(domain);
  oss << '_' << enableCmsgReceiveTimeToString(enable_cmsg_receive_time);
  return oss.str();
}

class NetDatagramSocketsCmsgTimestampTest
    : public NetDatagramSocketsCmsgRecvTestBase,
      public testing::TestWithParam<SocketDomainAndEnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    auto [domain, enable_cmsg_receive_time] = GetParam();
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain, enable_cmsg_receive_time));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving SO_TIMESTAMP control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_SOCKET, SO_TIMESTAMP, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override { EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets()); }
};

TEST_P(NetDatagramSocketsCmsgTimestampTest, RecvCmsg) {
  const std::chrono::duration before = std::chrono::system_clock::now().time_since_epoch();
  char control[CMSG_SPACE(sizeof(timeval)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [before](msghdr& msghdr) {
        ASSERT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(timeval)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(timeval)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_SOCKET);
        EXPECT_EQ(cmsg->cmsg_type, SO_TIMESTAMP);

        timeval received_tv;
        memcpy(&received_tv, CMSG_DATA(cmsg), sizeof(received_tv));
        const std::chrono::duration received = std::chrono::seconds(received_tv.tv_sec) +
                                               std::chrono::microseconds(received_tv.tv_usec);
        const std::chrono::duration after = std::chrono::system_clock::now().time_since_epoch();
        // It is possible for the clock to 'jump'. To avoid flakiness, do not check the received
        // timestamp if the clock jumped back in time.
        if (before <= after) {
          ASSERT_GE(received, before);
          ASSERT_LE(received, after);
        }

        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgTimestampTest, RecvCmsgUnalignedControlBuffer) {
  const std::chrono::duration before = std::chrono::system_clock::now().time_since_epoch();
  char control[CMSG_SPACE(sizeof(timeval)) + 1];
  // Pass an unaligned control buffer.
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, CMSG_LEN(sizeof(timeval)), [before](msghdr& msghdr) {
        ASSERT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(timeval)));
        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Do not access the unaligned control header directly as that would be an undefined
        // behavior. Copy the content to a properly aligned variable first.
        char aligned_cmsg[CMSG_SPACE(sizeof(timeval))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(timeval)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_SOCKET);
        EXPECT_EQ(cmsg->cmsg_type, SO_TIMESTAMP);

        timeval received_tv;
        memcpy(&received_tv, CMSG_DATA(cmsg), sizeof(received_tv));
        const std::chrono::duration received = std::chrono::seconds(received_tv.tv_sec) +
                                               std::chrono::microseconds(received_tv.tv_usec);
        const std::chrono::duration after = std::chrono::system_clock::now().time_since_epoch();
        // It is possible for the clock to 'jump'. To avoid flakiness, do not check the received
        // timestamp if the clock jumped back in time.
        if (before <= after) {
          ASSERT_GE(received, before);
          ASSERT_LE(received, after);
        }

        // Note: We can't use CMSG_NXTHDR because:
        // * it *must* take the unaligned cmsghdr pointer from the control buffer.
        // * and it may access its members (cmsg_len), which would be an undefined behavior.
        // So we skip the CMSG_NXTHDR assertion that shows that there is no other control message.
      }));
}

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgTimestampTests, NetDatagramSocketsCmsgTimestampTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                     EnableCmsgReceiveTime::BetweenSendAndRecv)),
    SocketDomainAndEnableCmsgReceiveTimeToString);

class NetDatagramSocketsCmsgTimestampNsTest
    : public NetDatagramSocketsCmsgRecvTestBase,
      public testing::TestWithParam<SocketDomainAndEnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    auto [domain, enable_cmsg_receive_time] = GetParam();
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain, enable_cmsg_receive_time));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving SO_TIMESTAMPNS control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_SOCKET, SO_TIMESTAMPNS, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override { EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets()); }

  // libc++ implementation of chrono' system_clock uses microseconds, so we can't use it to
  // retrieve the current time for nanosecond timestamp tests.
  // https://github.com/llvm-mirror/libcxx/blob/78d6a7767ed/include/chrono#L1574
  // The high_resolution_clock is also not appropriate, because it is an alias on the
  // steady_clock.
  // https://github.com/llvm-mirror/libcxx/blob/78d6a7767ed/include/chrono#L313
  void TimeSinceEpoch(std::chrono::nanoseconds& out) const {
    struct timespec ts;
    ASSERT_EQ(clock_gettime(CLOCK_REALTIME, &ts), 0) << strerror(errno);
    out = std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
  }
};

TEST_P(NetDatagramSocketsCmsgTimestampNsTest, RecvMsg) {
  std::chrono::nanoseconds before;
  ASSERT_NO_FATAL_FAILURE(TimeSinceEpoch(before));
  char control[CMSG_SPACE(sizeof(timespec)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [&](msghdr& msghdr) {
        ASSERT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(timespec)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(timespec)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_SOCKET);
        EXPECT_EQ(cmsg->cmsg_type, SO_TIMESTAMPNS);

        timespec received_ts;
        memcpy(&received_ts, CMSG_DATA(cmsg), sizeof(received_ts));
        const std::chrono::duration received = std::chrono::seconds(received_ts.tv_sec) +
                                               std::chrono::nanoseconds(received_ts.tv_nsec);
        std::chrono::nanoseconds after;
        ASSERT_NO_FATAL_FAILURE(TimeSinceEpoch(after));
        // It is possible for the clock to 'jump'. To avoid flakiness, do not check the received
        // timestamp if the clock jumped back in time.
        if (before <= after) {
          ASSERT_GE(received, before);
          ASSERT_LE(received, after);
        }

        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgTimestampNsTest, RecvCmsgUnalignedControlBuffer) {
  std::chrono::nanoseconds before;
  ASSERT_NO_FATAL_FAILURE(TimeSinceEpoch(before));
  char control[CMSG_SPACE(sizeof(timespec)) + 1];
  // Pass an unaligned control buffer.
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, CMSG_LEN(sizeof(timespec)), [&](msghdr& msghdr) {
        ASSERT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(timespec)));
        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Do not access the unaligned control header directly as that would be an undefined
        // behavior. Copy the content to a properly aligned variable first.
        char aligned_cmsg[CMSG_SPACE(sizeof(timespec))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(timespec)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_SOCKET);
        EXPECT_EQ(cmsg->cmsg_type, SO_TIMESTAMPNS);

        timespec received_tv;
        memcpy(&received_tv, CMSG_DATA(cmsg), sizeof(received_tv));
        const std::chrono::duration received = std::chrono::seconds(received_tv.tv_sec) +
                                               std::chrono::nanoseconds(received_tv.tv_nsec);
        std::chrono::nanoseconds after;
        ASSERT_NO_FATAL_FAILURE(TimeSinceEpoch(after));
        // It is possible for the clock to 'jump'. To avoid flakiness, do not check the received
        // timestamp if the clock jumped back in time.
        if (before <= after) {
          ASSERT_GE(received, before);
          ASSERT_LE(received, after);
        }

        // Note: We can't use CMSG_NXTHDR because:
        // * it *must* take the unaligned cmsghdr pointer from the control buffer.
        // * and it may access its members (cmsg_len), which would be an undefined behavior.
        // So we skip the CMSG_NXTHDR assertion that shows that there is no other control message.
      }));
}

INSTANTIATE_TEST_SUITE_P(
    NetDatagramSocketsCmsgTimestampNsTests, NetDatagramSocketsCmsgTimestampNsTest,
    testing::Combine(testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                     testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                     EnableCmsgReceiveTime::BetweenSendAndRecv)),
    SocketDomainAndEnableCmsgReceiveTimeToString);

class NetDatagramSocketsCmsgIpTosTest : public NetDatagramSocketsCmsgRecvTestBase,
                                        public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv4(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IP_RECVTOS control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IP, IP_RECVTOS, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgIpTosTest, RecvCmsg) {
  constexpr uint8_t tos = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IP, IP_TOS, &tos, sizeof(tos)), 0) << strerror(errno);

  char control[CMSG_SPACE(sizeof(tos)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [tos](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(tos)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(tos)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
        EXPECT_EQ(cmsg->cmsg_type, IP_TOS);
        uint8_t recv_tos;
        memcpy(&recv_tos, CMSG_DATA(cmsg), sizeof(recv_tos));
        EXPECT_EQ(recv_tos, tos);
        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpTosTest, RecvCmsgBufferTooSmallToBePadded) {
  constexpr uint8_t tos = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IP, IP_TOS, &tos, sizeof(tos)), 0) << strerror(errno);

  // This test is only meaningful if the length of the data is not aligned.
  ASSERT_NE(CMSG_ALIGN(sizeof(tos)), sizeof(tos));
  // Add an extra byte in the control buffer. It will be reported in the msghdr controllen field.
  char control[CMSG_LEN(sizeof(tos)) + 1];
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(control, sizeof(control), [](msghdr& msghdr) {
    // There is not enough space in the control buffer for it to be padded by CMSG_SPACE. So we
    // expect the size of the input control buffer in controllen instead. It indicates that every
    // bytes from the control buffer were used.
    EXPECT_EQ(msghdr.msg_controllen, CMSG_LEN(sizeof(tos)) + 1);
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
    ASSERT_NE(cmsg, nullptr);
    EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(tos)));
    EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
    EXPECT_EQ(cmsg->cmsg_type, IP_TOS);
    EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgIpTosTest, SendCmsg) {
  constexpr uint8_t tos = 42;
  char send_buf[] = "hello";
  ASSERT_NO_FATAL_FAILURE(
      SendWithCmsg(connected().get(), send_buf, sizeof(send_buf), SOL_IP, IP_TOS, tos));

  char recv_control[CMSG_SPACE(sizeof(tos)) + 1];
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(
      send_buf, sizeof(send_buf), recv_control, sizeof(recv_control), [tos](msghdr& recv_msghdr) {
        EXPECT_EQ(recv_msghdr.msg_controllen, CMSG_SPACE(sizeof(tos)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&recv_msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(tos)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
        EXPECT_EQ(cmsg->cmsg_type, IP_TOS);
        uint8_t recv_tos;
        memcpy(&recv_tos, CMSG_DATA(cmsg), sizeof(recv_tos));
        if (kIsFuchsia) {
          // TODO(https://fxbug.dev/21106): Support sending SOL_IP -> IP_TOS control message.
          constexpr uint8_t kDefaultTOS = 0;
          EXPECT_EQ(recv_tos, kDefaultTOS);
        } else {
          EXPECT_EQ(recv_tos, tos);
        }
        EXPECT_EQ(CMSG_NXTHDR(&recv_msghdr, cmsg), nullptr);
      }));
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgIpTosTests, NetDatagramSocketsCmsgIpTosTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

class NetDatagramSocketsCmsgIpTtlTest : public NetDatagramSocketsCmsgRecvTestBase,
                                        public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv4(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IP_TTL control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IP, IP_RECVTTL, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgIpTtlTest, RecvCmsg) {
  constexpr int kTtl = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IP, IP_TTL, &kTtl, sizeof(kTtl)), 0)
      << strerror(errno);

  char control[CMSG_SPACE(sizeof(kTtl)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [kTtl](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kTtl)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kTtl)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
        EXPECT_EQ(cmsg->cmsg_type, IP_TTL);
        int recv_ttl;
        memcpy(&recv_ttl, CMSG_DATA(cmsg), sizeof(recv_ttl));
        EXPECT_EQ(recv_ttl, kTtl);
        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpTtlTest, RecvCmsgUnalignedControlBuffer) {
  constexpr int kDefaultTTL = 64;
  char control[CMSG_SPACE(sizeof(kDefaultTTL)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, sizeof(control), [kDefaultTTL](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kDefaultTTL)));

        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Copy the content to a properly aligned variable.
        char aligned_cmsg[CMSG_SPACE(sizeof(kDefaultTTL))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kDefaultTTL)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
        EXPECT_EQ(cmsg->cmsg_type, IP_TTL);
        int recv_ttl;
        memcpy(&recv_ttl, CMSG_DATA(cmsg), sizeof(recv_ttl));
        EXPECT_EQ(recv_ttl, kDefaultTTL);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpTtlTest, SendCmsg) {
  constexpr int kTtl = 42;
  char send_buf[] = "hello";
  ASSERT_NO_FATAL_FAILURE(
      SendWithCmsg(connected().get(), send_buf, sizeof(send_buf), SOL_IP, IP_TTL, kTtl));

  char recv_control[CMSG_SPACE(sizeof(kTtl)) + 1];
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(
      send_buf, sizeof(send_buf), recv_control, sizeof(recv_control), [kTtl](msghdr& recv_msghdr) {
        EXPECT_EQ(recv_msghdr.msg_controllen, CMSG_SPACE(sizeof(kTtl)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&recv_msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kTtl)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IP);
        EXPECT_EQ(cmsg->cmsg_type, IP_TTL);
        int recv_ttl;
        memcpy(&recv_ttl, CMSG_DATA(cmsg), sizeof(recv_ttl));
        EXPECT_EQ(recv_ttl, kTtl);
        EXPECT_EQ(CMSG_NXTHDR(&recv_msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpTtlTest, SendCmsgInvalidValues) {
  // A valid IP_TTL must fit in an single byte and must not be zero.
  // https://github.com/torvalds/linux/blob/f443e374ae1/net/ipv4/ip_sockglue.c#L304
  constexpr std::array<int, 3> kInvalidValues = {-1, 0, 256};

  for (int value : kInvalidValues) {
    SCOPED_TRACE("ttl=" + std::to_string(value));
    char send_buf[] = "hello";
    iovec iov = {
        .iov_base = send_buf,
        .iov_len = sizeof(send_buf),
    };
    std::array<uint8_t, CMSG_SPACE(sizeof(value))> control;
    msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control.data(),
        .msg_controllen = CMSG_LEN(sizeof(value)),
    };
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    ASSERT_NE(cmsg, nullptr);
    *cmsg = {
        .cmsg_len = CMSG_LEN(sizeof(value)),
        .cmsg_level = SOL_IP,
        .cmsg_type = IP_TTL,
    };
    memcpy(CMSG_DATA(cmsg), &value, sizeof(value));
    ASSERT_EQ(sendmsg(connected().get(), &msg, 0), -1);
    ASSERT_EQ(errno, EINVAL) << strerror(errno);
  }
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgIpTtlTests, NetDatagramSocketsCmsgIpTtlTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

class NetDatagramSocketsCmsgIpv6TClassTest : public NetDatagramSocketsCmsgRecvTestBase,
                                             public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv6(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IPV6_TCLASS control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IPV6, IPV6_RECVTCLASS, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgIpv6TClassTest, RecvCmsg) {
  constexpr int kTClass = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IPV6, IPV6_TCLASS, &kTClass, sizeof(kTClass)), 0)
      << strerror(errno);

  char control[CMSG_SPACE(sizeof(kTClass)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [kTClass](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kTClass)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kTClass)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_TCLASS);
        uint8_t recv_tclass;
        memcpy(&recv_tclass, CMSG_DATA(cmsg), sizeof(recv_tclass));
        EXPECT_EQ(recv_tclass, kTClass);
        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpv6TClassTest, RecvCmsgUnalignedControlBuffer) {
  constexpr int kTClass = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IPV6, IPV6_TCLASS, &kTClass, sizeof(kTClass)), 0)
      << strerror(errno);

  char control[CMSG_SPACE(sizeof(kTClass)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, sizeof(control), [kTClass](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kTClass)));

        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Copy the content to a properly aligned variable.
        char aligned_cmsg[CMSG_SPACE(sizeof(kTClass))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kTClass)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_TCLASS);
        uint8_t recv_tclass;
        memcpy(&recv_tclass, CMSG_DATA(cmsg), sizeof(recv_tclass));
        EXPECT_EQ(recv_tclass, kTClass);
      }));
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgIpv6TClassTests,
                         NetDatagramSocketsCmsgIpv6TClassTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

class NetDatagramSocketsCmsgIpv6HopLimitTest
    : public NetDatagramSocketsCmsgRecvTestBase,
      public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv6(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IPV6_HOPLIMIT control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IPV6, IPV6_RECVHOPLIMIT, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgIpv6HopLimitTest, RecvCmsg) {
  constexpr int kHopLimit = 42;
  ASSERT_EQ(
      setsockopt(connected().get(), SOL_IPV6, IPV6_UNICAST_HOPS, &kHopLimit, sizeof(kHopLimit)), 0)
      << strerror(errno);

  char control[CMSG_SPACE(sizeof(kHopLimit)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control, sizeof(control), [kHopLimit](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kHopLimit)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kHopLimit)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_HOPLIMIT);
        int recv_hoplimit;
        memcpy(&recv_hoplimit, CMSG_DATA(cmsg), sizeof(recv_hoplimit));
        EXPECT_EQ(recv_hoplimit, kHopLimit);
        EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpv6HopLimitTest, RecvCmsgUnalignedControlBuffer) {
  constexpr int kDefaultHopLimit = 64;
  char control[CMSG_SPACE(sizeof(kDefaultHopLimit)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, sizeof(control), [kDefaultHopLimit](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(kDefaultHopLimit)));

        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Copy the content to a properly aligned variable.
        char aligned_cmsg[CMSG_SPACE(sizeof(kDefaultHopLimit))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kDefaultHopLimit)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_HOPLIMIT);
        int recv_hoplimit;
        memcpy(&recv_hoplimit, CMSG_DATA(cmsg), sizeof(recv_hoplimit));
        EXPECT_EQ(recv_hoplimit, kDefaultHopLimit);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpv6HopLimitTest, SendCmsg) {
  constexpr int kHopLimit = 42;
  char send_buf[] = "hello";
  ASSERT_NO_FATAL_FAILURE(SendWithCmsg(connected().get(), send_buf, sizeof(send_buf), SOL_IPV6,
                                       IPV6_HOPLIMIT, kHopLimit));

  char recv_control[CMSG_SPACE(sizeof(kHopLimit)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      ReceiveAndCheckMessage(send_buf, sizeof(send_buf), recv_control, sizeof(recv_control),
                             [kHopLimit](msghdr& recv_msghdr) {
                               EXPECT_EQ(recv_msghdr.msg_controllen, CMSG_SPACE(sizeof(kHopLimit)));
                               cmsghdr* cmsg = CMSG_FIRSTHDR(&recv_msghdr);
                               ASSERT_NE(cmsg, nullptr);
                               EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kHopLimit)));
                               EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
                               EXPECT_EQ(cmsg->cmsg_type, IPV6_HOPLIMIT);
                               int recv_hoplimit;
                               memcpy(&recv_hoplimit, CMSG_DATA(cmsg), sizeof(recv_hoplimit));
                               EXPECT_EQ(recv_hoplimit, kHopLimit);
                               EXPECT_EQ(CMSG_NXTHDR(&recv_msghdr, cmsg), nullptr);
                             }));
}

TEST_P(NetDatagramSocketsCmsgIpv6HopLimitTest, SendCmsgDefaultValue) {
  constexpr int kConfiguredHopLimit = 42;
  ASSERT_EQ(setsockopt(connected().get(), SOL_IPV6, IPV6_UNICAST_HOPS, &kConfiguredHopLimit,
                       sizeof(kConfiguredHopLimit)),
            0)
      << strerror(errno);

  char send_buf[] = "hello";
  constexpr int kUseConfiguredHopLimitValue = -1;
  ASSERT_NO_FATAL_FAILURE(SendWithCmsg(connected().get(), send_buf, sizeof(send_buf), SOL_IPV6,
                                       IPV6_HOPLIMIT, kUseConfiguredHopLimitValue));

  char recv_control[CMSG_SPACE(sizeof(kConfiguredHopLimit)) + 1];
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(
      send_buf, sizeof(send_buf), recv_control, sizeof(recv_control),
      [kConfiguredHopLimit](msghdr& recv_msghdr) {
        EXPECT_EQ(recv_msghdr.msg_controllen, CMSG_SPACE(sizeof(kConfiguredHopLimit)));
        cmsghdr* cmsg = CMSG_FIRSTHDR(&recv_msghdr);
        ASSERT_NE(cmsg, nullptr);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(kConfiguredHopLimit)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_HOPLIMIT);
        int recv_hoplimit;
        memcpy(&recv_hoplimit, CMSG_DATA(cmsg), sizeof(recv_hoplimit));
        EXPECT_EQ(recv_hoplimit, kConfiguredHopLimit);
        EXPECT_EQ(CMSG_NXTHDR(&recv_msghdr, cmsg), nullptr);
      }));
}

TEST_P(NetDatagramSocketsCmsgIpv6HopLimitTest, SendCmsgInvalidValues) {
  constexpr std::array<int, 2> kInvalidValues = {-2, 256};

  for (int value : kInvalidValues) {
    SCOPED_TRACE("hoplimit=" + std::to_string(value));
    char send_buf[] = "hello";
    iovec iov = {
        .iov_base = send_buf,
        .iov_len = sizeof(send_buf),
    };
    std::array<uint8_t, CMSG_SPACE(sizeof(value))> control;
    msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control.data(),
        .msg_controllen = CMSG_LEN(sizeof(value)),
    };
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    ASSERT_NE(cmsg, nullptr);
    *cmsg = {
        .cmsg_len = CMSG_LEN(sizeof(value)),
        .cmsg_level = SOL_IPV6,
        .cmsg_type = IPV6_HOPLIMIT,
    };
    memcpy(CMSG_DATA(cmsg), &value, sizeof(value));
    ASSERT_EQ(sendmsg(connected().get(), &msg, 0), -1);
    ASSERT_EQ(errno, EINVAL) << strerror(errno);
  }
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgIpv6HopLimitTests,
                         NetDatagramSocketsCmsgIpv6HopLimitTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

class NetDatagramSocketsCmsgIpv6PktInfoTest : public NetDatagramSocketsCmsgRecvTestBase,
                                              public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv6(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IPV6_PKTINFO control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IPV6, IPV6_RECVPKTINFO, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsCmsgIpv6PktInfoTest, RecvCmsg) {
  char control[CMSG_SPACE(sizeof(in6_pktinfo)) + 1];
  ASSERT_NO_FATAL_FAILURE(SendAndCheckReceivedMessage(control, sizeof(control), [](msghdr& msghdr) {
    EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(in6_pktinfo)));
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
    ASSERT_NE(cmsg, nullptr);
    EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(in6_pktinfo)));
    EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
    EXPECT_EQ(cmsg->cmsg_type, IPV6_PKTINFO);
    in6_pktinfo recv_pktinfo;
    memcpy(&recv_pktinfo, CMSG_DATA(cmsg), sizeof(recv_pktinfo));
    const unsigned int lo_ifindex = if_nametoindex("lo");
    EXPECT_NE(lo_ifindex, 0u) << strerror(errno);
    EXPECT_EQ(recv_pktinfo.ipi6_ifindex, lo_ifindex);
    char buf[INET6_ADDRSTRLEN];
    ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&recv_pktinfo.ipi6_addr))
        << inet_ntop(AF_INET6, &recv_pktinfo.ipi6_addr, buf, sizeof(buf));
    EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
  }));
}

TEST_P(NetDatagramSocketsCmsgIpv6PktInfoTest, RecvCmsgUnalignedControlBuffer) {
  char control[CMSG_SPACE(sizeof(in6_pktinfo)) + 1];
  ASSERT_NO_FATAL_FAILURE(
      SendAndCheckReceivedMessage(control + 1, sizeof(control), [](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(in6_pktinfo)));

        // Fetch back the control buffer and confirm it is unaligned.
        cmsghdr* unaligned_cmsg = CMSG_FIRSTHDR(&msghdr);
        ASSERT_NE(unaligned_cmsg, nullptr);
        ASSERT_NE(reinterpret_cast<uintptr_t>(unaligned_cmsg) % alignof(cmsghdr), 0u);

        // Copy the content to a properly aligned variable.
        char aligned_cmsg[CMSG_SPACE(sizeof(in6_pktinfo))];
        memcpy(&aligned_cmsg, unaligned_cmsg, sizeof(aligned_cmsg));
        cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(aligned_cmsg);
        EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(in6_pktinfo)));
        EXPECT_EQ(cmsg->cmsg_level, SOL_IPV6);
        EXPECT_EQ(cmsg->cmsg_type, IPV6_PKTINFO);
        in6_pktinfo recv_pktinfo;
        memcpy(&recv_pktinfo, CMSG_DATA(cmsg), sizeof(recv_pktinfo));
        const unsigned int lo_ifindex = if_nametoindex("lo");
        EXPECT_NE(lo_ifindex, 0u) << strerror(errno);
        EXPECT_EQ(recv_pktinfo.ipi6_ifindex, lo_ifindex);
        char buf[INET6_ADDRSTRLEN];
        ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&recv_pktinfo.ipi6_addr))
            << inet_ntop(AF_INET6, &recv_pktinfo.ipi6_addr, buf, sizeof(buf));
      }));
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsCmsgIpv6PktInfoTests,
                         NetDatagramSocketsCmsgIpv6PktInfoTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

class NetDatagramSocketsMultipleIpv6CmsgsTest
    : public NetDatagramSocketsCmsgRecvTestBase,
      public testing::TestWithParam<EnableCmsgReceiveTime> {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(SocketDomain::IPv6(), GetParam()));
  }

  void EnableReceivingCmsg() const override {
    // Enable receiving IPV6_HOPLIMIT control message.
    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), SOL_IPV6, IPV6_RECVHOPLIMIT, &kOne, sizeof(kOne)), 0)
        << strerror(errno);

    // Enable receiving IPV6_PKTINFO control message.
    ASSERT_EQ(setsockopt(bound().get(), SOL_IPV6, IPV6_RECVPKTINFO, &kOne, sizeof(kOne)), 0)
        << strerror(errno);
  }

  void TearDown() override {
    if (!IsSkipped()) {
      EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets());
    }
  }
};

TEST_P(NetDatagramSocketsMultipleIpv6CmsgsTest, SendCmsg) {
  char send_buf[] = "hello";
  size_t buf_len = sizeof(send_buf);

  iovec iov = {
      .iov_base = send_buf,
      .iov_len = buf_len,
  };

  constexpr int kHopLimit = 42;
  const unsigned int lo_ifindex = if_nametoindex("lo");
  EXPECT_NE(lo_ifindex, 0u) << strerror(errno);
  const in6_pktinfo pktinfo = {
      .ipi6_addr = IN6ADDR_LOOPBACK_INIT,
      .ipi6_ifindex = lo_ifindex,
  };

  // This buffer needs to be zero-initialized for CMSG_NXTHDR.
  // See CMSG_NXTHDR in https://man7.org/linux/man-pages/man3/cmsg.3.html
  char control[CMSG_SPACE(sizeof(kHopLimit)) + CMSG_SPACE(sizeof(pktinfo))] = {};
  msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = control,
      .msg_controllen = sizeof(control),
  };

  cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  *cmsg = {
      .cmsg_len = CMSG_LEN(sizeof(kHopLimit)),
      .cmsg_level = SOL_IPV6,
      .cmsg_type = IPV6_HOPLIMIT,
  };
  memcpy(CMSG_DATA(cmsg), &kHopLimit, sizeof(kHopLimit));

  cmsg = CMSG_NXTHDR(&msg, cmsg);
  *cmsg = {
      .cmsg_len = CMSG_LEN(sizeof(pktinfo)),
      .cmsg_level = SOL_IPV6,
      .cmsg_type = IPV6_PKTINFO,
  };
  memcpy(CMSG_DATA(cmsg), &pktinfo, sizeof(pktinfo));

  ASSERT_EQ(sendmsg(connected().get(), &msg, 0), ssize_t(buf_len)) << strerror(errno);

  // This buffer needs to be zero-initialized for CMSG_NXTHDR.
  char recv_control[CMSG_SPACE(sizeof(kHopLimit)) + CMSG_SPACE(sizeof(pktinfo))] = {};
  ASSERT_NO_FATAL_FAILURE(ReceiveAndCheckMessage(
      send_buf, buf_len, recv_control, sizeof(recv_control), [kHopLimit, pktinfo](msghdr& msghdr) {
        EXPECT_EQ(msghdr.msg_controllen,
                  CMSG_SPACE(sizeof(kHopLimit) + CMSG_SPACE(sizeof(pktinfo))));
        int hoplimit_count = 0;
        int pktinfo_count = 0;
        for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr); cmsg != nullptr;
             cmsg = CMSG_NXTHDR(&msghdr, cmsg)) {
          const cmsghdr& cmsg_ref = *cmsg;
          EXPECT_EQ(cmsg_ref.cmsg_level, SOL_IPV6);
          switch (cmsg_ref.cmsg_type) {
            case IPV6_HOPLIMIT: {
              EXPECT_EQ(cmsg_ref.cmsg_len, CMSG_LEN(sizeof(kHopLimit)));
              int recv_hoplimit;
              memcpy(&recv_hoplimit, CMSG_DATA(cmsg), sizeof(recv_hoplimit));
              EXPECT_EQ(recv_hoplimit, kHopLimit);
              hoplimit_count++;
              break;
            }
            case IPV6_PKTINFO: {
              EXPECT_EQ(cmsg_ref.cmsg_len, CMSG_LEN(sizeof(pktinfo)));
              in6_pktinfo recv_pktinfo;
              memcpy(&recv_pktinfo, CMSG_DATA(cmsg), sizeof(recv_pktinfo));
              EXPECT_EQ(recv_pktinfo.ipi6_ifindex, pktinfo.ipi6_ifindex);
              char buf[INET6_ADDRSTRLEN];
              ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&recv_pktinfo.ipi6_addr))
                  << "unexpected addr: "
                  << inet_ntop(AF_INET6, &recv_pktinfo.ipi6_addr, buf, sizeof(buf));
              pktinfo_count++;
              break;
            }
            default:
              FAIL() << "unexpected cmsg type: " << cmsg_ref.cmsg_type;
              break;
          }
        }
        EXPECT_EQ(hoplimit_count, 1);
        EXPECT_EQ(pktinfo_count, 1);
      }));
}

INSTANTIATE_TEST_SUITE_P(NetDatagramSocketsMultipleIpv6CmsgsTests,
                         NetDatagramSocketsMultipleIpv6CmsgsTest,
                         testing::Values(EnableCmsgReceiveTime::AfterSocketSetup,
                                         EnableCmsgReceiveTime::BetweenSendAndRecv),
                         [](const auto info) {
                           return std::string(enableCmsgReceiveTimeToString(info.param));
                         });

template <typename Instance, typename Arg>
void ValidateLinearizedSendSemantics(const Arg& arg) {
  // NOTE: our goal here is to exercise the potential race condition in which a setting
  // enabled by a control plane operation is mistakenly applied to a datagram that was
  // previously sent. We do so by repeatedly exercising the loop below. We've parallelized
  // this loop into multiple shards to reduce the overall latency; empirical testing
  // suggested that this sharding produced the highest throughput of [loops / second].
  constexpr size_t kIterationsPerThread = 50;
  constexpr size_t kNumThreads = 10;
  std::vector<std::thread> threads;

  for (size_t i = 0; i < kNumThreads; i++) {
    Instance instance(arg);
    ASSERT_NO_FATAL_FAILURE(instance.SetUpInstance());
    threads.emplace_back([instance = std::move(instance)]() mutable {
      for (size_t i = 0; i < kIterationsPerThread; i++) {
        ASSERT_NO_FATAL_FAILURE(instance.ToggleOn());
        ASSERT_NO_FATAL_FAILURE(instance.SendDatagram());
        ASSERT_NO_FATAL_FAILURE(instance.ToggleOff());
        ASSERT_NO_FATAL_FAILURE(instance.ObserveOn());
      }
      ASSERT_NO_FATAL_FAILURE(instance.TearDownInstance());
    });
  }

  for (std::thread& t : threads) {
    t.join();
  }
}

template <typename Instance, typename Arg>
void ValidateCachedSendSemantics(const Arg& arg) {
  // NOTE: our goal here is to ensure that control plane operations invalidate client side
  // caches, ensuring that subsequent datagrams are processed with the setting enabled by
  // the operation. The loop here is just to ensure we cover all of the transitions
  // (on -> send -> observe -> off -> send -> observe -> on). The number of iterations is
  // basically arbitrary.
  constexpr size_t kIterations = 10;

  Instance instance(arg);
  ASSERT_NO_FATAL_FAILURE(instance.SetUpInstance());

  for (size_t i = 0; i < kIterations; i++) {
    ASSERT_NO_FATAL_FAILURE(instance.ToggleOn());
    ASSERT_NO_FATAL_FAILURE(instance.SendDatagram());
    ASSERT_NO_FATAL_FAILURE(instance.ObserveOn());

    ASSERT_NO_FATAL_FAILURE(instance.ToggleOff());
    ASSERT_NO_FATAL_FAILURE(instance.SendDatagramAndObserveOff());
  }
  ASSERT_NO_FATAL_FAILURE(instance.TearDownInstance());
}

template <typename T>
struct CmsgValues {
  T on;
  T off;
};

template <typename T>
std::ostream& operator<<(std::ostream& oss, const CmsgValues<T>& cmsg_values) {
  oss << "_ValueOn_" << std::to_string(cmsg_values.on) << "_ValueOff_"
      << std::to_string(cmsg_values.off);
  return oss;
}

using cmsgValuesVariant = std::variant<CmsgValues<int>, CmsgValues<uint8_t>>;

struct CmsgLinearizedSendTestCase {
  SocketDomain domain;
  CmsgSocketOption recv_option;
  int send_type;
  cmsgValuesVariant send_values;
};

std::string CmsgLinearizedSendTestCaseToString(
    const testing::TestParamInfo<CmsgLinearizedSendTestCase>& info) {
  auto const& test_case = info.param;
  std::ostringstream oss;
  oss << socketDomainToString(test_case.domain);
  oss << '_' << test_case.recv_option;
  std::visit([&](auto arg) { oss << arg; }, test_case.send_values);
  return oss.str();
}

class DatagramLinearizedSendSemanticsCmsgTestInstance : public NetDatagramSocketsCmsgTestBase {
 public:
  DatagramLinearizedSendSemanticsCmsgTestInstance(const CmsgLinearizedSendTestCase& test_case)
      : test_case_(test_case) {}

  void SetUpInstance() {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(test_case_.domain));

    constexpr int kOne = 1;
    ASSERT_EQ(setsockopt(bound().get(), test_case_.recv_option.cmsg.level,
                         test_case_.recv_option.optname_to_enable_receive, &kOne, sizeof(kOne)),
              0)
        << strerror(errno);
  }

  void TearDownInstance() { EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets()); }

  void ToggleOn() {
    std::visit(
        [&](auto arg) {
          ASSERT_EQ(setsockopt(connected().get(), test_case_.recv_option.cmsg.level,
                               test_case_.send_type, &arg.on, sizeof(arg.on)),
                    0)
              << strerror(errno);
        },
        test_case_.send_values);
  }

  void ToggleOff() {
    std::visit(
        [&](auto arg) {
          ASSERT_EQ(setsockopt(connected().get(), test_case_.recv_option.cmsg.level,
                               test_case_.send_type, &arg.off, sizeof(arg.off)),
                    0)
              << strerror(errno);
        },
        test_case_.send_values);
  }

  void SendDatagram() {
    ASSERT_EQ(send(connected().get(), kBuf.data(), kBuf.size(), 0), ssize_t(kBuf.size()))
        << strerror(errno);
  }

  void ObserveOn() {
    std::visit([&](auto arg) { ASSERT_NO_FATAL_FAILURE(RecvDatagramAndValidateCmsg(arg.on)); },
               test_case_.send_values);
  }

 private:
  template <typename CmsgType>
  void RecvDatagramAndValidateCmsg(CmsgType expected_value) {
    const int cmsg_level = test_case_.recv_option.cmsg.level;
    const int cmsg_type = test_case_.recv_option.cmsg.type;
    char control[CMSG_SPACE(sizeof(expected_value)) + 1];
    ReceiveAndCheckMessage(kBuf.data(), kBuf.size(), control, sizeof(control), [&](msghdr& msghdr) {
      EXPECT_EQ(msghdr.msg_controllen, CMSG_SPACE(sizeof(expected_value)));
      cmsghdr* cmsg = CMSG_FIRSTHDR(&msghdr);
      ASSERT_NE(cmsg, nullptr);
      EXPECT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(expected_value)));
      EXPECT_EQ(cmsg->cmsg_level, cmsg_level);
      EXPECT_EQ(cmsg->cmsg_type, cmsg_type);
      CmsgType found_value;
      memcpy(&found_value, CMSG_DATA(cmsg), sizeof(found_value));
      EXPECT_EQ(found_value, expected_value);
      EXPECT_EQ(CMSG_NXTHDR(&msghdr, cmsg), nullptr);
    });
  }

  static constexpr std::string_view kBuf = "hello";
  CmsgLinearizedSendTestCase test_case_;
};

class DatagramLinearizedSendSemanticsCmsgTest
    : public testing::TestWithParam<CmsgLinearizedSendTestCase> {};

TEST_P(DatagramLinearizedSendSemanticsCmsgTest, Evaluate) {
  ASSERT_NO_FATAL_FAILURE(
      ValidateLinearizedSendSemantics<DatagramLinearizedSendSemanticsCmsgTestInstance>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(DatagramLinearizedSendSemanticsCmsgTests,
                         DatagramLinearizedSendSemanticsCmsgTest,
                         testing::Values(
                             CmsgLinearizedSendTestCase{
                                 .domain = SocketDomain::IPv4(),
                                 .recv_option =
                                     {
                                         .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TOS),
                                         .cmsg_size = sizeof(uint8_t),
                                         .optname_to_enable_receive = IP_RECVTOS,
                                     },
                                 .send_type = IP_TOS,
                                 .send_values = cmsgValuesVariant(CmsgValues<uint8_t>{
                                     .on = 42,
                                     .off = 0,
                                 }),
                             },
                             CmsgLinearizedSendTestCase{
                                 .domain = SocketDomain::IPv4(),
                                 .recv_option =
                                     {
                                         .cmsg = STRINGIFIED_CMSG(SOL_IP, IP_TTL),
                                         .cmsg_size = sizeof(int),
                                         .optname_to_enable_receive = IP_RECVTTL,
                                     },
                                 .send_type = IP_TTL,
                                 .send_values = cmsgValuesVariant(CmsgValues<int>{
                                     .on = 42,
                                     .off = 1,
                                 }),
                             },
                             CmsgLinearizedSendTestCase{
                                 .domain = SocketDomain::IPv6(),
                                 .recv_option =
                                     {
                                         .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_TCLASS),
                                         .cmsg_size = sizeof(int),
                                         .optname_to_enable_receive = IPV6_RECVTCLASS,
                                     },
                                 .send_type = IPV6_TCLASS,
                                 .send_values = cmsgValuesVariant(CmsgValues<int>{
                                     .on = 42,
                                     .off = 0,
                                 }),
                             },
                             CmsgLinearizedSendTestCase{
                                 .domain = SocketDomain::IPv6(),
                                 .recv_option =
                                     {
                                         .cmsg = STRINGIFIED_CMSG(SOL_IPV6, IPV6_HOPLIMIT),
                                         .cmsg_size = sizeof(int),
                                         .optname_to_enable_receive = IPV6_RECVHOPLIMIT,
                                     },
                                 .send_type = IPV6_UNICAST_HOPS,
                                 .send_values = cmsgValuesVariant(CmsgValues<int>{
                                     .on = 42,
                                     .off = 0,
                                 }),
                             }),
                         CmsgLinearizedSendTestCaseToString);

class DatagramSendSemanticsTestInstance : public NetDatagramSocketsTestBase {
 public:
  DatagramSendSemanticsTestInstance(const SocketDomain& domain) : domain_(domain) {}

  virtual void SetUpInstance() {
    ASSERT_NO_FATAL_FAILURE(SetUpDatagramSockets(domain_));
    recvbuf_.resize(kBuf.size() + 1);
  }

  virtual void TearDownInstance() { EXPECT_NO_FATAL_FAILURE(TearDownDatagramSockets()); }

 protected:
  void SendDatagramFrom(int fd) {
    ASSERT_EQ(send(fd, kBuf.data(), kBuf.size(), 0), ssize_t(kBuf.size())) << strerror(errno);
  }
  void RecvDatagramOn(int fd) {
    pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    const int n = poll(&pfd, 1, std::chrono::milliseconds(kTimeout).count());
    EXPECT_GE(n, 0) << strerror(errno);
    EXPECT_EQ(n, 1);
    ASSERT_EQ(read(fd, recvbuf_.data(), recvbuf_.size()), ssize_t(kBuf.size())) << strerror(errno);
    EXPECT_STREQ(kBuf.data(), recvbuf_.data());
  }
  SocketDomain domain_;

  static constexpr std::string_view kBuf = "hello";

 private:
  std::string recvbuf_;
};

class DatagramCachedSendSemanticsTest : public testing::TestWithParam<SocketDomain> {};
class DatagramLinearizedSendSemanticsTest : public testing::TestWithParam<SocketDomain> {};

class DatagramSendSemanticsConnectInstance : public DatagramSendSemanticsTestInstance {
 public:
  DatagramSendSemanticsConnectInstance(const SocketDomain& domain)
      : DatagramSendSemanticsTestInstance(domain) {}

  void SetUpInstance() override {
    DatagramSendSemanticsTestInstance::SetUpInstance();
    const auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(domain_);
    addrlen_ = addrlen;

    // Create a third socket on the system with a distinct bound address. We alternate
    // between connecting the `connected()` socket to this new socket vs the original `bound()`
    // socket. We validate that packets reach the address to which `connected()` was bound
    // when `send()` was called -- even when the socket is re-`connect()`ed elsewhere immediately
    // afterwards.
    ASSERT_TRUE(receiver_fd_ = fbl::unique_fd(socket(domain_.Get(), SOCK_DGRAM, 0)))
        << strerror(errno);
    ASSERT_EQ(bind(receiver_fd_.get(), reinterpret_cast<const sockaddr*>(&addr), addrlen_), 0)
        << strerror(errno);
  }

  void ToggleOn() {
    sockaddr_storage addr;
    ASSERT_NO_FATAL_FAILURE(LoadSockname(receiver_fd_.get(), addr));
    ASSERT_EQ(connect(connected().get(), reinterpret_cast<sockaddr*>(&addr), addrlen_), 0)
        << strerror(errno);
  }

  void SendDatagram() {
    ASSERT_NO_FATAL_FAILURE(DatagramSendSemanticsTestInstance::SendDatagramFrom(connected().get()));
  }

  void ToggleOff() {
    sockaddr_storage addr;
    ASSERT_NO_FATAL_FAILURE(LoadSockname(bound().get(), addr));
    ASSERT_EQ(connect(connected().get(), reinterpret_cast<sockaddr*>(&addr), addrlen_), 0)
        << strerror(errno);
  }

  void ObserveOn() { ASSERT_NO_FATAL_FAILURE(RecvDatagramOn(receiver_fd_.get())); }

  void SendDatagramAndObserveOff() {
    ASSERT_NO_FATAL_FAILURE(SendDatagram());
    ASSERT_NO_FATAL_FAILURE(RecvDatagramOn(bound().get()));
  }

 private:
  void LoadSockname(int fd, sockaddr_storage& addr) {
    socklen_t found_addrlen = addrlen_;
    ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &found_addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(found_addrlen, addrlen_);
  }

  fbl::unique_fd receiver_fd_;
  uint32_t addrlen_;
};

TEST_P(DatagramCachedSendSemanticsTest, Connect) {
  ASSERT_NO_FATAL_FAILURE(
      ValidateCachedSendSemantics<DatagramSendSemanticsConnectInstance>(GetParam()));
}

TEST_P(DatagramLinearizedSendSemanticsTest, Connect) {
  ASSERT_NO_FATAL_FAILURE(
      ValidateLinearizedSendSemantics<DatagramSendSemanticsConnectInstance>(GetParam()));
}

class DatagramSendSemanticsCloseInstance : public DatagramSendSemanticsTestInstance {
 public:
  DatagramSendSemanticsCloseInstance(const SocketDomain& domain)
      : DatagramSendSemanticsTestInstance(domain) {}

  void SetUpInstance() override {
    DatagramSendSemanticsTestInstance::SetUpInstance();
    const auto [addr, addrlen] = LoopbackSockaddrAndSocklenForDomain(domain_);
    addrlen_ = addrlen;
  }

  void ToggleOn() {
    ASSERT_TRUE(other_sender_fd_ = fbl::unique_fd(socket(domain_.Get(), SOCK_DGRAM, 0)))
        << strerror(errno);
    sockaddr_storage addr;
    socklen_t found_addrlen = addrlen_;
    ASSERT_EQ(getsockname(bound().get(), reinterpret_cast<sockaddr*>(&addr), &found_addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(found_addrlen, addrlen_);
    ASSERT_EQ(connect(other_sender_fd_.get(), reinterpret_cast<sockaddr*>(&addr), addrlen_), 0)
        << strerror(errno);
  }

  void SendDatagram() {
    ASSERT_NO_FATAL_FAILURE(
        DatagramSendSemanticsTestInstance::SendDatagramFrom(other_sender_fd_.get()));
  }

  void ToggleOff() { EXPECT_EQ(close(other_sender_fd_.release()), 0) << strerror(errno); }

  void ObserveOn() { ASSERT_NO_FATAL_FAILURE(RecvDatagramOn(bound().get())); }

  void SendDatagramAndObserveOff() {
    ASSERT_EQ(send(other_sender_fd_.get(), kBuf.data(), kBuf.size(), 0), -1);
    EXPECT_EQ(errno, EBADF);
  }

 private:
  fbl::unique_fd other_sender_fd_;
  uint32_t addrlen_;
};

TEST_P(DatagramLinearizedSendSemanticsTest, Close) {
  if (!kIsFuchsia) {
    GTEST_SKIP() << "Linux does not guarantee linearized send semantics with respect to close().";
  }

  ASSERT_NO_FATAL_FAILURE(
      ValidateLinearizedSendSemantics<DatagramSendSemanticsCloseInstance>(GetParam()));
}

TEST_P(DatagramCachedSendSemanticsTest, Close) {
  ASSERT_NO_FATAL_FAILURE(
      ValidateCachedSendSemantics<DatagramSendSemanticsCloseInstance>(GetParam()));
}

class DatagramSendSemanticsIpv6OnlyInstance : public DatagramSendSemanticsTestInstance {
 public:
  DatagramSendSemanticsIpv6OnlyInstance(const SocketDomain& domain)
      : DatagramSendSemanticsTestInstance(domain) {}

  void SetUpInstance() override {
    DatagramSendSemanticsTestInstance::SetUpInstance();
    ASSERT_TRUE(recv_fd_ = fbl::unique_fd(socket(AF_INET, SOCK_DGRAM, 0))) << strerror(errno);

    sockaddr_in recv_addr = {
        .sin_family = AF_INET,
        .sin_addr =
            {
                .s_addr = htonl(INADDR_LOOPBACK),
            },
    };
    socklen_t addrlen = sizeof(recv_addr);
    ASSERT_EQ(bind(recv_fd_.get(), reinterpret_cast<const sockaddr*>(&recv_addr), addrlen), 0)
        << strerror(errno);

    ASSERT_EQ(getsockname(recv_fd_.get(), reinterpret_cast<sockaddr*>(&recv_addr), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(recv_addr));

    ASSERT_TRUE(send_fd_ = fbl::unique_fd(socket(AF_INET6, SOCK_DGRAM, 0))) << strerror(errno);

    // Construct a IPV4 mapped IPV6 address.
    send_addr_ = MapIpv4SockaddrToIpv6Sockaddr(recv_addr);
  }

  void ToggleOn() {
    constexpr int v6_only = 0;
    EXPECT_EQ(setsockopt(send_fd_.get(), IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)), 0)
        << strerror(errno);
  }

  void SendDatagram() {
    ASSERT_EQ(sendto(send_fd_.get(), kBuf.data(), kBuf.size(), 0,
                     reinterpret_cast<sockaddr*>(&send_addr_), sizeof(sockaddr_in6)),
              ssize_t(kBuf.size()))
        << strerror(errno);
  }

  void ToggleOff() {
    constexpr int v6_only = 1;
    EXPECT_EQ(setsockopt(send_fd_.get(), IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)), 0)
        << strerror(errno);
  }

  void ObserveOn() { ASSERT_NO_FATAL_FAILURE(RecvDatagramOn(recv_fd_.get())); }

  void SendDatagramAndObserveOff() {
    ASSERT_EQ(sendto(send_fd_.get(), kBuf.data(), kBuf.size(), 0,
                     reinterpret_cast<sockaddr*>(&send_addr_), sizeof(sockaddr_in6)),
              -1);
    EXPECT_EQ(errno, EHOSTUNREACH);
  }

  void TearDownInstance() override {
    EXPECT_EQ(close(recv_fd_.release()), 0) << strerror(errno);
    EXPECT_EQ(close(send_fd_.release()), 0) << strerror(errno);
    ASSERT_NO_FATAL_FAILURE(DatagramSendSemanticsTestInstance::TearDownInstance());
  }

 private:
  fbl::unique_fd recv_fd_;
  fbl::unique_fd send_fd_;
  sockaddr_in6 send_addr_;
};

TEST_P(DatagramLinearizedSendSemanticsTest, Ipv6Only) {
  if (GetParam().Get() != AF_INET6) {
    GTEST_SKIP() << "IPV6_V6ONLY can only be used on AF_INET6 sockets.";
  }
  // TODO(https://fxbug.dev/96108): Remove this test after setting IPV6_V6ONLY after bind is
  // disallowed on Fuchsia.
  if (!kIsFuchsia) {
    GTEST_SKIP() << "Linux does not support setting IPV6_V6ONLY after a socket has been bound.";
  }

  ASSERT_NO_FATAL_FAILURE(
      ValidateLinearizedSendSemantics<DatagramSendSemanticsIpv6OnlyInstance>(GetParam()));
}

TEST_P(DatagramCachedSendSemanticsTest, Ipv6Only) {
  if (GetParam().Get() != AF_INET6) {
    GTEST_SKIP() << "IPV6_V6ONLY can only be used on AF_INET6 sockets.";
  }
  // TODO(https://fxbug.dev/96108): Remove this test after setting IPV6_V6ONLY after bind is
  // disallowed on Fuchsia.
  if (!kIsFuchsia) {
    GTEST_SKIP() << "Linux does not support setting IPV6_V6ONLY after a socket has been bound.";
  }

  ASSERT_NO_FATAL_FAILURE(
      ValidateCachedSendSemantics<DatagramSendSemanticsIpv6OnlyInstance>(GetParam()));
}

class DatagramSendSemanticsSoBroadcastInstance : public DatagramSendSemanticsTestInstance {
 public:
  DatagramSendSemanticsSoBroadcastInstance(const SocketDomain& domain)
      : DatagramSendSemanticsTestInstance(domain) {}

  void SetUpInstance() override {
    DatagramSendSemanticsTestInstance::SetUpInstance();
    recv_addr_ = {
        .sin_family = AF_INET,
        .sin_addr =
            {
                .s_addr = htonl(INADDR_BROADCAST),
            },
    };
    ASSERT_TRUE(recv_fd_ = fbl::unique_fd(socket(domain_.Get(), SOCK_DGRAM, 0))) << strerror(errno);

    ASSERT_EQ(
        bind(recv_fd_.get(), reinterpret_cast<const sockaddr*>(&recv_addr_), sizeof(recv_addr_)), 0)
        << strerror(errno);

    socklen_t addrlen = sizeof(recv_addr_);
    ASSERT_EQ(getsockname(recv_fd_.get(), reinterpret_cast<sockaddr*>(&recv_addr_), &addrlen), 0)
        << strerror(errno);
    ASSERT_EQ(addrlen, sizeof(recv_addr_));

    ASSERT_TRUE(send_fd_ = fbl::unique_fd(socket(domain_.Get(), SOCK_DGRAM, 0))) << strerror(errno);
  }

  void ToggleOn() {
    constexpr int so_broadcast = 1;
    EXPECT_EQ(
        setsockopt(send_fd_.get(), SOL_SOCKET, SO_BROADCAST, &so_broadcast, sizeof(so_broadcast)),
        0)
        << strerror(errno);
  }

  void SendDatagram() {
    ASSERT_EQ(sendto(send_fd_.get(), kBuf.data(), kBuf.size(), 0,
                     reinterpret_cast<sockaddr*>(&recv_addr_), sizeof(recv_addr_)),
              ssize_t(kBuf.size()))
        << strerror(errno);
  }

  void ToggleOff() {
    constexpr int so_broadcast = 0;
    EXPECT_EQ(
        setsockopt(send_fd_.get(), SOL_SOCKET, SO_BROADCAST, &so_broadcast, sizeof(so_broadcast)),
        0)
        << strerror(errno);
  }

  void ObserveOn() { ASSERT_NO_FATAL_FAILURE(RecvDatagramOn(recv_fd_.get())); }

  void SendDatagramAndObserveOff() {
    ASSERT_EQ(sendto(send_fd_.get(), kBuf.data(), kBuf.size(), 0,
                     reinterpret_cast<sockaddr*>(&recv_addr_), sizeof(recv_addr_)),
              -1);
    EXPECT_EQ(errno, EACCES);
  }

  void TearDownInstance() override {
    EXPECT_EQ(close(recv_fd_.release()), 0) << strerror(errno);
    EXPECT_EQ(close(send_fd_.release()), 0) << strerror(errno);
    ASSERT_NO_FATAL_FAILURE(DatagramSendSemanticsTestInstance::TearDownInstance());
  }

 private:
  fbl::unique_fd recv_fd_;
  fbl::unique_fd send_fd_;
  sockaddr_in recv_addr_;
};

TEST_P(DatagramLinearizedSendSemanticsTest, SoBroadcast) {
  if (GetParam().Get() != AF_INET) {
    GTEST_SKIP() << "SO_BROADCAST can only be used on AF_INET sockets.";
  }
  ASSERT_NO_FATAL_FAILURE(
      ValidateLinearizedSendSemantics<DatagramSendSemanticsSoBroadcastInstance>(GetParam()));
}

TEST_P(DatagramCachedSendSemanticsTest, SoBroadcast) {
  if (GetParam().Get() != AF_INET) {
    GTEST_SKIP() << "SO_BROADCAST can only be used on AF_INET sockets.";
  }
  ASSERT_NO_FATAL_FAILURE(
      ValidateCachedSendSemantics<DatagramSendSemanticsSoBroadcastInstance>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(DatagramCachedSendSemanticsTests, DatagramCachedSendSemanticsTest,
                         testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                         [](const auto info) {
                           return std::string(socketDomainToString(info.param));
                         });

INSTANTIATE_TEST_SUITE_P(DatagramLinearizedSendSemanticsTests, DatagramLinearizedSendSemanticsTest,
                         testing::Values(SocketDomain::IPv4(), SocketDomain::IPv6()),
                         [](const auto info) {
                           return std::string(socketDomainToString(info.param));
                         });

}  // namespace
