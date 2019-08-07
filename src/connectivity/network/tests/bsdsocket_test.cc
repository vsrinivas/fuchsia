// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <time.h>

#include <future>
#include <thread>

#include "gtest/gtest.h"
#include "util.h"

namespace netstack {

TEST(LocalhostTest, IP_ADD_MEMBERSHIP_INADDR_ANY) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  ip_mreqn param = {};
  param.imr_ifindex = 1;
  param.imr_multiaddr.s_addr = inet_addr("224.0.2.1");
  param.imr_address.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(setsockopt(s, SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0) << strerror(errno);
}

TEST(LocalhostTest, IP_MULTICAST_IF_ifindex) {
  int s;
  ASSERT_GE(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0) << strerror(errno);

  ip_mreqn param_in = {};
  param_in.imr_ifindex = 1;
  ASSERT_EQ(setsockopt(s, SOL_IP, IP_MULTICAST_IF, &param_in, sizeof(param_in)), 0)
      << strerror(errno);

  in_addr param_out = {};
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s, SOL_IP, IP_MULTICAST_IF, &param_in, &len), 0) << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  ASSERT_EQ(param_out.s_addr, INADDR_ANY);
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
  int server_fd;
  ASSERT_GE(server_fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(server_fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), 0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(listen(server_fd, 1), 0) << strerror(errno);

  int client_fd;
  ASSERT_GE(client_fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  addr.sin_port++;

  ASSERT_EQ(connect(client_fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)), -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);

  addr.sin_port--;

  // TODO(tamird): decide if we want to match Linux's behaviour.
  ASSERT_EQ(connect(client_fd, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
#if defined(__linux__)
            0)
      << strerror(errno);
#else
            -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);
#endif

  ASSERT_EQ(close(server_fd), 0) << strerror(errno);
  ASSERT_EQ(close(client_fd), 0) << strerror(errno);
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

TEST_P(TimeoutSockoptsTest, Timeout) {
  int optname = GetParam();
  ASSERT_TRUE(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO);

  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(0),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0) << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(acptfd, 1), 0) << strerror(errno);

  int client_fd;
  ASSERT_GE(client_fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);
  ASSERT_EQ(connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr)), 0) << strerror(errno);

  int server_fd;
  ASSERT_GE(server_fd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  // We're done with the listener.
  EXPECT_EQ(close(acptfd), 0) << strerror(errno);

  if (optname == SO_SNDTIMEO) {
    // We're about to fill the send buffer; shrink it and the other side's receive buffer to the
    // minimum allowed.
    int opt = 1;
    socklen_t optlen = sizeof(opt);

    EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &opt, optlen), 0) << strerror(errno);
    EXPECT_EQ(setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &opt, optlen), 0) << strerror(errno);

    EXPECT_EQ(getsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &opt, &optlen), 0) << strerror(errno);
    EXPECT_EQ(optlen, sizeof(opt));

    // Now that the buffers involved are minimal, we can temporarily make the socket non-blocking on
    // Linux without introducing flakiness. We can't do that on Fuchsia because of the asynchronous
    // copy from the zircon socket to the "real" send buffer, which takes a bit of time, so we use
    // a small timeout which was empirically tested to ensure no flakiness is introduced.
#if defined(__linux__)
    int flags;
    EXPECT_GE(flags = fcntl(client_fd, F_GETFL), 0) << strerror(errno);
    EXPECT_EQ(fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0) << strerror(errno);
#else
    const struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 1 << 16,  // ~65ms
    };
    EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, optname, &tv, sizeof(tv)), 0) << strerror(errno);
#endif

    // buf size should be neither too small in which case too many writes operation is required
    // to fill out the sending buffer nor too big in which case a big stack is needed for the buf
    // array.
    char buf[opt];
    int size = 0;
    int cnt = 0;
    while ((size = write(client_fd, buf, sizeof(buf))) > 0) {
      cnt += size;
    }
    EXPECT_GT(cnt, 0);
    ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);

#if defined(__linux__)
    EXPECT_EQ(fcntl(client_fd, F_SETFL, flags), 0) << strerror(errno);
