// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/vm_op.h"
#include "src/developer/debug/zxdb/expr/vm_stream.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ExprTest : public TestWithLoop {
 public:
  ErrOrValue Eval(const std::string& code, const fxl::RefPtr<EvalContext>& context) {
    ErrOrValue result(Err("Uncalled"));
    bool called = false;
    EvalExpression(code, context, true, [&](ErrOrValue in_result) {
      result = in_result;
      called = true;
      debug::MessageLoop::Current()->QuitNow();
    });

    if (!called)
      loop().RunUntilNoTasks();
    EXPECT_TRUE(called);

    return result;
  }
};

}  // namespace

TEST_F(ExprTest, ValueToAddressAndSize) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Ints are OK but have no size.
  uint64_t address = 0;
  std::optional<uint32_t> size;
  ASSERT_TRUE(ValueToAddressAndSize(eval_context, ExprValue(23), &address, &size).ok());
  EXPECT_EQ(23u, address);
  EXPECT_EQ(std::nullopt, size);

  // Structure.
  auto uint64_type = MakeUint64Type();
  auto collection =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", uint64_type}, {"b", uint64_type}});
  std::vector<uint8_t> collection_data;
  collection_data.resize(collection->byte_size());

  // Currently evaluating a structure is expected to fail.
  // TODO(bug 44074) support non-pointer values and take their address implicitly.
  address = 0;
  size = std::nullopt;
  Err err = ValueToAddressAndSize(
      eval_context, ExprValue(collection, collection_data, ExprValueSource(0x12345678)), &address,
      &size);
  ASSERT_TRUE(err.has_error());
  EXPECT_EQ("Can't convert 'Foo' to an address.", err.msg());
  EXPECT_EQ(0u, address);
  EXPECT_EQ(std::nullopt, size);

  // Pointer to a collection.
  auto collection_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, collection);
  std::vector<uint8_t> ptr_data{8, 7, 6, 5, 4, 3, 2, 1};

  address = 0;
  size = std::nullopt;
  err = ValueToAddressAndSize(eval_context, ExprValue(collection_ptr, ptr_data), &address, &size);
  ASSERT_TRUE(err.ok());
  EXPECT_EQ(0x0102030405060708u, address);
  ASSERT_TRUE(size);
  EXPECT_EQ(collection->byte_size(), *size);
}

TEST_F(ExprTest, CConditions) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // If true condition executed.
  auto result = Eval("if (5 > 0) { 6; } else { 7; }", eval_context);
  EXPECT_TRUE(result.ok());
  int64_t result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(6, result_value);

  // Else condition executed.
  result = Eval("if (5 < 0) { 6; } else { 7; }", eval_context);
  EXPECT_TRUE(result.ok());
  result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(7, result_value);

  // Cascading if/else, execute the middle condition.
  result = Eval("if (5 < 0) { 6; } else if (0 < 5) { 99; } else { 7; }", eval_context);
  EXPECT_TRUE(result.ok());
  result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(99, result_value);
}

TEST_F(ExprTest, RustConditions) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  eval_context->set_language(ExprLanguage::kRust);

  // If true condition executed.
  auto result = Eval("if 5 > 0 { 6 } else { 7 }", eval_context);
  EXPECT_TRUE(result.ok());
  int64_t result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(6, result_value);

  // Else condition executed.
  result = Eval("if 5 < 0 { 6 } else { 7 }", eval_context);
  EXPECT_TRUE(result.ok());
  result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(7, result_value);

  // Cascading if/else, execute the middle condition.
  result = Eval("if 5 < 0 { 6 } else if 0 < 5 { 99 } else { 7 }", eval_context);
  EXPECT_TRUE(result.ok());
  result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(99, result_value);
}

