// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/wait.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

#include "zxtest/base/runner.h"

namespace {

pid_t pid = -1;

TEST(ZxtestContextTest, LackOfContextAborts) {
  ASSERT_NE(pid, -1);
  ASSERT_NE(pid, 0);

  int status;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_FALSE(WIFEXITED(status));
}

}  // namespace

int main(int argc, char** argv) {
  pid = fork();
  if (pid == -1) {
    return -1;
  }
  if (pid == 0) {
    // Child process should abort.
    EXPECT_TRUE(true);
    return 0;
  }
  return RUN_ALL_TESTS(argc, argv);
}
