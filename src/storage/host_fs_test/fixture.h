// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_HOST_FS_TEST_FIXTURE_H_
#define SRC_STORAGE_HOST_FS_TEST_FIXTURE_H_

#include <fcntl.h>

#include <gtest/gtest.h>

#include "src/storage/minfs/host.h"

namespace fs_test {

class HostFilesystemTest : public testing::Test {
 public:
  void SetUp() override;
  void TearDown() override;

 protected:
  int RunFsck();

 private:
  std::string mount_path_;
};

template <typename F, typename Buf>
[[nodiscard]] bool CheckStreamAll(F function, int fd, Buf buf, size_t len) {
  return function(fd, buf, len) == static_cast<ssize_t>(len);
}

}  // namespace fs_test

#endif  // SRC_STORAGE_HOST_FS_TEST_FIXTURE_H_
