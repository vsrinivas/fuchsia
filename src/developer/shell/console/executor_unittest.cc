// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/executor.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/developer/shell/console/scoped_interpreter.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using Executor = gtest::TestLoopFixture;

std::unique_ptr<shell::console::Command> MakeCommand(std::string line) {
  auto command = std::make_unique<shell::console::Command>();
  command->Parse(std::move(line));
  return command;
}

TEST_F(Executor, ExecuteVariableDecl) {
  shell::console::ScopedInterpreter interpreter;
  shell::console::Executor exec(interpreter.client());
  shell::console::Err e = exec.Execute(MakeCommand("var a = i"), nullptr);
  EXPECT_EQ(ZX_ERR_NEXT, e.code);
}

}  // namespace
