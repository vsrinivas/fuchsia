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

#include <thread>

#include "gtest/gtest.h"
#include "util.h"

namespace netstack {

TEST(LocalhostTest, IP_ADD_MEMBERSHIP_INADDR_ANY) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s, 0) << strerror(errno);

  ip_mreqn param = {};
  param.imr_ifindex = 1;
  param.imr_multiaddr.s_addr = inet_addr("224.0.2.1");
  param.imr_address.s_addr = htonl(INADDR_ANY);
  ASSERT_EQ(setsockopt(s, SOL_IP, IP_ADD_MEMBERSHIP, &param, sizeof(param)), 0)
      << strerror(errno);
}

TEST(LocalhostTest, IP_MULTICAST_IF_ifindex) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s, 0) << strerror(errno);

  ip_mreqn param_in = {};
  param_in.imr_ifindex = 1;
  ASSERT_EQ(setsockopt(s, SOL_IP, IP_MULTICAST_IF, &param_in, sizeof(param_in)),
            0)
      << strerror(errno);

  in_addr param_out = {};
  socklen_t len = sizeof(param_out);
  ASSERT_EQ(getsockopt(s, SOL_IP, IP_MULTICAST_IF, &param_in, &len), 0)
      << strerror(errno);
  ASSERT_EQ(len, sizeof(param_out));

  ASSERT_EQ(param_out.s_addr, INADDR_ANY);
}

class ReuseTest
    : public ::testing::TestWithParam<
          ::testing::tuple<int /* type */, in_addr_t /* address */>> {};

TEST_P(ReuseTest, AllowsAddressReuse) {
  const int on = true;

  int s1 = socket(AF_INET, ::testing::get<0>(GetParam()), 0);
  ASSERT_GE(s1, 0) << strerror(errno);

  ASSERT_EQ(setsockopt(s1, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0)
      << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ::testing::get<1>(GetParam());
  ASSERT_EQ(
      bind(s1, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(
      getsockname(s1, reinterpret_cast<struct sockaddr*>(&addr), &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  int s2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s2, 0) << strerror(errno);

  ASSERT_EQ(setsockopt(s2, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)), 0)
      << strerror(errno);

  ASSERT_EQ(
      bind(s2, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      0)
      << strerror(errno);
}

INSTANTIATE_TEST_SUITE_P(
    LocalhostTest, ReuseTest,
    ::testing::Combine(::testing::Values(SOCK_DGRAM, SOCK_STREAM),
                       ::testing::Values(htonl(INADDR_LOOPBACK),
                                         inet_addr("224.0.2.1"))));

TEST(LocalhostTest, Accept) {
  int serverfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(serverfd, 0) << strerror(errno);

  struct sockaddr_in6 serveraddr = {};
  serveraddr.sin6_family = AF_INET6;
  serveraddr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  socklen_t serveraddrlen = sizeof(serveraddr);
  ASSERT_EQ(bind(serverfd, (sockaddr*)&serveraddr, serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(getsockname(serverfd, (sockaddr*)&serveraddr, &serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(serveraddrlen, sizeof(serveraddr));
  ASSERT_EQ(listen(serverfd, 1), 0) << strerror(errno);

  int clientfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(clientfd, 0) << strerror(errno);
  ASSERT_EQ(connect(clientfd, (sockaddr*)&serveraddr, serveraddrlen), 0)
      << strerror(errno);

  struct sockaddr_in connaddr;
  socklen_t connaddrlen = sizeof(connaddr);
  int connfd = accept(serverfd, (sockaddr*)&connaddr, &connaddrlen);
  ASSERT_GE(connfd, 0) << strerror(errno);
  ASSERT_GT(connaddrlen, sizeof(connaddr));
}

TEST(LocalhostTest, ConnectAFMismatchINET) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s, 0) << strerror(errno);

  struct sockaddr_in6 addr = {};
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  addr.sin6_port = htons(1337);
  EXPECT_EQ(
      connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      -1);
  EXPECT_EQ(errno, EAFNOSUPPORT) << strerror(errno);
  EXPECT_EQ(close(s), 0) << strerror(errno);
}

TEST(LocalhostTest, ConnectAFMismatchINET6) {
  int s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  ASSERT_GE(s, 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(1337);
  EXPECT_EQ(
      connect(s, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)),
      0)
      << strerror(errno);
  EXPECT_EQ(close(s), 0) << strerror(errno);
}

TEST(NetStreamTest, ConnectTwice) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(server_fd, 0) << strerror(errno);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(server_fd, reinterpret_cast<const struct sockaddr*>(&addr),
                 sizeof(addr)),
            0)
      << strerror(errno);
  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&addr),
                        &addrlen),
            0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));
  ASSERT_EQ(listen(server_fd, 1), 0) << strerror(errno);

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client_fd, 0);

  addr.sin_port++;

  ASSERT_EQ(connect(client_fd, reinterpret_cast<const struct sockaddr*>(&addr),
                    sizeof(addr)),
            -1);
  ASSERT_EQ(errno, ECONNREFUSED) << strerror(errno);

  addr.sin_port--;

  // TODO(tamird): decide if we want to match Linux's behaviour.
  ASSERT_EQ(connect(client_fd, reinterpret_cast<const struct sockaddr*>(&addr),
                    sizeof(addr)),
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
  ASSERT_EQ(getaddrinfo("localhost", NULL, &hints, &result), 0);

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
        EXPECT_EQ(sin->sin_addr.s_addr,
                  *reinterpret_cast<uint32_t*>(expected_addr));

        break;
      }
      case AF_INET6: {
        EXPECT_EQ(ai->ai_addrlen, (socklen_t)28);

        const char expected_addr[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x01};

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
  int sockfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(sockfd, 0) << strerror(errno);

  struct sockaddr sa;
  socklen_t len = sizeof(sa);
  ASSERT_EQ(getsockname(sockfd, &sa, &len), 0) << strerror(errno);
  ASSERT_GT(len, sizeof(sa));
  ASSERT_EQ(sa.sa_family, AF_INET6);
}

