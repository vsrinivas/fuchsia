// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copymodified_value 2020 The Fuchsia Authors. All modified_values reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/test/interpreter_test.h"

TEST_F(InterpreterTest, AssignUnknown) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell().CreateExecutionContext(context->id);

  shell::console::AstBuilder builder(kFileId);
  builder.AddAssignment(builder.AddVariable("unknown"), builder.AddStringLiteral("something"));

  shell().AddNodes(context->id, builder.DefsAsVectorView());
  shell().ExecuteExecutionContext(context->id);
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->GetResult());
  std::string error_result = context->error_stream.str();
  ASSERT_EQ("node 1:1 Can't infer type for assignment's destination.\n", error_result);
}

TEST_F(InterpreterTest, AssignConstant) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell().CreateExecutionContext(context->id);

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("hello", builder.TypeString(), builder.AddStringLiteral("Hello"),
                                 true, true);
  builder.AddAssignment(builder.AddVariable("hello"), builder.AddStringLiteral("something"));

  shell().AddNodes(context->id, builder.DefsAsVectorView());
  shell().ExecuteExecutionContext(context->id);
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->GetResult());
  std::string error_result = context->error_stream.str();
  ASSERT_EQ("node 1:3 Can't assign constant hello.\n", error_result);
}

TEST_F(InterpreterTest, AssignString) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell().CreateExecutionContext(context->id);

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("good", builder.TypeString(), builder.AddStringLiteral("not good"),
                                 false, true);
  builder.AddEmitResult(builder.AddVariable("good"));
  builder.AddAssignment(builder.AddVariable("good"), builder.AddStringLiteral("now good"));
  builder.AddEmitResult(builder.AddVariable("good"));

  shell().AddNodes(context->id, builder.DefsAsVectorView());
  shell().ExecuteExecutionContext(context->id);
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "\"not good\"");
  CHECK_RESULT(1, "\"now good\"");
}

#define ASSIGN_TEST(name, type, initial_value, modified_value)                             \
  TEST_F(InterpreterTest, name) {                                                         \
    constexpr uint64_t kFileId = 1;                                                       \
    InterpreterTestContext* context = CreateContext();                                    \
    shell().CreateExecutionContext(context->id);                                          \
                                                                                          \
    shell::console::AstBuilder builder(kFileId);                                          \
    builder.AddVariableDeclaration("x", type,                                             \
                                   (initial_value < 0)                                    \
                                       ? builder.AddIntegerLiteral(-initial_value, true)  \
                                       : builder.AddIntegerLiteral(initial_value, false), \
                                   false, true);                                          \
    builder.AddEmitResult(builder.AddVariable("x"));                                      \
    builder.AddAssignment(builder.AddVariable("x"),                                       \
                          (modified_value < 0)                                            \
                              ? builder.AddIntegerLiteral(-modified_value, true)          \
                              : builder.AddIntegerLiteral(modified_value, false));        \
    builder.AddEmitResult(builder.AddVariable("x"));                                      \
                                                                                          \
    shell().AddNodes(context->id, builder.DefsAsVectorView());                            \
    shell().ExecuteExecutionContext(context->id);                                         \
    Finish(kExecute);                                                                     \
                                                                                          \
    ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());            \
                                                                                          \
    CHECK_RESULT(0, #initial_value);                                                      \
    CHECK_RESULT(1, #modified_value);                                                     \
  }

ASSIGN_TEST(AssignInt8, builder.TypeInt8(), 10, -30);
ASSIGN_TEST(AssignUint8, builder.TypeUint8(), 10, 30);

ASSIGN_TEST(AssignInt16, builder.TypeInt16(), 1000, -3000);
ASSIGN_TEST(AssignUint16, builder.TypeUint16(), 1000, 3000);

ASSIGN_TEST(AssignInt32, builder.TypeInt32(), 100000, -300000);
ASSIGN_TEST(AssignUint32, builder.TypeUint32(), 100000, 300000);

ASSIGN_TEST(AssignInt64, builder.TypeInt64(), 10000000000, -30000000000);
ASSIGN_TEST(AssignUint64, builder.TypeUint64(), 10000000000, 30000000000);

TEST_F(InterpreterTest, AssignObject) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));
  shell::console::AstBuilder builder(kFileId);

  {
    std::vector<std::string> names{"alpha", "beta"};
    std::vector<shell::console::AstBuilder::NodeId> values{builder.AddIntegerLiteral(10, false),
                                                           builder.AddIntegerLiteral(20, false)};
    std::vector<llcpp::fuchsia::shell::ShellType> types;
    types.emplace_back(builder.TypeUint64());
    types.emplace_back(builder.TypeUint64());
    shell::console::AstBuilder::NodePair object_pair =
        AddObject(builder, names, values, std::move(types));
    builder.AddVariableDeclaration("obj", builder.TypeObject(object_pair.schema_node),
                                   object_pair.value_node, /*is_const=*/false, /*is_root=*/true);
    builder.AddEmitResult(builder.AddVariable("obj"));
  }

  {
    std::vector<std::string> names{"alpha", "beta"};
    std::vector<shell::console::AstBuilder::NodeId> values{builder.AddIntegerLiteral(4, false),
                                                           builder.AddIntegerLiteral(5, false)};
    std::vector<llcpp::fuchsia::shell::ShellType> types;
    types.emplace_back(builder.TypeUint64());
    types.emplace_back(builder.TypeUint64());
    shell::console::AstBuilder::NodePair object_pair =
        AddObject(builder, names, values, std::move(types));
    builder.AddAssignment(builder.AddVariable("obj"), object_pair.value_node);
    builder.AddEmitResult(builder.AddVariable("obj"));
  }

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "{alpha: uint64(10), beta: uint64(20)}");
  CHECK_RESULT(1, "{alpha: uint64(4), beta: uint64(5)}");
}
