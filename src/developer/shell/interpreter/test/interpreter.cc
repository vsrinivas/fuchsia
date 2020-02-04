// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/test/interpreter_test.h"

TEST_F(InterpreterTest, ContextNotCreated) {
  shell()->ExecuteExecutionContext(1);
  Run();

  ASSERT_EQ("Execution context 1 not defined.\n", GlobalErrors());
}

TEST_F(InterpreterTest, ContextCreatedTwice) {
  shell()->CreateExecutionContext(1);
  shell()->CreateExecutionContext(1);
  Run();

  ASSERT_EQ("Execution context 1 is already in use.\n", GlobalErrors());
}

TEST_F(InterpreterTest, NoPendingInstruction) {
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("No pending instruction to execute.\n", error_result);
}
