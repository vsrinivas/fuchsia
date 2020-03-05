// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/test/interpreter_test.h"

TEST_F(InterpreterTest, LoadStringVariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto a_marx_brother = builder.VariableDefinition("a_marx_brother", TypeString(), false,
                                                   builder.StringLiteral("A Marx brother"));
  builder.VariableDefinition("groucho", TypeString(), false, builder.Variable(a_marx_brother));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("groucho");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));
}

TEST_F(InterpreterTest, LoadStringVariableFromAnotherContext) {
  constexpr uint64_t kFileId = 1;

  // First context.
  InterpreterTestContext* context_1 = CreateContext();
  shell()->CreateExecutionContext(context_1->id);

  NodeBuilder builder(kFileId);
  auto a_marx_brother = builder.VariableDefinition("a_marx_brother", TypeString(), false,
                                                   builder.StringLiteral("A Marx brother"));

  shell()->AddNodes(context_1->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context_1->id);

  // Second context.
  InterpreterTestContext* context_2 = CreateContext();
  shell()->CreateExecutionContext(context_2->id);

  builder.VariableDefinition("groucho", TypeString(), false, builder.Variable(a_marx_brother));

  shell()->AddNodes(context_2->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context_2->id);

  // Check execution.
  LoadGlobal("groucho");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context_1->GetResult());

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context_2->GetResult());

  fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));
}

TEST_F(InterpreterTest, LoadInt8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt8(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt8(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint8(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint8(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt16(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt16(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint16(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint16(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt32(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt32(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint32(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint32(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt64(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt64(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint64(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint64(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}
