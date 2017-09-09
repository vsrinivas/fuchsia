// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tese tests ensure the magenta libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string>
#include <thread>

#include "gtest/gtest.h"

namespace netstack {
namespace {

const uint8_t kNotifySuccess = 1;
const uint8_t kNotifyFail = 2;

void NotifySuccess(int ntfyfd) {
  uint8_t c = kNotifySuccess;
  EXPECT_EQ(write(ntfyfd, &c, 1), 1);
}

void NotifyFail(int ntfyfd) {
  uint8_t c = kNotifyFail;
  EXPECT_EQ(write(ntfyfd, &c, 1), 1);
}

bool WaitSuccess(int ntfyfd, int timeout) {
  struct pollfd fds = { ntfyfd, POLLIN, 0 };
  int nfds = poll(&fds, 1, timeout);
  EXPECT_GE(nfds, 0) << "poll failed: " << errno;
  if (nfds == 1) {
    uint8_t c = kNotifyFail;
    EXPECT_EQ(read(ntfyfd, &c, 1), 1);
    return kNotifySuccess == c;
  } else {
    EXPECT_EQ(nfds, 1);
    return false;
  }
}

class NetStreamTest : public ::testing::Test {
 protected:
  NetStreamTest() {}

  virtual void SetUp() {
    acptfd_ = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(acptfd_, 0);

    addr_.sin_family = AF_INET;
    addr_.sin_port = 0;
    addr_.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(acptfd_, (const struct sockaddr*)&addr_, sizeof(addr_));
    ASSERT_EQ(0, ret) << "bind failed: " << errno;

    addrlen_ = sizeof(addr_);
    ret = getsockname(acptfd_, (struct sockaddr*)&addr_, &addrlen_);
    ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

    ASSERT_EQ(pipe(ntfyfd_), 0);
  }

  virtual void TearDown() {
    EXPECT_EQ(close(acptfd_), 0);
    EXPECT_EQ(close(ntfyfd_[0]), 0);
    EXPECT_EQ(close(ntfyfd_[1]), 0);
  }