// Tests short-circuiting behavior of the || and && operators.
//
// This test takes advantage of our lazy evaluation where we don't do name lookups until the code
// actually executes. We can therefore tell if the condition was executed by whether it encountered
// a name lookup error or not.
TEST_F(ExprTest, LogicalOrShortCircuit) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  ExprValue true_value(true);
  ExprValue false_value(false);

  auto result = Eval("1 || nonexistant", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(true_value, result.value());

  result = Eval("0 || nonexistant", eval_context);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ("MockEvalContext::GetVariableValue 'nonexistant' not found.", result.err().msg());

  result = Eval("0 || 1", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(true_value, result.value());

  result = Eval("0 || 0", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(false_value, result.value());

  // Check that condition in a real "if" statement.
  result = Eval("if (1 || nonexistant) { 5; } else { 6; }", eval_context);
  EXPECT_TRUE(result.ok());
  int64_t result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(5, result_value);
}

// See "LogicalOrShortCircuit" above.
TEST_F(ExprTest, LogicalAndShortCircuit) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  ExprValue true_value(true);
  ExprValue false_value(false);

  auto result = Eval("0 && nonexistant", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(false_value, result.value());

  result = Eval("1 && nonexistant", eval_context);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ("MockEvalContext::GetVariableValue 'nonexistant' not found.", result.err().msg());

  result = Eval("1 && 99", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(true_value, result.value());

  result = Eval("1 && 0", eval_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(false_value, result.value());

  // Check that condition in a real "if" statement.
  result = Eval("if (0 && nonexistant) { 5; } else { 6; }", eval_context);
  EXPECT_TRUE(result.ok());
  int64_t result_value = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result_value).ok());
  EXPECT_EQ(6, result_value);
}

TEST_F(ExprTest, CLocalVars) {
  const char kCode[] = R"(
  {
    int source = 45;
    auto sum(source - 3);
    sum = sum * 2;
    sum;  // The result of the program (since everything is an expression).
  }
  )";

  bool called = false;
  EvalExpression(kCode, fxl::MakeRefCounted<MockEvalContext>(), false, [&](ErrOrValue result) {
    called = true;
    ASSERT_TRUE(result.ok()) << result.err().msg();
    // (45 - 3) * 2 = 84
    // The expression system likes to promote internally to C-style int64 to avoid overflows.
    EXPECT_EQ(result.value(),
              ExprValue(static_cast<int64_t>(84), GetBuiltinType(ExprLanguage::kC, "int64_t")));
  });
  EXPECT_TRUE(called);
}

TEST_F(ExprTest, RustLocalVars) {
  const char kCode[] = R"(
  {
    let source:i32;
    source = 45;
    let sum = source - 3;
    sum = sum * 2;
    sum;  // The result of the program (since everything is an expression).
  }
  )";

  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  eval_context->set_language(ExprLanguage::kRust);

  bool called = false;
  EvalExpression(kCode, eval_context, false, [&](ErrOrValue result) {
    called = true;
    ASSERT_TRUE(result.ok()) << result.err().msg();
    // (45 - 3) * 2 = 84
    // The expression system likes to promote to int64 to avoid overflows (in contrast to C).
    EXPECT_EQ(result.value(),
              ExprValue(static_cast<int64_t>(84), GetBuiltinType(ExprLanguage::kC, "int64_t")));
  });
  EXPECT_TRUE(called);
}

