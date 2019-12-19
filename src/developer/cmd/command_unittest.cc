// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/command.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Command, ParseEmpty) {
  cmd::Command command;

  command.Parse("");
  EXPECT_TRUE(command.is_empty());

  command.Parse("  \t  ");
  EXPECT_TRUE(command.is_empty());

  command.Parse("# This is a comment");
  EXPECT_TRUE(command.is_empty());

  command.Parse("   # Also a comment");
  EXPECT_TRUE(command.is_empty());

  command.Parse("control");
  EXPECT_FALSE(command.is_empty());
}

TEST(Command, Parse) {
  cmd::Command command;

  command.Parse("");
  ASSERT_THAT(command.args(), testing::ElementsAre());

  command.Parse("ls");
  ASSERT_THAT(command.args(), testing::ElementsAre("ls"));

  command.Parse("ls -lart");
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", "-lart"));

  command.Parse("ls#not-a-comment");
  ASSERT_THAT(command.args(), testing::ElementsAre("ls#not-a-comment"));

  command.Parse("ls #a-comment");
  ASSERT_THAT(command.args(), testing::ElementsAre("ls"));

  command.Parse(" ls \t -lart \n banana\r");
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", "-lart", "banana"));
}

}  // namespace
