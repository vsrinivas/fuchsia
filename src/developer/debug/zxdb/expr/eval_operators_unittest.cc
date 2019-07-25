// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_operators.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/mock_expr_node.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class EvalOperators : public TestWithLoop {
 public:
  EvalOperators() : eval_context_(fxl::MakeRefCounted<MockEvalContext>()) {}
  ~EvalOperators() = default;

  fxl::RefPtr<MockEvalContext>& eval_context() { return eval_context_; }

  Err SyncEvalBinaryOperator(const ExprValue& left, ExprTokenType op, const ExprValue& right,
                             ExprValue* result) {
    ExprToken token(op, "", 0);

    bool needs_quit = false;
    bool called = false;
    Err out_err;
    EvalBinaryOperator(eval_context(), left, token, right,
                       [&needs_quit, &called, &out_err, result](const Err& err, ExprValue value) {
                         called = true;
                         out_err = err;
                         *result = value;
                         if (needs_quit)
                           debug_ipc::MessageLoop::Current()->QuitNow();
                       });
    if (!called) {
      // Needs async completion.
      needs_quit = true;
      loop().Run();
    }
    return out_err;
  }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
};

void QuitNow() { debug_ipc::MessageLoop::Current()->QuitNow(); }

}  // namespace

TEST_F(EvalOperators, Assignment) {
  auto int32_type = MakeInt32Type();

  // The casting test provides most tests for conversions so this test just
  // checks that the correct values are written and returned.
  constexpr uint64_t kAddress = 0x98723461923;
  ExprValue dest(int32_type, {0, 0, 0, 0}, ExprValueSource(kAddress));
  auto dest_node = fxl::MakeRefCounted<MockExprNode>(false, dest);

  ExprToken assign(ExprTokenType::kEquals, "=", 0);

  std::vector<uint8_t> data{0x12, 0x34, 0x56, 0x78};
  ExprValue source(int32_type, data, ExprValueSource());
  auto source_node = fxl::MakeRefCounted<MockExprNode>(false, source);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  EvalBinaryOperator(eval_context(), dest_node, assign, source_node,
                     [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                       called = true;
                       out_err = err;
                       out_value = value;
                       QuitNow();
                     });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // Written value returned.
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(source, out_value);

  // Memory written to target.
  auto mem_writes = eval_context()->data_provider()->GetMemoryWrites();
  ASSERT_EQ(1u, mem_writes.size());
  EXPECT_EQ(kAddress, mem_writes[0].first);
  EXPECT_EQ(data, mem_writes[0].second);
}

