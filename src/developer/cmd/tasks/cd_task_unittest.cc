// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/cd_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

void ChangeDirectory(std::string directory) {
  cmd::Command command;
  command.Parse("cd " + directory);
  cmd::CdTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));
}

void CheckDirectory(const char* expected) {
  char buffer[PATH_MAX];
  getcwd(buffer, sizeof(buffer));
  EXPECT_STREQ(expected, buffer);
  EXPECT_STREQ(expected, getenv("PWD"));
}

TEST(CdTask, Control) {
  char original_directory[PATH_MAX];
  getcwd(original_directory, sizeof(original_directory));

  ChangeDirectory("/pkg");
  CheckDirectory("/pkg");

  EXPECT_EQ(0, chdir(original_directory)) << original_directory;
}

TEST(CdTask, RelativeTraveral) {
  char original_directory[PATH_MAX];
  getcwd(original_directory, sizeof(original_directory));

  ChangeDirectory("/");
  CheckDirectory("/");

  ChangeDirectory("pkg");
  CheckDirectory("/pkg");

  ChangeDirectory("..");
  CheckDirectory("/");

  ChangeDirectory("..");
  CheckDirectory("/");

  EXPECT_EQ(0, chdir(original_directory)) << original_directory;
}

}  // namespace
