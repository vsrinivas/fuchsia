// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <thread>

#include "gtest/gtest.h"

#define DEBUG 0
#if DEBUG
#include <arpa/inet.h>
#endif

namespace netstack {

// Note: we choose 100 because the max number of fds per process is limited to 256.
const int32_t kListeningSockets = 100;

TEST(NetStreamTest, MultipleListeningSockets) {
  int listenfd[kListeningSockets];
  int connfd[kListeningSockets];

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addrlen = sizeof(addr);

  for (int i = 0; i < kListeningSockets; i++) {
    listenfd[i] = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listenfd[i], 0) << "socket failed:" << errno;

    int ret = bind(listenfd[i], reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(0, ret) << "bind failed: " << errno;

    ret = listen(listenfd[i], 10);
    ASSERT_EQ(0, ret) << "listen failed: " << errno;
  }

  for (int i = 0; i < kListeningSockets; i++) {
    int ret = getsockname(listenfd[i], reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    ASSERT_EQ(0, ret) << "getsockname failed: " << errno;
#if DEBUG
    char addrstr[INET_ADDRSTRLEN];
    printf("[%d] %s:%d\n", i,
           inet_ntop(AF_INET, &addr.sin_addr, addrstr, sizeof(addrstr)),
           ntohs(addr.sin_port));
#endif

    connfd[i] = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(connfd[i], 0);

    ret = connect(connfd[i], reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(0, ret) << "connect failed: " << errno;
  }

  for (int i = 0; i < kListeningSockets; i++) {
    ASSERT_EQ(0, close(connfd[i]));
    ASSERT_EQ(0, close(listenfd[i]));
  }
}

}  // namespace netstack
