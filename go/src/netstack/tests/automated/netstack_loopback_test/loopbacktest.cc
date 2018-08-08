// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tese tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <thread>

#include <lib/fdio/io.h>
#include <lib/fdio/private.h>
#include <lib/fdio/util.h>

#include <zircon/syscalls.h>

#include "gtest/gtest.h"

namespace netstack {
namespace {

const int32_t kTimeout = 10000;  // 10 seconds
const int32_t kRepeatEach = 1;   // How many times to repeat each test

// InterThread communication helper

const uint8_t kNotifySuccess = 1;
const uint8_t kNotifyFail = 2;

void NotifySuccess(int ntfyfd) {
  uint8_t c = kNotifySuccess;
  EXPECT_EQ(1, write(ntfyfd, &c, 1));
}

void NotifyFail(int ntfyfd) {
  uint8_t c = kNotifyFail;
  EXPECT_EQ(1, write(ntfyfd, &c, 1));
}

bool WaitSuccess(int ntfyfd, int timeout) {
  struct pollfd fds = {ntfyfd, POLLIN, 0};
  int nfds = poll(&fds, 1, timeout);
  EXPECT_GE(nfds, 0) << "poll failed: " << errno;
  if (nfds == 1) {
    uint8_t c = kNotifyFail;
    EXPECT_EQ(1, read(ntfyfd, &c, 1));
    return kNotifySuccess == c;
  } else {
    EXPECT_EQ(1, nfds);
    return false;
  }
}

// NetStreamTest

// NetStreamTest.BlockingAcceptWrite

void StreamConnectRead(struct sockaddr_in* addr, std::string* out, int ntfyfd) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_GE(connfd, 0);
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  EXPECT_EQ(0, ret) << "connect failed: " << errno;
  if (ret != 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0);
  NotifySuccess(ntfyfd);
}

void BlockingAcceptWrite() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

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

TEST(NetStreamTest, BlockingAcceptWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    BlockingAcceptWrite();
  }
}

// NetStreamTest.BlockAcceptWriteNoClose

// NoClose simulates an unexpected process exit by closing the socket handle
// associated with fd without sending a Close op to netstack.
void NoClose(int fd) {
  fdio_t* io;
  zx_status_t status = fdio_unbind_from_fd(fd, &io);
  EXPECT_GE(status, 0);
  zx_handle_t h;
  zx_signals_t sigs;
  __fdio_wait_begin(io, 0, &h, &sigs);
  EXPECT_NE(NULL, h);
  zx_handle_close(h);
  __fdio_release(io);
}

void BlockingAcceptWriteNoClose() {
  short port = 0;  // will be assigned by the first bind.

  for (int j = 0; j < 2; j++) {
    int acptfd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(acptfd, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    addr.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
    ASSERT_EQ(0, ret) << "bind failed: " << errno << " port: " << port;

    socklen_t addrlen = sizeof(addr);
    ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
    ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

    // remember the assigned port and use it for the next bind.
    port = addr.sin_port;

    int ntfyfd[2];
    ASSERT_EQ(0, pipe(ntfyfd));

    ret = listen(acptfd, 10);
    ASSERT_EQ(0, ret) << "listen failed: " << errno;

    std::string out;
    std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

    int connfd = accept(acptfd, nullptr, nullptr);
    ASSERT_GE(connfd, 0) << "accept failed: " << errno;

    const char* msg = "hello";
    ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));
    ASSERT_EQ(0, close(connfd));

    ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
    thrd.join();

    EXPECT_STREQ(msg, out.c_str());

    // Simulate unexpected process exit.
    NoClose(acptfd);

    EXPECT_EQ(0, close(ntfyfd[0]));
    EXPECT_EQ(0, close(ntfyfd[1]));

    // Wait while netstack tears down the port.
    // TODO: synchronize with netstack instead of nanosleep.
    if (j == 0) {
      zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
  }
}

TEST(NetStreamTest, BlockingAcceptWriteNoClose) {
  for (int i = 0; i < kRepeatEach; i++) {
    BlockingAcceptWriteNoClose();
  }
}