TEST(NetStreamTest, BlockingAcceptWrite) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(connfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, ReceiveTimeout) {
  int acptfd;
  ASSERT_GE(acptfd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(0),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  ASSERT_EQ(bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ASSERT_EQ(getsockname(acptfd, (struct sockaddr*)&addr, &addrlen), 0)
      << strerror(errno);
  ASSERT_EQ(addrlen, sizeof(addr));

  ASSERT_EQ(listen(acptfd, 1), 0) << strerror(errno);

  int client_fd;
  ASSERT_GE(client_fd = socket(AF_INET, SOCK_STREAM, 0), 0);
  ASSERT_EQ(connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr)), 0)
      << strerror(errno);

  const int64_t rcv_timeout_us = 1150000;

  // Set the timeout for reading.
  struct timeval tv = {
      .tv_sec = rcv_timeout_us / 1000000,
      .tv_usec = rcv_timeout_us % 1000000,
  };
  EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);

  int server_fd;
  ASSERT_GE(server_fd = accept(acptfd, nullptr, nullptr), 0) << strerror(errno);

  std::thread timeout_thread([server_fd]() {
    // This is long enough for the test to succeed.  If this sleep ends
    // before the read ends, the total_seconds below will be too long and
    // report a test failure.
    int64_t read_timeout_ns = rcv_timeout_us * 1000 * 3;
    struct timespec read_timeout = {
        .tv_sec = read_timeout_ns / 1000000000,
        .tv_nsec = read_timeout_ns % 1000000000,
    };
    ASSERT_EQ(nanosleep(&read_timeout, NULL), 0) << strerror(errno);
    EXPECT_EQ(close(server_fd), 0) << strerror(errno);
  });

  // Try to read.  There will be no data so this should eventually timeout.  If
  // the timeout_thread closes the connection, that will also end the wait.
  timespec current_time;
  ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &current_time), 0)
      << strerror(errno);
  int64_t start_time_ns =
      current_time.tv_sec * 1000000000 + current_time.tv_nsec;

  char buf[16];  // This buffer will never be written into.
  EXPECT_EQ(read(client_fd, buf, sizeof(buf)), -1);
  ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);

  ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &current_time), 0)
      << strerror(errno);
  int64_t end_time_ns = current_time.tv_sec * 1000000000 + current_time.tv_nsec;

  int64_t total_ns = end_time_ns - start_time_ns;
  // Check that the actual time waited was within 100ms of the expectation.
  EXPECT_LT(total_ns, 1000 * (rcv_timeout_us + 100000))
      << "SO_RCVTIMEO waited too long";
  EXPECT_GT(total_ns, 1000 * (rcv_timeout_us - 100000))
      << "SO_RCVTIMEO didn't wait long enough";

  // Remove the timeout
  tv = {
      .tv_sec = 0,
      .tv_usec = 0,
  };
  EXPECT_EQ(setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), 0)
      << strerror(errno);

  // This read should never timeout.  timeout_thread should close the connection
  // eventually.
  EXPECT_EQ(read(client_fd, buf, sizeof(buf)), 0) << strerror(errno);

  timeout_thread.join();  // Clean up the thread.
}

