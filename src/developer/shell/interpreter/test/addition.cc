// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "src/developer/shell/common/ast_builder.h"
#include "src/developer/shell/interpreter/test/interpreter_test.h"

// - String ----------------------------------------------------------------------------------------

TEST_F(InterpreterTest, StringAdditionOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("marx", builder.TypeString(), builder.AddStringLiteral("Marx"),
                                 false, true);
  // Checks s1 + (s2 + s3).
  builder.AddVariableDeclaration(
      "groucho1", builder.TypeString(),
      builder.AddAddition(
          /*with_exceptions=*/true, builder.AddStringLiteral("A "),
          builder.AddAddition(/*with_exceptions=*/true, builder.AddVariable("marx"),
                              builder.AddStringLiteral(" brother"))),
      false, true);
  // Checks (s1 + s2) + s3.
  builder.AddVariableDeclaration(
      "groucho2", builder.TypeString(),
      builder.AddAddition(
          /*with_exceptions=*/true,
          builder.AddAddition(/*with_exceptions=*/true, builder.AddStringLiteral("A "),
                              builder.AddVariable("marx")),
          builder.AddStringLiteral(" brother")),
      false, true);

  builder.AddEmitResult(builder.AddVariable("groucho1"));
  builder.AddEmitResult(builder.AddVariable("groucho2"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "\"A Marx brother\"");
  CHECK_RESULT(1, "\"A Marx brother\"");
}

TEST_F(InterpreterTest, StringAdditionEmpty) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("foo", builder.TypeString(), builder.AddStringLiteral("foo"),
                                 false, true);
  builder.AddVariableDeclaration(
      "foo1", builder.TypeString(),
      builder.AddAddition(/*with_exceptions=*/true, builder.AddVariable("foo"),
                          builder.AddStringLiteral("")),
      false, true);
  builder.AddVariableDeclaration(
      "foo2", builder.TypeString(),
      builder.AddAddition(/*with_exceptions=*/true, builder.AddStringLiteral(""),
                          builder.AddVariable("foo")),
      false, true);

  builder.AddEmitResult(builder.AddVariable("foo1"));
  builder.AddEmitResult(builder.AddVariable("foo2"));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  CHECK_RESULT(0, "\"foo\"");
  CHECK_RESULT(1, "\"foo\"");
}

// - Helpers ---------------------------------------------------------------------------------------

#define ExecuteAddition(type, with_exceptions, left, right)                           \
  constexpr uint64_t kFileId = 1;                                                     \
  InterpreterTestContext* context = CreateContext();                                  \
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));                        \
                                                                                      \
  shell::console::AstBuilder builder(kFileId);                                        \
  builder.AddVariableDeclaration("x", type,                                           \
                                 (left < 0) ? builder.AddIntegerLiteral(-left, true)  \
                                            : builder.AddIntegerLiteral(left, false), \
                                 false, true);                                        \
  builder.AddVariableDeclaration(                                                     \
      "y", type,                                                                      \
      builder.AddAddition(with_exceptions, builder.AddVariable("x"),                  \
                          (right < 0) ? builder.AddIntegerLiteral(-right, true)       \
                                      : builder.AddIntegerLiteral(right, false)),     \
      false, true);                                                                   \
                                                                                      \
  builder.AddEmitResult(builder.AddVariable("y"));                                    \
                                                                                      \
  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));          \
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));                       \
  Finish(kExecute);

#define DoAdditionTest(name, type, with_exceptions, left, right, result)       \
  TEST_F(InterpreterTest, name) {                                              \
    ExecuteAddition(builder.type, with_exceptions, left, right);               \
                                                                               \
    ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult()); \
                                                                               \
    std::string string_result = std::to_string(result);                        \
    CHECK_RESULT(0, string_result);                                            \
  }

#define DoAdditionTestException(name, type, left, right, errors)                       \
  TEST_F(InterpreterTest, name) {                                                      \
    ExecuteAddition(builder.type, /*with_exceptions=*/true, left, right);              \
                                                                                       \
    ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::EXECUTION_ERROR, context->result); \
                                                                                       \
    std::string error_result = context->error_stream.str();                            \
    ASSERT_EQ(errors, error_result);                                                   \
  }

#define AdditionTest(name, type, left, right, result) \
  DoAdditionTest(name, type, /*with_exceptions=*/true, left, right, result);

