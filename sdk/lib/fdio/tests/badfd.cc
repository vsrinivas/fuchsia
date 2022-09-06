// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/socket.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

class BadFdTest : public zxtest::Test {
 public:
  void SetUp() override {
    // Get the smallest unbound fd.
    for (int fd = 0; fd < INT_MAX; ++fd) {
      if (fcntl(fd, F_GETFD, nullptr) < 0) {
        ASSERT_EQ(errno, EBADF, "%s", strerror(errno));
        unbound_fd = fd;
        return;
      }
    }
  }

 protected:
  int unbound_fd;
};

TEST_F(BadFdTest, Bind) {
  ASSERT_EQ(bind(unbound_fd, nullptr, 0), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, Connect) {
  ASSERT_EQ(connect(unbound_fd, nullptr, 0), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, Listen) {
  ASSERT_EQ(listen(unbound_fd, 0), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, Accept4) {
  ASSERT_EQ(accept4(unbound_fd, nullptr, nullptr, 0), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, GetSockOpt) {
  ASSERT_EQ(getsockopt(unbound_fd, 0, 0, nullptr, nullptr), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, SetSockOpt) {
  ASSERT_EQ(setsockopt(unbound_fd, 0, 0, nullptr, 0), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, GetSockName) {
  ASSERT_EQ(getsockname(unbound_fd, nullptr, nullptr), -1);
  ASSERT_ERRNO(EBADF);
}

TEST_F(BadFdTest, GetPeerName) {
  ASSERT_EQ(getpeername(unbound_fd, nullptr, nullptr), -1);
  ASSERT_ERRNO(EBADF);
}