// NetStreamTest.BlockAcceptDupWrite

void BlockingAcceptDupWrite() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

  int dupfd = dup(connfd);
  ASSERT_GE(dupfd, 0) << "dup failed: " << errno;
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

TEST(NetStreamTest, BlockingAcceptDupWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    BlockingAcceptDupWrite();
  }
}

// NetStreamTest.NonBlockingAcceptWrite

void NonBlockingAcceptWrite() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

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

TEST(NetStreamTest, NonBlockingAcceptWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    NonBlockingAcceptWrite();
  }
}

// NetStreamTest.NonBlockingAcceptDupWrite

void NonBlockingAcceptDupWrite() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamConnectRead, &addr, &out, ntfyfd[1]);

  int status = fcntl(acptfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(acptfd, F_SETFL, status | O_NONBLOCK));

  struct pollfd pfd = {acptfd, POLLIN, 0};
  ASSERT_EQ(1, poll(&pfd, 1, kTimeout));

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

  int dupfd = dup(connfd);
  ASSERT_GE(dupfd, 0) << "dup failed: " << errno;
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

TEST(NetStreamTest, NonBlockingAcceptDupWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    NonBlockingAcceptDupWrite();
  }
}

// NetStreamTest.NonBlockingConnectWrite

void StreamAcceptRead(int acptfd, std::string* out, int ntfyfd) {
  int connfd = accept(acptfd, nullptr, nullptr);
  EXPECT_GE(connfd, 0) << "accept failed: " << errno;
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(0, close(connfd));
  NotifySuccess(ntfyfd);
}

void NonBlockingConnectWrite() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamAcceptRead, acptfd, &out, ntfyfd[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

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

TEST(NetStreamTest, NonBlockingConnectWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    NonBlockingConnectWrite();
  }
}

// NetStreamTest.NonBlockingConnectRead

void StreamAcceptWrite(int acptfd, const char* msg, int ntfyfd) {
  int connfd = accept(acptfd, nullptr, nullptr);
  EXPECT_GE(connfd, 0) << "accept failed: " << errno;
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  ASSERT_EQ((ssize_t)strlen(msg), write(connfd, msg, strlen(msg)));

  EXPECT_EQ(0, close(connfd));
  NotifySuccess(ntfyfd);
}

void NonBlockingConnectRead() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  const char* msg = "hello";
  std::thread thrd(StreamAcceptWrite, acptfd, msg, ntfyfd[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

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

TEST(NetStreamTest, NonBlockingConnectRead) {
  for (int i = 0; i < kRepeatEach; i++) {
    NonBlockingConnectRead();
  }
}

// NetStreamTest.NonBlockingConnectRefused

void NonBlockingConnectRefused() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  // No listen() on acptfd.

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(0, fcntl(connfd, F_SETFL, status | O_NONBLOCK));

  ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  EXPECT_EQ(-1, ret);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

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

TEST(NetStreamTest, NonBlockingConnectRefused) {
  for (int i = 0; i < kRepeatEach; i++) {
    NonBlockingConnectRefused();
  }
}

// NetStreamTest.GetTcpInfo

void GetTcpInfo() {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  tcp_info info;
  socklen_t info_len = sizeof(tcp_info);
  int rv = getsockopt(connfd, SOL_TCP, TCP_INFO, (void*)&info, &info_len);
  ASSERT_GE(rv, 0) << "getsockopt failed: " << errno;
  ASSERT_EQ(sizeof(tcp_info), info_len);
  ASSERT_EQ(0u, info.tcpi_rtt);
  ASSERT_EQ(0u, info.tcpi_rttvar);

  ASSERT_EQ(0, close(connfd));
}

TEST(NetStreamTest, GetTcpInfo) {
  for (int i = 0; i < kRepeatEach; i++) {
    GetTcpInfo();
  }
}

// NetStreamTest.Shutdown

void PollSignal(struct sockaddr_in* addr, short events, short* revents,
                int ntfyfd) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0);

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  EXPECT_EQ(0, ret) << "connect failed: " << errno;
  if (ret != 0) {
    NotifyFail(ntfyfd);
    return;
  }

  struct pollfd fds = {connfd, events, 0};

  int n = poll(&fds, 1, kTimeout);
  EXPECT_GT(n, 0) << "poll failed: " << errno;
  if (n <= 0) {
    NotifyFail(ntfyfd);
    return;
  }

  EXPECT_EQ(0, close(connfd));
  *revents = fds.revents;
  NotifySuccess(ntfyfd);
}

