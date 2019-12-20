// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/command.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(Command, ParseEmpty) {
  cmd::Command command;

  EXPECT_TRUE(command.Parse(""));
  EXPECT_TRUE(command.is_empty());

  EXPECT_TRUE(command.Parse("  \t  "));
  EXPECT_TRUE(command.is_empty());

  EXPECT_TRUE(command.Parse("# This is a comment"));
  EXPECT_TRUE(command.is_empty());

  EXPECT_TRUE(command.Parse("   # Also a comment"));
  EXPECT_TRUE(command.is_empty());

  EXPECT_TRUE(command.Parse("control"));
  EXPECT_FALSE(command.is_empty());
}

TEST(Command, Parse) {
  cmd::Command command;

  EXPECT_TRUE(command.Parse(""));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_TRUE(command.Parse("ls"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls"));

  EXPECT_TRUE(command.Parse("ls -lart"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", "-lart"));

  EXPECT_TRUE(command.Parse("ls#not-a-comment"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls#not-a-comment"));

  EXPECT_TRUE(command.Parse("ls #a-comment"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls"));

  EXPECT_TRUE(command.Parse(" ls \t -lart \n banana\r"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", "-lart", "banana"));
}

TEST(Command, Quoted) {
  cmd::Command command;

  EXPECT_TRUE(command.Parse(" \"\" "));
  ASSERT_THAT(command.args(), testing::ElementsAre(""));

  EXPECT_TRUE(command.Parse(" \" \" "));
  ASSERT_THAT(command.args(), testing::ElementsAre(" "));

  EXPECT_TRUE(command.Parse("ls \" \" -lart"));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", " ", "-lart"));

  EXPECT_FALSE(command.Parse("really ls\"not\" a-quote"));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_FALSE(command.Parse("ls \"parse-error"));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_FALSE(command.Parse("ls \"also-parse-error  "));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_FALSE(command.Parse("ls \"another-parse-erro\"r  "));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_TRUE(command.Parse("ls \"not-parse-error\"  "));
  ASSERT_THAT(command.args(), testing::ElementsAre("ls", "not-parse-error"));

  EXPECT_TRUE(command.Parse("\"a\tb\""));
  ASSERT_THAT(command.args(), testing::ElementsAre("a\tb"));

  EXPECT_TRUE(command.Parse("\"a\nb\""));
  ASSERT_THAT(command.args(), testing::ElementsAre("a\nb"));

  EXPECT_TRUE(command.Parse("\"\r\""));
  ASSERT_THAT(command.args(), testing::ElementsAre("\r"));

  EXPECT_TRUE(command.Parse("\"\\\"\""));
  ASSERT_THAT(command.args(), testing::ElementsAre("\""));

  EXPECT_FALSE(command.Parse("\"\\\""));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_TRUE(command.Parse("\"\\\\\\\"\""));
  ASSERT_THAT(command.args(), testing::ElementsAre("\\\""));

  EXPECT_FALSE(command.Parse("\"\\z\""));
  ASSERT_THAT(command.args(), testing::ElementsAre());

  EXPECT_TRUE(command.Parse("comments are ok # see \""));
  ASSERT_THAT(command.args(), testing::ElementsAre("comments", "are", "ok"));
}

}  // namespace