TEST_F(EvalOperators, IntArithmetic) {
  // Simple signed arithmatic of 32-bit types. We promote all math results to 64-bit.
  ExprValue out;
  Err err = SyncEvalBinaryOperator(ExprValue(static_cast<int32_t>(12)), ExprTokenType::kPlus,
                                   ExprValue(static_cast<int32_t>(-1)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(11, out.GetAs<int64_t>());

  // Type promotion to larger size. This uses a custom 64-bit int type so we can tell it's been
  // preserved. This is "127 + (-2)"
  auto weird_64 = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "Weird64");
  SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(0x7f)), ExprTokenType::kPlus,
                         ExprValue(weird_64, {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
                         &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(125, out.GetAs<int64_t>());
  EXPECT_EQ(weird_64.get(), out.type());

  // Promotion to unsigned when sizes match.
  auto int32_type = MakeInt32Type();
  auto uint32_type = MakeUint32Type();
  SyncEvalBinaryOperator(ExprValue(int32_type, {1, 0, 0, 0}), ExprTokenType::kPlus,
                         ExprValue(uint32_type, {2, 0, 0, 0}), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(3, out.GetAs<int64_t>());
  EXPECT_EQ(BaseType::kBaseTypeUnsigned, out.type()->AsBaseType()->base_type());

  // Signed subtraction.
  err = SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(100)), ExprTokenType::kMinus,
                               ExprValue(static_cast<int8_t>(-100)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(200, out.GetAs<int64_t>());

  // Overflow of input type with multiplication.
  err = SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(100)), ExprTokenType::kStar,
                               ExprValue(static_cast<int8_t>(100)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(10000, out.GetAs<int64_t>());

  // Boundary condition, should promote to unsigned 64-bit and do the multiplication.
  err = SyncEvalBinaryOperator(ExprValue(static_cast<uint32_t>(0xffffffff)), ExprTokenType::kStar,
                               ExprValue(static_cast<uint32_t>(0xffffffff)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0xfffffffe00000001, out.GetAs<uint64_t>());

  // Signed integer division.
  err = SyncEvalBinaryOperator(ExprValue(100), ExprTokenType::kSlash, ExprValue(-12), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(-8, out.GetAs<int64_t>());

  // Unsigned integer division. "100 / (unsigned)-12" does give 0.
  err = SyncEvalBinaryOperator(ExprValue(100), ExprTokenType::kSlash,
                               ExprValue(static_cast<unsigned>(-12)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0, out.GetAs<int64_t>());

  // Modulo.
  err = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kPercent,
                               ExprValue(static_cast<unsigned>(100)), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(8, out.GetAs<int64_t>());

  // Division by 0.
  err = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kSlash, ExprValue(0), &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Division by 0.", err.msg());

  // Modulo by 0.
  err = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kPercent, ExprValue(0), &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Division by 0.", err.msg());

  // Bitwise |
  err =
      SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kBitwiseOr, ExprValue(0b1100), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0b1100, out.GetAs<int64_t>());

  // Bitwise &
  err =
      SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kAmpersand, ExprValue(0b1100), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0b0100, out.GetAs<int64_t>());

  // ^
  err = SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kCaret, ExprValue(0b1100), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0b1000, out.GetAs<int64_t>());
}

TEST_F(EvalOperators, FloatArithmetic) {
  // Double-precision division.
  ExprValue out;
  Err err = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kSlash, ExprValue(10.0), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(21.0 / 10.0, out.GetAs<double>());

  // Floating-point division.
  err = SyncEvalBinaryOperator(ExprValue(21.0f), ExprTokenType::kSlash, ExprValue(10.0f), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(21.0f / 10.0f, out.GetAs<float>());

  // Promotion from float to double.
  err = SyncEvalBinaryOperator(ExprValue(21.0f), ExprTokenType::kSlash, ExprValue(10.0), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(21.0 / 10.0, out.GetAs<double>());

  // Promotion from int to float.
  err = SyncEvalBinaryOperator(ExprValue(21), ExprTokenType::kSlash, ExprValue(10.0f), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(21.0f / 10.0f, out.GetAs<float>());

  // Division by 0.
  err = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kSlash, ExprValue(0.0), &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(21.0 / 0.0, out.GetAs<double>());  // Should be "inf".

  // Modulo is an error.
  err = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kPercent, ExprValue(5), &out);
  EXPECT_TRUE(err.has_error());
  // Note: empty '' is because the test infrastructure doesn't set up a "value" for the token is
  // passes in. In real life it will be '%'.
  EXPECT_EQ("Operator '' not defined for floating point.", err.msg());
}

TEST_F(EvalOperators, PointerArithmetic) {
  auto int32_type = MakeInt32Type();
  auto int32_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int32_type);

  auto int64_type = MakeInt64Type();
  auto int64_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int64_type);

  constexpr uint64_t kPtrVal1 = 0x123400;
  ExprValue int32_ptr(kPtrVal1, int32_ptr_type);
  ExprValue eight(8);

  // int32_ptr + 8.
  ExprValue out;
  Err err = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kPlus, eight, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(kPtrVal1 + (8 * sizeof(int32_t)), out.GetAs<uint64_t>());

  // 8 + int32_ptr.
  err = SyncEvalBinaryOperator(eight, ExprTokenType::kPlus, int32_ptr, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(kPtrVal1 + (8 * sizeof(int32_t)), out.GetAs<uint64_t>());

  // int32_ptr - 8.
  err = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, eight, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(kPtrVal1 - (8 * sizeof(int32_t)), out.GetAs<uint64_t>());

  // 8 - int32_ptr -> Error.
  err = SyncEvalBinaryOperator(eight, ExprTokenType::kMinus, int32_ptr, &out);
  EXPECT_TRUE(err.has_error());

  // int32_ptr - int32_ptr2.
  constexpr uint64_t kPtrVal2 = 0x120000;
  ExprValue int32_ptr2(kPtrVal2, int32_ptr_type);
  err = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, int32_ptr2, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  int64_t ptr1_2_diff = static_cast<int64_t>((kPtrVal1 - kPtrVal2) / sizeof(int32_t));
  EXPECT_EQ(ptr1_2_diff, out.GetAs<int64_t>());

  // int32_ptr2 - int32_ptr.
  err = SyncEvalBinaryOperator(int32_ptr2, ExprTokenType::kMinus, int32_ptr, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(-ptr1_2_diff, out.GetAs<int64_t>());

  // int32_ptr * 8 -> Error.
  err = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kStar, eight, &out);
  EXPECT_TRUE(err.has_error());

  // int32_ptr2 + int32_ptr -> error;
  err = SyncEvalBinaryOperator(int32_ptr2, ExprTokenType::kPlus, int32_ptr, &out);
  EXPECT_TRUE(err.has_error());

  // int32_ptr - int64_ptr -> Error.
  constexpr uint64_t kPtrVal3 = 0x9900;
  ExprValue int64_ptr(kPtrVal3, int64_ptr_type);
  err = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, int64_ptr, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't subtract pointers of different types 'int32_t*' and 'int64_t*'.", err.msg());

  // Two pointers near overflow.
  constexpr uint64_t kLargePtr1 = 0xffffffffffffff00;
  ExprValue large_ptr1(kLargePtr1, int32_ptr_type);
  constexpr uint64_t kLargePtr2 = 0xffffffffffffff80;
  ExprValue large_ptr2(kLargePtr2, int32_ptr_type);

  // large_ptr1 - large_ptr2.
  err = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kMinus, large_ptr2, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ((-0x80) / static_cast<int>(sizeof(int32_t)), out.GetAs<int64_t>());

  // large_ptr2 - large_ptr1.
  err = SyncEvalBinaryOperator(large_ptr2, ExprTokenType::kMinus, large_ptr1, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0x80 / static_cast<int>(sizeof(int32_t)), out.GetAs<int64_t>());

  // large_ptr1 + 8.
  err = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kPlus, eight, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(kLargePtr1 + (8 * sizeof(int32_t)), out.GetAs<uint64_t>());

  // Wraparound of 64-bit pointer addition. This threshold will force 0xffffffffffffff00 to wrap
  // when doing int32_t operations.
  ExprValue threshold(static_cast<int>(0x100 / sizeof(uint32_t)));
  err = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kPlus, threshold, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(0u, out.GetAs<uint64_t>());

  // Try | which should fail on pointers.
  err = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kBitwiseOr, eight, &out);
  EXPECT_TRUE(err.has_error());
}

}  // namespace zxdb
