// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/getenv_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(GetenvTask, ZeroArgs) {
  cmd::Command command;
  command.Parse("getenv");
  cmd::GetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

TEST(GetenvTask, Present) {
  cmd::Command command;
  command.Parse("getenv PWD");
  cmd::GetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

TEST(GetenvTask, Absent) {
  cmd::Command command;
  command.Parse("getenv DOES_NOT_EXIST");
  cmd::GetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

TEST(GetenvTask, TooManyArgs) {
  cmd::Command command;
  command.Parse("getenv FOO BAR BAZ");
  cmd::GetenvTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

}  // namespace