TEST(NetStreamTest, ReceiveTimeoutSockopts) {
  int socket_fd;
  ASSERT_GE(socket_fd = socket(AF_INET, SOCK_STREAM, 0), 0) << strerror(errno);

  // Set the timeout for reading.
  const struct timeval expected_tv = {
      .tv_sec = 39,
      .tv_usec = 500000,
  };
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &expected_tv,
                       sizeof(expected_tv)),
            0)
      << strerror(errno);

  // Reading it back should work.
  struct timeval actual_tv;
  socklen_t optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &actual_tv, &optlen),
            0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, expected_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, expected_tv.tv_usec);

  // Reading it back with too much space should work and set optlen.
  char actual_tv2_buffer[sizeof(struct timeval) * 2];
  memset(&actual_tv2_buffer, 44, sizeof(actual_tv2_buffer));
  optlen = sizeof(actual_tv2_buffer);
  struct timeval* actual_tv2 = (struct timeval*)&actual_tv2_buffer;
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, actual_tv2, &optlen),
            0)
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
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &actual_tv, &optlen),
#if defined(__linux__)
            0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv) - 7);
  struct timeval linux_expected_tv = expected_tv;
  memset(((char*)&linux_expected_tv) + optlen, 0,
         sizeof(linux_expected_tv) - optlen);
  EXPECT_EQ(memcmp(&actual_tv, &linux_expected_tv, sizeof(actual_tv)), 0);
#else
            -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);
#endif

  // Setting it without enough space should fail gracefully.
  optlen = sizeof(expected_tv) - 1;  // Not big enough.
  EXPECT_EQ(
      setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &expected_tv, optlen), -1);
  EXPECT_EQ(errno, EINVAL) << strerror(errno);

  // Setting it with too much space should work okay.
  const struct timeval expected_tv2 = {
      .tv_sec = 42,
      .tv_usec = 0,
  };
  optlen = sizeof(expected_tv2) + 1;  // Too big.
  EXPECT_EQ(
      setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &expected_tv2, optlen), 0)
      << strerror(errno);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &actual_tv, &optlen),
            0)
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
  EXPECT_EQ(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &zero_tv, optlen), 0)
      << strerror(errno);

  // Reading back the disabled timeout should work.
  memset(&actual_tv, 55, sizeof(actual_tv));
  optlen = sizeof(actual_tv);
  EXPECT_EQ(getsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &actual_tv, &optlen),
            0)
      << strerror(errno);
  EXPECT_EQ(optlen, sizeof(actual_tv));
  EXPECT_EQ(actual_tv.tv_sec, zero_tv.tv_sec);
  EXPECT_EQ(actual_tv.tv_usec, zero_tv.tv_usec);
}

const int32_t kConnections = 100;

TEST(NetStreamTest, BlockingAcceptWriteMultiple) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, kConnections);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::thread thrd[kConnections];
  std::string out[kConnections];
  const char* msg = "hello";

  for (int i = 0; i < kConnections; i++) {
    thrd[i] = std::thread(StreamConnectRead, &addr, &out[i], ntfyfd[1]);
  }

  for (int i = 0; i < kConnections; i++) {
    struct pollfd pfd = {acptfd, POLLIN, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int connfd = accept(acptfd, nullptr, nullptr);
    ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

    ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
    ASSERT_EQ(0, close(connfd));

    ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  }

  for (int i = 0; i < kConnections; i++) {
    thrd[i].join();
    EXPECT_STREQ(msg, out[i].c_str());
  }

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, BlockingAcceptDupWrite) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

  int dupfd = dup(connfd);
  ASSERT_GE(dupfd, 0) << "dup failed: " << strerror(errno);
  ASSERT_EQ(0, close(connfd));

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(dupfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(dupfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, NonBlockingAcceptWrite) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(connfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, NonBlockingAcceptDupWrite) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

  int dupfd = dup(connfd);
  ASSERT_GE(dupfd, 0) << "dup failed: " << strerror(errno);
  ASSERT_EQ(0, close(connfd));

  const char* msg = "hello";
  ASSERT_EQ((ssize_t)strlen(msg), write(dupfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(dupfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, NonBlockingConnectWrite) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  std::string out;
  std::thread thrd(StreamAcceptRead, acptfd, &out, ntfyfd[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << strerror(errno);

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

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, NonBlockingConnectRead) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  const char* msg = "hello";
  std::thread thrd(StreamAcceptWrite, acptfd, msg, ntfyfd[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << strerror(errno);

    // Note: the success of connection can be detected with POLLOUT, but
    // we use POLLIN here to wait until some data is written by the peer.
    struct pollfd pfd = {connfd, POLLIN, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(0, getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen));
    ASSERT_EQ(0, val);
  }

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

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, NonBlockingConnectRefused) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  // No listen() on acptfd.

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << strerror(errno);

    struct pollfd pfd = {connfd, POLLOUT, 0};
    ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(0, getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen));
    ASSERT_EQ(ECONNREFUSED, val);
  }

  ASSERT_EQ(0, close(connfd));

  EXPECT_EQ(0, close(acptfd));
}

TEST(NetStreamTest, GetTcpInfo) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  tcp_info info;
  socklen_t info_len = sizeof(tcp_info);
  int rv = getsockopt(connfd, SOL_TCP, TCP_INFO, (void*)&info, &info_len);
  ASSERT_GE(rv, 0) << "getsockopt failed: " << strerror(errno);
  ASSERT_EQ(sizeof(tcp_info), info_len);

  ASSERT_EQ(0, close(connfd));
}

