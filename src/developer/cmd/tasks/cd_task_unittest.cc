// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/tasks/cd_task.h"

#include <utility>

#include "gtest/gtest.h"

namespace {

TEST(CdTask, Control) {
  char original_directory[PATH_MAX];
  getcwd(original_directory, sizeof(original_directory));

  cmd::Command command;
  command.Parse("cd /pkg");
  cmd::CdTask task(nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, task.Execute(std::move(command), nullptr));

  char buffer[PATH_MAX];
  getcwd(buffer, sizeof(buffer));
  EXPECT_STREQ("/pkg", buffer);

  chdir(original_directory);
}

}  // namespace