#endif
  }

  // We want this to be a small number so the test is fast, but at least 1
  // second so that we exercise `tv_sec`.
  const auto timeout = std::chrono::seconds(1) + std::chrono::milliseconds(50);
  {
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const struct timeval tv = {
        .tv_sec = sec.count(),
        .tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(timeout - sec).count(),
    };
    EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, optname, &tv, sizeof(tv)), 0) << strerror(errno);
  }

  const auto margin = std::chrono::milliseconds(50);

  char buf[16];

  // Perform the read/write. This is the core of the test - we expect the operation to time out
  // per our setting of the timeout above.
  //
  // The operation is performed asynchronously so that in the event of regression, this test can
  // fail gracefully rather than deadlocking.
  {
    const auto fut = std::async(std::launch::async, [=, &buf]() {
      const auto start = std::chrono::steady_clock::now();

      switch (optname) {
        case SO_RCVTIMEO:
          EXPECT_EQ(read(client_fd, buf, sizeof(buf)), -1);
          break;
        case SO_SNDTIMEO:
          EXPECT_EQ(write(client_fd, buf, sizeof(buf)), -1);
          break;
      }
      ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);

      const auto elapsed = std::chrono::steady_clock::now() - start;

      // Check that the actual time waited was close to the expectation.
      const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
      EXPECT_LT(elapsed, timeout + margin)
          << "elapsed=" << elapsed_ms.count() << "ms (which is not within " << margin.count()
          << "ms of " << std::chrono::milliseconds(timeout).count() << "ms)";
      EXPECT_GT(elapsed, timeout - margin)
          << "elapsed=" << elapsed_ms.count() << "ms (which is not within " << margin.count()
          << "ms of " << std::chrono::milliseconds(timeout).count() << "ms)";
    });
    EXPECT_EQ(fut.wait_for(timeout + 2 * margin), std::future_status::ready);
  }

  // Remove the timeout
  const struct timeval tv = {};
  EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, optname, &tv, sizeof(tv)), 0) << strerror(errno);

  // Wrap the read/write in a future to enable a timeout. We expect the future
  // to time out.
  {
    const auto fut = std::async(std::launch::async, [=, &buf]() {
      switch (optname) {
        case SO_RCVTIMEO:
          EXPECT_EQ(read(client_fd, buf, sizeof(buf)), 0) << strerror(errno);
          break;
        case SO_SNDTIMEO:
          EXPECT_EQ(write(client_fd, buf, sizeof(buf)), -1);

// TODO(NET-2462): Investigate why different errnos are returned
// for Linux and Fuchsia.
#if defined(__linux__)
          EXPECT_EQ(errno, ECONNRESET) << strerror(errno);
#else
          EXPECT_EQ(errno, EPIPE) << strerror(errno);
#endif
          break;
      }
    });
    EXPECT_EQ(fut.wait_for(margin), std::future_status::timeout);

    // Closing the remote end should cause the read/write to complete.
    EXPECT_EQ(close(server_fd), 0) << strerror(errno);

    EXPECT_EQ(fut.wait_for(margin), std::future_status::ready);
  }

  EXPECT_EQ(close(client_fd), 0) << strerror(errno);
}

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
  SENDTO,
  SENDMSG,
};

// Use this routine to test blocking socket reads. On failure, this attempts to recover the blocked
// thread.
// Return value:
//      (1) actual length of read data on successful recv
//      (2) 0, when we abort a blocked recv
//      (3) -1, on failure of both of the above operations.
static ssize_t asyncSocketRead(int recvfd, int sendfd, char* buf, ssize_t len, int flags,
                               struct sockaddr_in* addr, socklen_t* addrlen, int socketType) {
  std::future<ssize_t> recv = std::async(std::launch::async, [recvfd, buf, len, flags]() {
    ssize_t readlen;
    memset(buf, 0, len);
    readlen = recvfrom(recvfd, buf, len, flags, nullptr, nullptr);
    return readlen;
  });

  if (recv.wait_for(std::chrono::milliseconds(kTimeout)) == std::future_status::ready) {
    return recv.get();
  }

  // recover the blocked receiver thread
  switch (socketType) {
    case SOCK_STREAM: {
      // shutdown() would unblock the receiver thread with recv returning 0.
      EXPECT_EQ(shutdown(recvfd, SHUT_RD), 0) << strerror(errno);
      EXPECT_EQ(recv.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);
      EXPECT_EQ(recv.get(), 0);
      break;
    }
    case SOCK_DGRAM: {
      // Send a valid packet to unblock the receiver.
      // This would ensure that the async-task deterministically exits before call to future`s
      // destructor. Calling close() on recvfd when the async task is blocked on recv(),
      // __does_not__ cause recv to return; this can result in undefined behavior, as the descriptor
      // can get reused. Instead of sending a valid packet to unblock the recv() task, we could call
      // shutdown(), but that returns ENOTCONN (unconnected) but still causing recv() to return.
      // shutdown() becomes unreliable for unconnected UDP sockets because, irrespective of the
      // effect of calling this call, it returns error.
      // TODO(NET-2558): dgram send should accept 0 length payload, once that is fixed, fix this
      // revert code
      char shut[] = "shutdown";
      EXPECT_EQ(
          sendto(sendfd, shut, sizeof(shut), 0, reinterpret_cast<struct sockaddr*>(addr), *addrlen),
          static_cast<ssize_t>(sizeof(shut)))
          << strerror(errno);
      EXPECT_EQ(recv.wait_for(std::chrono::milliseconds(kTimeout)), std::future_status::ready);
      EXPECT_EQ(recv.get(), static_cast<ssize_t>(sizeof(shut)));
      EXPECT_STREQ(shut, buf);
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
  EXPECT_EQ(
      asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen, SOCK_DGRAM),
      (ssize_t)strlen(msg));
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
  EXPECT_EQ(
      asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen, SOCK_DGRAM),
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
  EXPECT_EQ(
      asyncSocketRead(recvfd, sendfd, recvbuf, sizeof(recvbuf), 0, &addr, &addrlen, SOCK_DGRAM),
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

}  // namespace netstack
