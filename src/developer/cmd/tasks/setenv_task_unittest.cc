// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/setenv_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(SetenvTask, Control) {
  cmd::Command command;
  command.Parse("setenv EXAMPLE_ENVIRON_VAR myTestValue");
  cmd::SetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));

  EXPECT_STREQ("myTestValue", getenv("EXAMPLE_ENVIRON_VAR"));
  unsetenv("EXAMPLE_ENVIRON_VAR");
}

TEST(SetenvTask, TooManyArgs) {
  cmd::Command command;
  command.Parse("setenv FOO BAR BAZ");
  cmd::SetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

TEST(SetenvTask, ContainsEquals) {
  cmd::Command command;
  command.Parse("setenv FOO=BAR BAZ");
  cmd::SetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
  EXPECT_EQ(nullptr, getenv("FOO"));
  EXPECT_EQ(nullptr, getenv("FOO=BAR"));
}

}  // namespace
