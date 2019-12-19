// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/executor.h"

#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using Executor = gtest::TestLoopFixture;

cmd::Command MakeCommand(std::string line) {
  cmd::Command command;
  command.Parse(std::move(line));
  return command;
}

TEST_F(Executor, ExecuteNothing) {
  cmd::Executor exec(dispatcher());
  EXPECT_EQ(ZX_ERR_NEXT, exec.Execute(cmd::Command(), nullptr));
}

TEST_F(Executor, Quit) {
  cmd::Executor exec(dispatcher());
  EXPECT_EQ(ZX_ERR_STOP, exec.Execute(MakeCommand("exit"), nullptr));
  EXPECT_EQ(ZX_ERR_STOP, exec.Execute(MakeCommand("quit"), nullptr));
}

TEST_F(Executor, FailToExecute) {
  cmd::Executor exec(dispatcher());
  EXPECT_EQ(ZX_ERR_NOT_FOUND, exec.Execute(MakeCommand("/does/not/exist"), nullptr));
}

}  // namespace
