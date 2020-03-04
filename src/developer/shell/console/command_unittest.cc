// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/command.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace shell::console {

TEST(Command, ParseEmpty) {
  Command command;
  EXPECT_TRUE(command.Parse(""));
}

TEST(Command, ParseSimpleVarDecls) {
  Command command;
  EXPECT_TRUE(command.Parse("var i = 0"));
  EXPECT_TRUE(command.Parse("const i = 0"));
  EXPECT_TRUE(command.Parse("var i = 1_000"));
  EXPECT_TRUE(command.Parse("const i = 123_123"));

  EXPECT_FALSE(command.Parse("vari i = 0"));
  EXPECT_FALSE(command.Parse("smrzh"));
  EXPECT_FALSE(command.Parse("var i = 0_0"));
}

TEST(Command, ParseObjectVarDecls) {
  Command command;
  EXPECT_TRUE(command.Parse("var i = {}"));
  EXPECT_TRUE(command.Parse("const i = { }"));
  EXPECT_TRUE(command.Parse("var i = {a: 1}"));
  EXPECT_TRUE(command.Parse("const i = {a:1,}"));
  EXPECT_TRUE(command.Parse("var i = {a: 1, b : 2}"));
  EXPECT_TRUE(command.Parse("const i = {a:1,b:2,}"));

  EXPECT_FALSE(command.Parse("var i = {"));
  EXPECT_FALSE(command.Parse("var i = { a }"));
  EXPECT_FALSE(command.Parse("var i = { a,, }"));
}

}  // namespace shell::console