#define AdditionTestWithException(name, type, left, right, result, errors)    \
  DoAdditionTest(name, type, /*with_exceptions=*/false, left, right, result); \
  DoAdditionTestException(name##Exception, type, left, right, errors);

// - Int8 ------------------------------------------------------------------------------------------

AdditionTest(Int8AdditionOk1, TypeInt8(), 1, 3, 4);
AdditionTest(Int8AdditionOk2, TypeInt8(), -1, 3, 2);
AdditionTest(Int8AdditionOk3, TypeInt8(), 1, -3, -2);
AdditionTestWithException(Int8AdditionOverflowException, TypeInt8(), 127, 1, -128,
                          "Int8 overflow when adding 127 and 1.\n");
AdditionTestWithException(Int8AdditionUnderflow, TypeInt8(), -128, -1, 127,
                          "Int8 underflow when adding -128 and -1.\n");

// - Uint8 -----------------------------------------------------------------------------------------

AdditionTest(Uint8AdditionOk, TypeUint8(), 1, 3, 4);
AdditionTestWithException(Uint8AdditionOverflow, TypeUint8(), 250, 6, 0,
                          "Uint8 overflow when adding 250 and 6.\n");

// - Int16 -----------------------------------------------------------------------------------------

AdditionTest(Int16AdditionOk1, TypeInt16(), 1000, 3000, 4000);
AdditionTest(Int16AdditionOk2, TypeInt16(), -1000, 3000, 2000);
AdditionTest(Int16AdditionOk3, TypeInt16(), 1000, -3000, -2000);
AdditionTestWithException(Int16AdditionOverflow, TypeInt16(), 32000, 768, -32768,
                          "Int16 overflow when adding 32000 and 768.\n");
AdditionTestWithException(Int16AdditionUnderflow, TypeInt16(), -32000, -769, 32767,
                          "Int16 underflow when adding -32000 and -769.\n");

// - Uint16 ----------------------------------------------------------------------------------------

AdditionTest(Uint16AdditionOk, TypeUint16(), 1000, 3000, 4000);
AdditionTestWithException(Uint16AdditionOverflow, TypeUint16(), 65000, 536, 0,
                          "Uint16 overflow when adding 65000 and 536.\n");

// - Int32 -----------------------------------------------------------------------------------------

AdditionTest(Int32AdditionOk1, TypeInt32(), 100000, 300000, 400000);
AdditionTest(Int32AdditionOk2, TypeInt32(), -100000, 300000, 200000);
AdditionTest(Int32AdditionOk3, TypeInt32(), 100000, -300000, -200000);
AdditionTestWithException(Int32AdditionOverflow, TypeInt32(), 2147480000, 3648, -2147483648,
                          "Int32 overflow when adding 2147480000 and 3648.\n");
AdditionTestWithException(Int32AdditionUnderflow, TypeInt32(), -2147480000, -3649, 2147483647,
                          "Int32 underflow when adding -2147480000 and -3649.\n");

// - Uint32 ----------------------------------------------------------------------------------------

AdditionTest(Uint32AdditionOk, TypeUint32(), 1000000, 3000000, 4000000);
AdditionTestWithException(Uint32AdditionOverflow, TypeUint32(), 4294960000, 7296, 0,
                          "Uint32 overflow when adding 4294960000 and 7296.\n");

// - Int64 -----------------------------------------------------------------------------------------

AdditionTest(Int64AdditionOk1, TypeInt64(), 100000000000L, 300000000000L, 400000000000L);
AdditionTest(Int64AdditionOk2, TypeInt64(), -100000000000L, 300000000000L, 200000000000L);
AdditionTest(Int64AdditionOk3, TypeInt64(), 100000000000L, -300000000000L, -200000000000L);
AdditionTestWithException(Int64AdditionOverflow, TypeInt64(), 9223372036854770000LL, 5808,
                          std::numeric_limits<int64_t>::min(),
                          "Int64 overflow when adding 9223372036854770000 and 5808.\n");
AdditionTestWithException(Int64AdditionUnderflow, TypeInt64(), -9223372036854770000LL, -5809,
                          std::numeric_limits<int64_t>::max(),
                          "Int64 underflow when adding -9223372036854770000 and -5809.\n");

// - Uint64 ----------------------------------------------------------------------------------------

AdditionTest(Uint64AdditionOk, TypeUint64(), 100000000000L, 300000000000L, 400000000000L);
AdditionTestWithException(Uint64AdditionOverflow, TypeUint64(), 18446744073709550000ULL, 1616, 0,
                          "Uint64 overflow when adding 18446744073709550000 and 1616.\n");
