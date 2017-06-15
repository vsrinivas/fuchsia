// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tese tests ensure the magenta libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string>
#include <thread>

#include "gtest/gtest.h"
#include "lib/ftl/macros.h"

namespace netstack {
namespace {

class NetTest {
 public:
  NetTest() {}
};

void StreamRead(struct sockaddr_in* addr, std::string* out) {
  int connfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GT(connfd, 0);

  int ret = connect(connfd, (const struct sockaddr*)addr, sizeof(*addr));
  ASSERT_EQ(0, ret) << "connect failed: " << errno;

  int n;
  char buf[4096];
  while ((n = read(connfd, buf, sizeof(buf))) > 0) {
    out->append(buf, n);
  }

  EXPECT_EQ(close(connfd), 0);
}

TEST(NetTest, LoopbackStream) {
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

  ASSERT_STREQ(msg, out.c_str());
}

// TODO datagrams
// TODO port reuse

}  // namespace
}  // namespace netstack