  int acptfd_;
  struct sockaddr_in addr_;
  socklen_t addrlen_;
  int ntfyfd_[2];
};

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

TEST_F(NetStreamTest, LoopbackStream) {
  int ret = listen(acptfd_, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamConnectRead, &addr_, &out, ntfyfd_[1]);

  int connfd = accept(acptfd_, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

  const char* msg = "hello";
  ASSERT_EQ(write(connfd, msg, strlen(msg)), (ssize_t)strlen(msg));
  ASSERT_EQ(close(connfd), 0);

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

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

  EXPECT_EQ(close(connfd), 0);
  NotifySuccess(ntfyfd);
}

TEST_F(NetStreamTest, NonBlockingConnectWrite) {
  int ret = listen(acptfd_, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamAcceptRead, acptfd_, &out, ntfyfd_[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(fcntl(connfd, F_SETFL, status | O_NONBLOCK), 0);

  ret = connect(connfd, (const struct sockaddr*)&addr_, sizeof(addr_));
  EXPECT_EQ(ret, -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

    struct pollfd pfd = {connfd, POLLOUT, 0};
    ASSERT_EQ(poll(&pfd, 1, 1000), 1);

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen), 0);
    ASSERT_EQ(val, 0);
  }

  const char* msg = "hello";
  ASSERT_EQ(write(connfd, msg, strlen(msg)), (ssize_t)strlen(msg));
  ASSERT_EQ(close(connfd), 0);

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

void StreamAcceptWrite(int acptfd, const char* msg, int ntfyfd) {
  int connfd = accept(acptfd, nullptr, nullptr);
  EXPECT_GE(connfd, 0) << "accept failed: " << errno;
  if (connfd < 0) {
    NotifyFail(ntfyfd);
    return;
  }

  ASSERT_EQ(write(connfd, msg, strlen(msg)), (ssize_t)strlen(msg));

  EXPECT_EQ(close(connfd), 0);
  NotifySuccess(ntfyfd);
}

TEST_F(NetStreamTest, NonBlockingConnectRead) {
  int ret = listen(acptfd_, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  const char* msg = "hello";
  std::thread thrd(StreamAcceptWrite, acptfd_, msg, ntfyfd_[1]);

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(fcntl(connfd, F_SETFL, status | O_NONBLOCK), 0);

  ret = connect(connfd, (const struct sockaddr*)&addr_, sizeof(addr_));
  EXPECT_EQ(ret, -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

    // Note: the success of connection can be detected with POLLOUT, but
    // we use POLLIN here to wait until some data is written by the peer.
    struct pollfd pfd = {connfd, POLLIN, 0};
    ASSERT_EQ(poll(&pfd, 1, 1000), 1);

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen), 0);
    ASSERT_EQ(val, 0);
  }

  std::string out;
  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out.append(buf, n);
  }
  ASSERT_EQ(close(connfd), 0);

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

// TODO: Enable this test when it works.
TEST_F(NetStreamTest, DISABLED_NonBlockingConnectRefused) {
  // No listen() on acptfd_.

  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << errno;

  int status = fcntl(connfd, F_GETFL, 0);
  ASSERT_EQ(fcntl(connfd, F_SETFL, status | O_NONBLOCK), 0);

  int ret = connect(connfd, (const struct sockaddr*)&addr_, sizeof(addr_));
  EXPECT_EQ(ret, -1);
  if (ret == -1) {
    ASSERT_EQ(EINPROGRESS, errno) << "connect failed: " << errno;

    struct pollfd pfd = {connfd, POLLOUT, 0};
    ASSERT_EQ(poll(&pfd, 1, 1000), 1);

    int val;
    socklen_t vallen = sizeof(val);
    ASSERT_EQ(getsockopt(connfd, SOL_SOCKET, SO_ERROR, &val, &vallen), 0);
    ASSERT_EQ(val, ECONNREFUSED);
  }

  ASSERT_EQ(close(connfd), 0);
}

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

  struct pollfd fds = { connfd, events, 0 };

  int n = poll(&fds, 1, 1000); // timeout: 1000ms
  EXPECT_GT(n, 0) << "poll failed: " << errno;
  if (n <= 0) {
    NotifyFail(ntfyfd);
    return;
  }

  EXPECT_EQ(0, close(connfd));
  *revents = fds.revents;
  NotifySuccess(ntfyfd);
}

TEST_F(NetStreamTest, Shutdown) {
  int ret = listen(acptfd_, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  short events = POLLRDHUP;
  short revents;
  std::thread thrd(PollSignal, &addr_, events, &revents, ntfyfd_[1]);

  int connfd = accept(acptfd_, nullptr, nullptr);
  ASSERT_GE(connfd, 0) << "accept failed: " << errno;

  ret = shutdown(connfd, SHUT_WR);
  ASSERT_EQ(0, ret) << "shutdown failed: " << errno;

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_EQ(POLLRDHUP, revents);
  ASSERT_EQ(0, close(connfd));
}

class NetDatagramTest : public ::testing::Test {
 protected:
  NetDatagramTest() {}

  virtual void SetUp() {
    recvfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(recvfd_, 0);

    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = 0;
    addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int ret = bind(recvfd_, (const struct sockaddr*)&addr_, sizeof(addr_));
    ASSERT_EQ(0, ret) << "bind failed: " << errno;

    addrlen_ = sizeof(addr_);
    ret = getsockname(recvfd_, (struct sockaddr*)&addr_, &addrlen_);
    ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

    ASSERT_EQ(pipe(ntfyfd_), 0);
  }

  virtual void TearDown() {
    EXPECT_EQ(close(recvfd_), 0);
    EXPECT_EQ(close(ntfyfd_[0]), 0);
    EXPECT_EQ(close(ntfyfd_[1]), 0);
  }

  int recvfd_;
  struct sockaddr_in addr_;
  socklen_t addrlen_;
  int ntfyfd_[2];
};

void DatagramRead(int recvfd, std::string* out,
                  struct sockaddr_in* addr, socklen_t *addrlen,
                  int ntfyfd, int timeout) {
  struct pollfd fds = { recvfd, POLLIN, 0 };
  int nfds = poll(&fds, 1, timeout);
  EXPECT_EQ(nfds, 1) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(ntfyfd);
    return;
  }

  char buf[4096];
  int nbytes = recvfrom(recvfd, buf, sizeof(buf), 0, (struct sockaddr*)addr,
                        addrlen);
  EXPECT_GT(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(ntfyfd);
    return;
  }
  out->append(buf, nbytes);

  NotifySuccess(ntfyfd);
}

TEST_F(NetDatagramTest, LoopbackDatagramSendto) {
  std::string out;
  std::thread thrd(DatagramRead, recvfd_,
                   &out, &addr_, &addrlen_, ntfyfd_[1], 1000);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
  ASSERT_EQ(sendto(sendfd, msg, strlen(msg), 0, (struct sockaddr*)&addr_,
                   addrlen_), (ssize_t)strlen(msg)) << "sendto failed: "
                                                      << errno;
  ASSERT_EQ(close(sendfd), 0);

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

TEST_F(NetDatagramTest, LoopbackDatagramConnectWrite) {
  std::string out;
  std::thread thrd(DatagramRead, recvfd_,
                   &out, &addr_, &addrlen_, ntfyfd_[1], 1000);

  const char* msg = "hello";

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0);
  ASSERT_EQ(connect(sendfd, (struct sockaddr*)&addr_, addrlen_), 0);
  ASSERT_EQ(write(sendfd, msg, strlen(msg)), (ssize_t)strlen(msg));
  ASSERT_EQ(close(sendfd), 0);

  ASSERT_EQ(WaitSuccess(ntfyfd_[0], 1000), true);
  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

TEST_F(NetDatagramTest, PartialRecv) {
  const char kTestMsg[] = "hello";
  const int kTestMsgSize = sizeof(kTestMsg);

  int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
  ASSERT_EQ(sendto(sendfd, kTestMsg, kTestMsgSize, 0,
                   reinterpret_cast<sockaddr*>(&addr_), addrlen_),
            kTestMsgSize);

  char recv_buf[kTestMsgSize];

  // Read only first 2 bytes of the message. recv() is expected to discard the
  // rest.
  const int kPartialReadSize = 2;
  int recv_result = recv(recvfd_, &recv_buf, kPartialReadSize, 0);
  ASSERT_EQ(recv_result, kPartialReadSize);
  ASSERT_EQ(std::string(kTestMsg, kPartialReadSize),
            std::string(recv_buf, kPartialReadSize));

  // Send the second packet.
  ASSERT_EQ(sendto(sendfd, kTestMsg, kTestMsgSize, 0,
                   reinterpret_cast<sockaddr*>(&addr_), addrlen_),
            kTestMsgSize);

  // Read the whole packet now.
  recv_result = recv(recvfd_, &recv_buf, kTestMsgSize, 0);
  ASSERT_EQ(recv_result, kTestMsgSize);
  ASSERT_EQ(std::string(kTestMsg, kTestMsgSize),
            std::string(recv_buf, kTestMsgSize));

  ASSERT_EQ(close(sendfd), 0);
}

// TODO port reuse

}  // namespace
}  // namespace netstack
