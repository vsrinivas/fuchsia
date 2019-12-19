// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/unsetenv_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(UnsetenvTask, Control) {
  setenv("ANOTHER_EXAMPLE_VAR", "XYZZY", 1);

  cmd::Command command;
  command.Parse("unsetenv ANOTHER_EXAMPLE_VAR");
  cmd::UnsetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));

  EXPECT_EQ(nullptr, getenv("ANOTHER_EXAMPLE_VAR"));
}

TEST(UnsetenvTask, TooManyArgs) {
  cmd::Command command;
  command.Parse("unsetenv FOO BAR");
  cmd::UnsetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

}  // namespace
