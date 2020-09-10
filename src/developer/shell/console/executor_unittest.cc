// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/executor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/shell/console/scoped_interpreter.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

using Executor = gtest::TestLoopFixture;

class CommandTester {
 public:
  CommandTester(shell::console::Executor& executor) : executor_(executor) {}

  void Test(std::string cmd, const std::string_view& expected_out,
            const std::string_view& expected_err) {
    std::stringstream outs;
    std::stringstream errs;

    shell::console::Err e = executor_.Execute(MakeCommand(std::move(cmd)), StringCapture(outs),
                                              StringCapture(errs), nullptr);
    EXPECT_EQ(expected_out, outs.str());
    EXPECT_EQ(expected_err, errs.str());
    EXPECT_EQ(ZX_ERR_NEXT, e.code);
  }

 private:
  shell::console::Executor& executor_;

  static fit::function<void(const std::string&)> StringCapture(std::stringstream& ss) {
    return [&ss](const std::string& str) { ss << str; };
  }

  static std::unique_ptr<shell::console::Command> MakeCommand(std::string&& line) {
    auto command = std::make_unique<shell::console::Command>();
    command->Parse(std::move(line));
    return command;
  }
};

TEST_F(Executor, ExecuteVariableDecl) {
  shell::console::ScopedInterpreter interpreter;
  shell::console::Executor exec(interpreter.client());
  CommandTester tester(exec);
  tester.Test("var a = 2", "2", "");
  tester.Test("const b = 4", "4", "");
}

TEST_F(Executor, ExecuteObjectDecl) {
  shell::console::ScopedInterpreter interpreter;
  shell::console::Executor exec(interpreter.client());
  CommandTester tester(exec);

  // Success cases:
  tester.Test("var a = { }", "{}", "");
  tester.Test("var b = { a:1 }", "{a: 1}", "");
  tester.Test("var c = { a:1, b:2 }", "{a: 1, b: 2}", "");

  // An error from the interpreter:
  tester.Test("var c = { a:1, b:2 }", "", "Variable 'c' already defined.First definition.");

  // An error from the parser:
  tester.Test("var c = { a:1,", "", "Invalid command: Unrecoverable parse error");
}

}  // namespace
