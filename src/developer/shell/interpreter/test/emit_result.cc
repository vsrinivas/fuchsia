// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/common/result.h"
#include "src/developer/shell/interpreter/test/interpreter_test.h"

#define EmitResultTest(name, type, left, right, result)                                        \
  TEST_F(InterpreterTest, name) {                                                              \
    constexpr uint64_t kFileId = 1;                                                            \
    InterpreterTestContext* context = CreateContext();                                         \
    shell()->CreateExecutionContext(context->id);                                              \
                                                                                               \
    shell::console::AstBuilder builder(kFileId);                                               \
    builder.AddVariableDeclaration("x", type,                                                  \
                                   (left < 0) ? builder.AddIntegerLiteral(-left, true)         \
                                              : builder.AddIntegerLiteral(left, false),        \
                                   false, true);                                               \
    builder.AddEmitResult(builder.AddAddition(false, builder.AddVariable("x"),                 \
                                              (right < 0)                                      \
                                                  ? builder.AddIntegerLiteral(-right, true)    \
                                                  : builder.AddIntegerLiteral(right, false))); \
                                                                                               \
    shell()->AddNodes(context->id, builder.DefsAsVectorView());                                \
    shell()->ExecuteExecutionContext(context->id);                                             \
    Finish(kExecute);                                                                          \
                                                                                               \
    ASSERT_EQ(fuchsia_shell::wire::ExecuteResult::kOk, context->GetResult());                  \
                                                                                               \
    CHECK_RESULT(0, result);                                                                   \
  }

EmitResultTest(EmitResultInt8, builder.TypeInt8(), 10, -30, "-20")
EmitResultTest(EmitResultUint8, builder.TypeUint8(), 10, 30, "40")

EmitResultTest(EmitResultInt16, builder.TypeInt16(), 1000, -3000, "-2000")
EmitResultTest(EmitResultUint16, builder.TypeUint16(), 1000, 3000, "4000")

EmitResultTest(EmitResultInt32, builder.TypeInt32(), 100000, -300000, "-200000")
EmitResultTest(EmitResultUint32, builder.TypeUint32(), 100000, 300000, "400000")

EmitResultTest(EmitResultInt64, builder.TypeInt64(), 10000000000LL, -30000000000LL, "-20000000000")
EmitResultTest(EmitResultUint64, builder.TypeUint64(), 10000000000LL, 30000000000LL, "40000000000")

TEST_F(InterpreterTest, EmitResultString) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("good", builder.TypeString(), builder.AddStringLiteral("good"),
                                 /*is_const=*/false, /*is_root=*/true);
  builder.AddEmitResult(builder.AddAddition(/*with_exceptions=*/true, builder.AddVariable("good"),
                                            builder.AddStringLiteral(" morning")));

  shell()->AddNodes(context->id, builder.DefsAsVectorView());
  shell()->ExecuteExecutionContext(context->id);
  Finish(kExecute);

  ASSERT_EQ(fuchsia_shell::wire::ExecuteResult::kOk, context->GetResult());

  CHECK_RESULT(0, "\"good morning\"");
}

TEST_F(InterpreterTest, EmitObject) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell()->CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);

  builder.OpenObject();
  shell::console::AstBuilder::NodePair obj1 = builder.CloseObject();
  builder.AddVariableDeclaration("obj1", builder.TypeObject(obj1.schema_node), obj1.value_node,
                                 /*is_const=*/false, /*is_root=*/true);
  builder.AddEmitResult(builder.AddVariable("obj1"));

  builder.OpenObject();
  builder.AddField("alpha", builder.AddIntegerLiteral(100, false), builder.TypeUint64());
  builder.AddField("beta", builder.AddStringLiteral("hello"), builder.TypeString());
  shell::console::AstBuilder::NodePair obj2 = builder.CloseObject();
  builder.AddEmitResult(obj2.value_node);

  ASSERT_CALL_OK(shell()->AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell()->ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(fuchsia_shell::wire::ExecuteResult::kOk, context->GetResult());

  CHECK_RESULT(0, "{}");
  CHECK_RESULT(1, "{alpha: uint64(100), beta: string(\"hello\")}");
}

TEST_F(InterpreterTest, EmitMultipleResults) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("x", builder.TypeInt64(), builder.AddIntegerLiteral(1250, false),
                                 /*is_const=*/false, /*is_root=*/true);
  builder.AddEmitResult(builder.AddAddition(/*with_exceptions=*/false, builder.AddVariable("x"),
                                            builder.AddIntegerLiteral(3000, true)));
  builder.AddEmitResult(builder.AddAddition(/*with_exceptions=*/false, builder.AddVariable("x"),
                                            builder.AddIntegerLiteral(3000, false)));
  builder.AddEmitResult(builder.AddAddition(/*with_exceptions=*/false, builder.AddVariable("x"),
                                            builder.AddIntegerLiteral(1000, true)));

  shell()->AddNodes(context->id, builder.DefsAsVectorView());
  shell()->ExecuteExecutionContext(context->id);
  Finish(kExecute);

  ASSERT_EQ(fuchsia_shell::wire::ExecuteResult::kOk, context->GetResult());

  CHECK_RESULT(0, "-1750");
  CHECK_RESULT(1, "4250");
  CHECK_RESULT(2, "250");
}