TEST(NetStreamTest, Shutdown) {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);

  short events = POLLRDHUP;
  short revents;
  std::thread thrd(PollSignal, &addr, events, &revents, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << strerror(errno);

  ret = shutdown(connfd, SHUT_WR);
  ASSERT_EQ(0, ret) << "shutdown failed: " << strerror(errno);

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(POLLRDHUP, revents);
  ASSERT_EQ(0, close(connfd));

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetDatagramTest, DatagramSendto) {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::string out;
  std::thread thrd(DatagramRead, recvfd, &out, &addr, &addrlen, ntfyfd[1],
                   kTimeout);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << strerror(errno);
  ASSERT_EQ((ssize_t)strlen(msg), sendto(sendfd, msg, strlen(msg), 0,
                                         (struct sockaddr*)&addr, addrlen))
      << "sendto failed: " << strerror(errno);
  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetDatagramTest, DatagramConnectWrite) {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::string out;
  std::thread thrd(DatagramRead, recvfd, &out, &addr, &addrlen, ntfyfd[1],
                   kTimeout);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0);
  ASSERT_EQ(0, connect(sendfd, (struct sockaddr*)&addr, addrlen));
  ASSERT_EQ((ssize_t)strlen(msg), write(sendfd, msg, strlen(msg)));
  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetDatagramTest, DatagramPartialRecv) {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);

  const char kTestMsg[] = "hello";
  const int kTestMsgSize = sizeof(kTestMsg);

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << strerror(errno);
  ASSERT_EQ(kTestMsgSize, sendto(sendfd, kTestMsg, kTestMsgSize, 0,
                                 reinterpret_cast<sockaddr*>(&addr), addrlen));

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
  ASSERT_EQ(std::string(kTestMsg, kPartialReadSize),
            std::string(recv_buf, kPartialReadSize));
  EXPECT_EQ(MSG_TRUNC, msg.msg_flags);

  // Send the second packet.
  ASSERT_EQ(kTestMsgSize, sendto(sendfd, kTestMsg, kTestMsgSize, 0,
                                 reinterpret_cast<sockaddr*>(&addr), addrlen));

  // Read the whole packet now.
  recv_buf[0] = 0;
  iov.iov_len = sizeof(recv_buf);
  recv_result = recvmsg(recvfd, &msg, 0);
  ASSERT_EQ(kTestMsgSize, recv_result);
  ASSERT_EQ(std::string(kTestMsg, kTestMsgSize),
            std::string(recv_buf, kTestMsgSize));
  EXPECT_EQ(0, msg.msg_flags);

  ASSERT_EQ(0, close(sendfd));

  EXPECT_EQ(0, close(recvfd));
}

// TODO port reuse

// DatagramSendtoRecvfrom tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

TEST(NetDatagramTest, DatagramSendtoRecvfrom) {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr),
                 sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret =
      getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("addr.sin_addr: %s\n",
         inet_ntop(AF_INET, &addr.sin_addr, addrstr, sizeof(addrstr)));
  printf("addr.sin_port: %d\n", ntohs(addr.sin_port));
  printf("addrlen: %d\n", addrlen);
