// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tese tests ensure the magenta libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string>
#include <thread>

#include "gtest/gtest.h"

namespace netstack {
namespace {

class NetStreamTest {
 public:
  NetStreamTest() {}
};

void StreamRead(struct sockaddr_in* addr, std::string* out) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(connfd, 0);

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  ASSERT_EQ(0, ret) << "connect failed: " << errno;

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0);
}

TEST(NetStreamTest, LoopbackStream) {
  int server = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GT(server, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(server, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(server, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  ret = listen(server, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  std::string out;
  std::thread thrd(StreamRead, &addr, &out);

  int connfd = accept(server, nullptr, nullptr);
  ASSERT_GT(connfd, 0) << "accept failed: " << errno;

  const char* msg = "hello";
  ASSERT_EQ(write(connfd, msg, strlen(msg)), (ssize_t)strlen(msg));
  ASSERT_EQ(close(connfd), 0);
  ASSERT_EQ(close(server), 0);

  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

void PollSignal(struct sockaddr_in* addr, short events, short* revents) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GT(connfd, 0);

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  ASSERT_EQ(0, ret) << "connect failed: " << errno;

  struct pollfd fds = { connfd, events, 0 };

  int n = poll(&fds, 1, 1000); // timeout: 1000ms
  ASSERT_GT(n, 0) << "poll failed: " << errno;

  EXPECT_EQ(0, close(connfd));

  *revents = fds.revents;
}

TEST(NetStreamTest, Shutdown) {
  int server = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GT(server, 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  int ret = bind(server, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(0, ret) << "bind failed: " << errno;

  socklen_t addrlen = sizeof(addr);
  ret = getsockname(server, (struct sockaddr*)&addr, &addrlen);
  ASSERT_EQ(0, ret) << "getsockname failed: " << errno;

  ret = listen(server, 10);
  ASSERT_EQ(0, ret) << "listen failed: " << errno;

  short events = POLLRDHUP;
  short revents;
  std::thread thrd(PollSignal, &addr, events, &revents);

  int connfd = accept(server, nullptr, nullptr);
  ASSERT_GT(connfd, 0) << "accept failed: " << errno;

  ret = shutdown(connfd, SHUT_WR);
  ASSERT_EQ(0, ret) << "shutdown failed: " << errno;

  thrd.join();

  EXPECT_EQ(POLLRDHUP, revents);

  ASSERT_EQ(0, close(connfd));
  ASSERT_EQ(0, close(server));
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

    ASSERT_EQ(pipe(pipefd_), 0);
  }

  virtual void TearDown() {
    EXPECT_EQ(close(recvfd_), 0);
    close(pipefd_[0]);
    close(pipefd_[1]);
  }

  int recvfd_;
  struct sockaddr_in addr_;
  socklen_t addrlen_;
  int pipefd_[2];
};

const uint8_t kDatagramRead_Success = 1;
const uint8_t kDatagramRead_Fail = 2;

void NotifySuccess(int pipefd) {
  uint8_t c = kDatagramRead_Success;
  EXPECT_EQ(write(pipefd, &c, 1), 1);
}

void NotifyFail(int pipefd) {
  uint8_t c = kDatagramRead_Fail;
  EXPECT_EQ(write(pipefd, &c, 1), 1);
}

void DatagramRead(int recvfd, std::string* out,
                  struct sockaddr_in* addr, socklen_t *addrlen,
                  int pipefd, int timeout) {
  struct pollfd fds = { recvfd, POLLIN, 0 };
  int nfds = poll(&fds, 1, timeout);
  EXPECT_EQ(nfds, 1) << "poll returned: " << nfds << " errno: " << errno;
  if (nfds != 1) {
    NotifyFail(pipefd);
    return;
  }

  char buf[4096];
  int nbytes = recvfrom(recvfd, buf, sizeof(buf), 0, (struct sockaddr*)addr,
                        addrlen);
  EXPECT_GT(nbytes, 0) << "recvfrom failed: " << errno;
  if (nbytes < 0) {
    NotifyFail(pipefd);
    return;
  }
  out->append(buf, nbytes);

  NotifySuccess(pipefd);
}

TEST_F(NetDatagramTest, LoopbackDatagramSendto) {
  std::string out;
  std::thread thrd(DatagramRead, recvfd_,
                   &out, &addr_, &addrlen_, pipefd_[1], 5000);

  const char* msg = "hello";

  // TODO: Remove do-while if the bots are happy.
  int attempts = 1;
  int nfd = -1;
  do {
    int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sendfd, 0) << "socket failed: " << errno;
    ASSERT_EQ(sendto(sendfd, msg, strlen(msg), 0, (struct sockaddr*)&addr_,
                     addrlen_), (ssize_t)strlen(msg)) << "sendto failed: "
                                                      << errno;
    ASSERT_EQ(close(sendfd), 0);

    // Check if the reader has received the message.
    struct pollfd fds = { pipefd_[0], POLLIN, 0 };
    nfd = poll(&fds, 1, 1000);
    ASSERT_GE(nfd, 0) << "poll failed: " << errno;
    if (nfd == 1) {
      uint8_t c;
      ASSERT_EQ(read(pipefd_[0], &c, 1), 1);
      ASSERT_EQ(kDatagramRead_Success, c);
    }
  } while (nfd != 1 && --attempts > 0);

  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

TEST_F(NetDatagramTest, LoopbackDatagramConnectWrite) {
  std::string out;
  std::thread thrd(DatagramRead, recvfd_,
                   &out, &addr_, &addrlen_, pipefd_[1], 5000);

  const char* msg = "hello";

  // TODO: Remove do-while if the bots are happy.
  int attempts = 1;
  int nfd = -1;
  do {
    int sendfd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sendfd, 0);
    ASSERT_EQ(connect(sendfd, (struct sockaddr*)&addr_, addrlen_), 0);
    ASSERT_EQ(write(sendfd, msg, strlen(msg)), (ssize_t)strlen(msg));
    ASSERT_EQ(close(sendfd), 0);

    // Check if the reader has received the message.
    struct pollfd fds = { pipefd_[0], POLLIN, 0 };
    nfd = poll(&fds, 1, 1000);
    ASSERT_GE(nfd, 0) << "poll failed: " << errno;
    if (nfd == 1) {
      uint8_t c;
      ASSERT_EQ(read(pipefd_[0], &c, 1), 1);
      ASSERT_EQ(kDatagramRead_Success, c);
    }
  } while (nfd != 1 && --attempts > 0);

  thrd.join();

  EXPECT_STREQ(msg, out.c_str());
}

// TODO port reuse

}  // namespace
}  // namespace netstack
