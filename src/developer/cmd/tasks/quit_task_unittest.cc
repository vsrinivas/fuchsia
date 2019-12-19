// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/quit_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(QuitTask, Control) {
  cmd::Command command;
  command.Parse("ignored");
  cmd::QuitTask task(nullptr);
  EXPECT_EQ(ZX_ERR_STOP, task.Execute(std::move(command), nullptr));
}

}  // namespace