#endif

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWrite, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << strerror(errno);
  ASSERT_EQ((ssize_t)strlen(msg),
            sendto(sendfd, msg, strlen(msg), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), addrlen))
      << "sendto failed: " << strerror(errno);

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds
                     << " errno: " << strerror(errno);

  char buf[32];
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ((ssize_t)strlen(msg),
            recvfrom(sendfd, buf, sizeof(buf), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), &peerlen))
      << "recvfrom failed: " << strerror(errno);
#if DEBUG
  printf("peer.sin_addr[2]: %s\n",
         inet_ntop(AF_INET, &peer.sin_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin_port[2]: %d\n", ntohs(peer.sin_port));
  printf("peerlen[2]: %d\n", peerlen);
#endif

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

// DatagramSendtoRecvfromV6 tests if UDP send automatically binds an ephemeral
// port where the receiver can responds to.

TEST(NetDatagramTest, DatagramSendtoRecvfromV6) {
  int recvfd = socket(AF_INET6, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(0);
  addr.sin6_addr = in6addr_loopback;

  int ret = bind(recvfd, reinterpret_cast<const struct sockaddr*>(&addr),
                 sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

  socklen_t addrlen = sizeof(addr);
  ret =
      getsockname(recvfd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);
#if DEBUG
  char addrstr[INET_ADDRSTRLEN];
  printf("addr.sin6_addr: %s\n",
         inet_ntop(AF_INET6, &addr.sin6_addr, addrstr, sizeof(addrstr)));
  printf("addr.sin6_port: %d\n", ntohs(addr.sin6_port));
  printf("addrlen: %d\n", addrlen);
#endif

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::thread thrd(DatagramReadWriteV6, recvfd, ntfyfd[1]);

  const char* msg = "hello";

  int sendfd = socket(AF_INET6, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << strerror(errno);
  ASSERT_EQ((ssize_t)strlen(msg),
            sendto(sendfd, msg, strlen(msg), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), addrlen))
      << "sendto failed: " << strerror(errno);

  struct pollfd fds = {sendfd, POLLIN, 0};
  int nfds = poll(&fds, 1, kTimeout);
  ASSERT_EQ(1, nfds) << "poll returned: " << nfds
                     << " errno: " << strerror(errno);

  char buf[32];
  struct sockaddr_in6 peer;
  socklen_t peerlen = sizeof(peer);
  ASSERT_EQ((ssize_t)strlen(msg),
            recvfrom(sendfd, buf, sizeof(buf), 0,
                     reinterpret_cast<struct sockaddr*>(&peer), &peerlen))
      << "recvfrom failed: " << strerror(errno);
#if DEBUG
  printf("peer.sin6_addr[2]: %s\n",
         inet_ntop(AF_INET6, &peer.sin6_addr, addrstr, sizeof(addrstr)));
  printf("peer.sin6_port[2]: %d\n", ntohs(peer.sin6_port));
  printf("peerlen[2]: %d\n", peerlen);
#endif

  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

// Note: we choose 100 because the max number of fds per process is limited to
// 256.
const int32_t kListeningSockets = 100;

TEST(NetStreamTest, MultipleListeningSockets) {
  int listenfd[kListeningSockets];
  int connfd[kListeningSockets];

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addrlen = sizeof(addr);

  for (int i = 0; i < kListeningSockets; i++) {
    listenfd[i] = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listenfd[i], 0) << "socket failed:" << strerror(errno);

    int ret = bind(listenfd[i], reinterpret_cast<const struct sockaddr*>(&addr),
                   sizeof(addr));
    ASSERT_EQ(0, ret) << "bind failed: " << strerror(errno);

    ret = listen(listenfd[i], 10);
    ASSERT_EQ(0, ret) << "listen failed: " << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    int ret = getsockname(listenfd[i],
                          reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    ASSERT_EQ(0, ret) << "getsockname failed: " << strerror(errno);
#if DEBUG
    char addrstr[INET_ADDRSTRLEN];
    printf("[%d] %s:%d\n", i,
           inet_ntop(AF_INET, &addr.sin_addr, addrstr, sizeof(addrstr)),
           ntohs(addr.sin_port));
#endif

    connfd[i] = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(connfd[i], 0);

    ret = connect(connfd[i], reinterpret_cast<const struct sockaddr*>(&addr),
                  sizeof(addr));
    ASSERT_EQ(0, ret) << "connect failed: " << strerror(errno);
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(0, close(connfd[i]));
    ASSERT_EQ(0, close(listenfd[i]));
  }
}

}  // namespace netstack