void Shutdown() {
  int acptfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(acptfd, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(acptfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(acptfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  ret = listen(acptfd, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  short events = POLLRDHUP;
  short revents;
  std::thread thrd(PollSignal, &addr, events, &revents, ntfyfd[1]);

  int connfd = accept(acptfd, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

  ret = shutdown(connfd, SHUT_WR);
  ASSERT_EQ(0, ret) << "shutdown failed: " << errno;

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_EQ(POLLRDHUP, revents);
  ASSERT_EQ(0, close(connfd));

  EXPECT_EQ(0, close(acptfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetStreamTest, Shutdown) {
  for (int i = 0; i < kRepeatEach; i++) {
    Shutdown();
  }
}

// NetDatagramTest

// NetDatagramTest.DatagramSendto

void DatagramRead(int recvfd, std::string* out, struct sockaddr_in* addr,
                  socklen_t* addrlen, int ntfyfd, int timeout) {
  struct pollfd fds = {recvfd, POLLIN, 0};
  int nfds = poll(&fds, 1, timeout);
  EXPECT_EQ(1, nfds) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[4096];
  int nbytes =
      recvfrom(recvfd, buf, sizeof(buf), 0, (struct sockaddr*)addr, addrlen);
  EXPECT_GT(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
  out->append(buf, nbytes);

  NotifySuccess(ntfyfd);
}

void DatagramSendto() {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  int ntfyfd[2];
  ASSERT_EQ(0, pipe(ntfyfd));

  std::string out;
  std::thread thrd(DatagramRead, recvfd, &out, &addr, &addrlen, ntfyfd[1],
                   kTimeout);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
  ASSERT_EQ((ssize_t)strlen(msg), sendto(sendfd, msg, strlen(msg), 0,
                                         (struct sockaddr*)&addr, addrlen))
      << "sendto failed: " << errno;
  ASSERT_EQ(0, close(sendfd));

  ASSERT_EQ(true, WaitSuccess(ntfyfd[0], kTimeout));
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());

  EXPECT_EQ(0, close(recvfd));
  EXPECT_EQ(0, close(ntfyfd[0]));
  EXPECT_EQ(0, close(ntfyfd[1]));
}

TEST(NetDatagramTest, DatagramSendto) {
  for (int i = 0; i < kRepeatEach; i++) {
    DatagramSendto();
  }
}

// NetDatagramTest.DatagramConnectWrite

void DatagramConnectWrite() {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

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

TEST(NetDatagramTest, DatagramConnectWrite) {
  for (int i = 0; i < kRepeatEach; i++) {
    DatagramConnectWrite();
  }
}

// NetDatagramTest.Datagram.PartialRecv

void DatagramPartialRecv() {
  int recvfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(recvfd, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = bind(recvfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(recvfd, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  const char kTestMsg[] = "hello";
  const int kTestMsgSize = sizeof(kTestMsg);

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
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

TEST(NetDatagramTest, DatagramPartialRecv) {
  for (int i = 0; i < kRepeatEach; i++) {
    DatagramPartialRecv();
  }
}

TEST(NetInvalidArgTest, Socket) {
  // Specify an unsupported protocol family and verify that an error returns
  // from the server. The service channel should not be closed because of
  // the error (errno should not be EIO).
  int s = socket(PF_NETLINK, SOCK_RAW, 0);
  ASSERT_EQ(-1, s);
  ASSERT_EQ(EOPNOTSUPP, errno);  // TODO: should be EPFNOSUPPORT

  // Check if we can still make a successful call (i.e. the service channel
  // is still open).
  s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ASSERT_GE(s, 0);
  close(s);
}

// TODO port reuse

}  // namespace
}  // namespace netstack