TEST_F(ExprTest, CForLoop) {
  const char kCode[] = R"(
  {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
      sum = sum + i;
    }
    sum;  // The result of the program (since everything is an expression).
  }
  )";

  bool called = false;
  EvalExpression(kCode, fxl::MakeRefCounted<MockEvalContext>(), false, [&](ErrOrValue result) {
    called = true;
    ASSERT_TRUE(result.ok()) << result.err().msg();
    // 0+1+2+3+4+5+6+7+8+9 = 45
    EXPECT_EQ(result.value(), ExprValue(45, GetBuiltinType(ExprLanguage::kC, "int")));
  });
  EXPECT_TRUE(called);

  // Check the bytecode. This should be relatively stable.
  ExprTokenizer tokenizer(kCode, ExprLanguage::kC);
  ASSERT_TRUE(tokenizer.Tokenize());
  ExprParser parser(tokenizer.TakeTokens(), tokenizer.language(),
                    fxl::MakeRefCounted<MockEvalContext>());
  auto node = parser.ParseStandaloneExpression();
  ASSERT_TRUE(node);

  VmStream stream;
  node->EmitBytecode(stream);
  // clang-format off
  EXPECT_EQ(
      // "int sum = 0"
      "0: Literal(int(0))\n"    // Literal for "sum" initialization.
      "1: AsyncCallback1()\n"   // Cast to "int" (strictly unnecessary here).
      "2: Dup()\n"              // Make a copy to save as the local.
      "3: SetLocal(0)\n"        // Save the 0 to local var slot 0 (the "sum" variable").
      "4: Drop()\n"             // Discard the result of the declaration.

      // Set up break destination.
      "5: PushBreak(34)\n"      // "break" ops jump to the given address with the stack restored.

      // "int i = 0" (same as the above except for "i" in slot 1).
      "6: Literal(int(0))\n"
      "7: AsyncCallback1()\n"
      "8: Dup()\n"
      "9: SetLocal(1)\n"
      "10: Drop()\n"

      // "i < 10"
      "11: GetLocal(1)\n"       // Get "i".
      "12: ExpandRef()\n"       // Make sure "i" isn't a reference (derefs the addr to its value).
      "13: Literal(int(10))\n"  // "10"
      "14: Binary(<)\n"
      "15: JumpIfFalse(33)\n"   // End of loop is the given address.

      // "sum = sum + i"
      "16: GetLocal(0)\n"       // "sum" (for the left-side of the assignment).
      "17: ExpandRef()\n"
      "18: GetLocal(0)\n"       // "sum" (for adding to "i").
      "19: ExpandRef()\n"
      "20: GetLocal(1)\n"       // "i"
      "21: Binary(+)\n"
      "22: Binary(=)\n"
      "23: Drop()\n"            // Discard the result of the assignment expression.

      // "i = i + 1"
      "24: GetLocal(1)\n"       // "i" (for left side of assignment).
      "25: ExpandRef()\n"
      "26: GetLocal(1)\n"       // "i" (for adding to 1).
      "27: ExpandRef()\n"
      "28: Literal(int(1))\n"   // "1"
      "29: Binary(+)\n"
      "30: Binary(=)\n"
      "31: Drop()\n"            // Discard the result of the increment expression.

      // Loop back to the precondition on the given line.
      "32: Jump(11)\n"

      // Loop end cleanup.
      "33: PopLocals(1)\n"      // Discard the "i" local variable, now only one ("sum") in scope.
      "34: PopBreak()\n"        // Restore previous break destination.
      "35: Literal({null ExprValue})\n"  // Result of loop expression (nothing).
      "36: Drop()\n"            // Discard the result of the loop expression.

      // "sum"
      "37: GetLocal(0)\n"

      // Clean up outer block state.
      "38: PopLocals(0)\n",     // Discard "sum" variable.
      VmStreamToString(stream));
  // clang-format on

  // Try a loop with a break statement.
  const char kCodeWithBreak[] = R"(
  {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
      sum = sum + i;
      if (i == 3)
        break;
    }
    sum;  // The result of the program (since everything is an expression).
  }
  )";

  called = false;
  EvalExpression(kCodeWithBreak, fxl::MakeRefCounted<MockEvalContext>(), false,
                 [&](ErrOrValue result) {
                   called = true;
                   ASSERT_TRUE(result.ok()) << result.err().msg();
                   // 0+1+2+3 = 6
                   EXPECT_EQ(result.value(), ExprValue(6, GetBuiltinType(ExprLanguage::kC, "int")));
                 });
  EXPECT_TRUE(called);
}

TEST_F(ExprTest, RustWhileLoop) {
  // This program computes the next power of 2 greater than 3000.
  const char kCode[] = R"(
  {
    let sum: i32 = 1;
    while sum < 3000 {
      sum = sum * 2
    }
    sum
  }
  )";

  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  eval_context->set_language(ExprLanguage::kRust);

  bool called = false;
  EvalExpression(kCode, eval_context, false, [&](ErrOrValue result) {
    called = true;
    ASSERT_TRUE(result.ok()) << result.err().msg();
    EXPECT_EQ(result.value(), ExprValue(4096, GetBuiltinType(ExprLanguage::kRust, "i32")));
  });

  EXPECT_TRUE(called);
}

TEST_F(ExprTest, BuiltinFunctionCall) {
  const char kCode[] = "1 + MyFunction(2, 3 * 4)";

  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  ParsedIdentifier ident(ParsedIdentifierComponent("MyFunction"));
  eval_context->AddBuiltinFunction(
      ident, [](const fxl::RefPtr<EvalContext>& eval_context, const std::vector<ExprValue>& params,
                EvalCallback cb) {
        // Validate we got the expected parameters,
        ASSERT_EQ(2u, params.size());

        int64_t value = 0;
        ASSERT_TRUE(params[0].PromoteTo64(&value).ok());
        EXPECT_EQ(2, value);

        ASSERT_TRUE(params[1].PromoteTo64(&value).ok());
        EXPECT_EQ(12, value);

        // This is the return value.
        cb(ExprValue(999));
      });

  bool called = false;
  EvalExpression(kCode, eval_context, false, [&](ErrOrValue result) {
    called = true;
    ASSERT_TRUE(result.ok()) << result.err().msg();

    int64_t value = 0;
    ASSERT_TRUE(result.value().PromoteTo64(&value).ok());
    EXPECT_EQ(1000, value);
  });

  EXPECT_TRUE(called);
}

}  // namespace zxdb
