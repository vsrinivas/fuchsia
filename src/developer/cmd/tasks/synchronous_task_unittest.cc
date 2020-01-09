// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/synchronous_task.h"

#include "gtest/gtest.h"
#include "src/developer/cmd/legacy/builtins.h"

namespace {

static int g_call_count = 0;

int IncrementCallCount(int argc, const char** argv) {
  g_call_count++;
  return -3;
}

TEST(SynchronousTask, Control) {
  cmd::Command command;
  command.Parse("ignored");
  cmd::SynchronousTask task(nullptr, &IncrementCallCount);
  g_call_count = 0;
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
  EXPECT_EQ(1, g_call_count);
}

TEST(SynchronousTask, Ls) {
  cmd::Command command;
  command.Parse("ls");
  cmd::SynchronousTask task(nullptr, &zxc_ls);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

}  // namespace
