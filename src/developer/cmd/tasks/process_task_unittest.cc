// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/process_task.h"

#include <unistd.h>

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

class ProcessTask : public gtest::RealLoopFixture {
 public:
  zx_status_t ExecuteCommand(std::string line, cmd::Task::CompletionCallback callback) {
    cmd::Command command;
    command.Parse(std::move(line));
    cmd::ProcessTask task(dispatcher());
    return task.Execute(std::move(command), std::move(callback));
  }
};

TEST_F(ProcessTask, Search) {
  EXPECT_EQ(ZX_ERR_NOT_FOUND, ExecuteCommand("/does/not/exist", nullptr));
  EXPECT_EQ(ZX_ERR_ASYNC, ExecuteCommand("/pkg/bin/trivial_success", nullptr));
  EXPECT_EQ(ZX_ERR_NOT_FOUND, ExecuteCommand("trivial_success", nullptr));

  const char* path = getenv("PATH");

  setenv("PATH", "/pkg/bin", 1);
  EXPECT_EQ(ZX_ERR_ASYNC, ExecuteCommand("/pkg/bin/trivial_success", nullptr));
  EXPECT_EQ(ZX_ERR_ASYNC, ExecuteCommand("trivial_success", nullptr));
  setenv("PATH", "/does/not/exist:/pkg/bin:/also/does/not/exit", 1);
  EXPECT_EQ(ZX_ERR_ASYNC, ExecuteCommand("/pkg/bin/trivial_success", nullptr));
  EXPECT_EQ(ZX_ERR_ASYNC, ExecuteCommand("trivial_success", nullptr));

  EXPECT_EQ(ZX_ERR_NOT_FOUND, ExecuteCommand("pkg/bin/trivial_success", nullptr));

  if (path) {
    setenv("PATH", path, 1);
  } else {
    unsetenv("PATH");
  }
}

}  // namespace
