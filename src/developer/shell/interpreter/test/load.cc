// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/common/ast_builder.h"
#include "src/developer/shell/interpreter/test/interpreter_test.h"

TEST_F(InterpreterTest, LoadStringVariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("a_marx_brother", builder.TypeString(),
                                 builder.AddStringLiteral("A Marx brother"), false, true);
  builder.AddVariableDeclaration("groucho", builder.TypeString(),
                                 builder.AddVariable("a_marx_brother"), false, true);

  builder.AddEmitResult(builder.AddVariable("groucho"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "\"A Marx brother\"");
}

TEST_F(InterpreterTest, LoadStringVariableFromAnotherContext) {
  constexpr uint64_t kFileId_1 = 1;
  constexpr uint64_t kFileId_2 = 2;

  // First context.
  InterpreterTestContext* context_1 = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context_1->id));

  shell::console::AstBuilder builder(kFileId_1);
  builder.AddVariableDeclaration("a_marx_brother", builder.TypeString(),
                                 builder.AddStringLiteral("A Marx brother"), false, true);

  ASSERT_CALL_OK(shell().AddNodes(context_1->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context_1->id));
  Run(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context_1->GetResult());

  shell::console::AstBuilder builder_2(kFileId_2);
  // Second context.
  InterpreterTestContext* context_2 = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context_2->id));

  builder_2.AddVariableDeclaration("groucho", builder_2.TypeString(),
                                   builder_2.AddVariable("a_marx_brother"), false, true);

  builder_2.AddEmitResult(builder_2.AddVariable("groucho"));

  ASSERT_CALL_OK(shell().AddNodes(context_2->id, builder_2.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context_2->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context_2->GetResult());

  CHECK_RESULT(0, "\"A Marx brother\"");
}

TEST_F(InterpreterTest, LoadInt8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeInt8(), builder.AddIntegerLiteral(1, true), false,
                                 true);
  builder.AddVariableDeclaration("y", builder.TypeInt8(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "-1");
}

TEST_F(InterpreterTest, LoadUint8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeUint8(), builder.AddIntegerLiteral(1, false),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeUint8(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "1");
}

TEST_F(InterpreterTest, LoadInt16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeInt16(), builder.AddIntegerLiteral(1, true),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeInt16(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "-1");
}

TEST_F(InterpreterTest, LoadUint16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeUint16(), builder.AddIntegerLiteral(1, false),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeUint16(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "1");
}

TEST_F(InterpreterTest, LoadInt32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeInt32(), builder.AddIntegerLiteral(1, true),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeInt32(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "-1");
}

TEST_F(InterpreterTest, LoadUint32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeUint32(), builder.AddIntegerLiteral(1, false),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeUint32(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "1");
}

TEST_F(InterpreterTest, LoadInt64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeInt64(), builder.AddIntegerLiteral(1, true),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeInt64(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "-1");
}

TEST_F(InterpreterTest, LoadUint64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeUint64(), builder.AddIntegerLiteral(1, false),
                                 false, true);
  builder.AddVariableDeclaration("y", builder.TypeUint64(), builder.AddVariable("x"), false, true);

  builder.AddEmitResult(builder.AddVariable("y"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "1");
}
