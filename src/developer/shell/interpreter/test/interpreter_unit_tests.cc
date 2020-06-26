// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/value.h"

// Fake interpreter. This is used to do unit tests on strings (Strings need an interpreter to be
// created).
class TestInterpreter : public shell::interpreter::Interpreter {
 public:
  TestInterpreter() = default;

  void EmitError(shell::interpreter::ExecutionContext* context,
                 std::string error_message) override {}
  void EmitError(shell::interpreter::ExecutionContext* context, shell::interpreter::NodeId node_id,
                 std::string error_message) override {}
  void DumpDone(shell::interpreter::ExecutionContext* context) override {}
  void ContextDone(shell::interpreter::ExecutionContext* context) override {}
  void ContextDoneWithAnalysisError(shell::interpreter::ExecutionContext* context) override {}
  void ContextDoneWithExecutionError(shell::interpreter::ExecutionContext* context) override {}
  void TextResult(shell::interpreter::ExecutionContext* context, std::string_view text) override {}
  void Result(shell::interpreter::ExecutionContext* context,
              const shell::interpreter::Value& result) override {}
};

TEST(InterpreterUnitTest, AssignValueToItself) {
  TestInterpreter interpreter;
  {
    shell::interpreter::Value value;
    value.SetString(&interpreter, "Test string.");
    ASSERT_EQ(value.GetString()->value(), "Test string.");
    value.Set(value);
    ASSERT_EQ(value.GetString()->value(), "Test string.");
    ASSERT_EQ(interpreter.string_count(), 1U);
  }
  ASSERT_EQ(interpreter.string_count(), 0U);
}
