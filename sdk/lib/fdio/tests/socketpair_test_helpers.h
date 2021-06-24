// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_TESTS_SOCKETPAIR_TEST_HELPERS_H_
#define LIB_FDIO_TESTS_SOCKETPAIR_TEST_HELPERS_H_

#include <sys/socket.h>
#include <sys/types.h>

#include <array>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

namespace fdio_tests {

class SocketPair : public testing::TestWithParam<uint16_t> {
 protected:
  void SetUp() override {
    int int_fds[fds_.size()];
    ASSERT_EQ(socketpair(AF_UNIX, GetParam(), 0, int_fds), 0) << strerror(errno);
    for (size_t i = 0; i < fds_.size(); ++i) {
      fds_[i].reset(int_fds[i]);
    }
  }

  const std::array<fbl::unique_fd, 2>& fds() { return fds_; }
  std::array<fbl::unique_fd, 2>& mutable_fds() { return fds_; }

 private:
  std::array<fbl::unique_fd, 2> fds_;
};

inline std::string TypeToString(const testing::TestParamInfo<uint16_t>& info) {
  switch (info.param) {
    case SOCK_STREAM:
      return "Stream";
    case SOCK_DGRAM:
      return "Datagram";
    default:
      return testing::PrintToStringParamName()(info);
  }
}

}  // namespace fdio_tests

#endif  // LIB_FDIO_TESTS_SOCKETPAIR_TEST_HELPERS_H_
