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

  ErrOrValue SyncEvalBinaryOperator(const ExprValue& left, ExprTokenType op,
                                    const ExprValue& right) {
    ExprToken token(op, "", 0);

    bool needs_quit = false;
    bool called = false;
    ErrOrValue result((ExprValue()));
    EvalBinaryOperator(eval_context(), left, token, right,
                       [&needs_quit, &called, &result](ErrOrValue value) {
                         called = true;
                         result = value;
                         if (needs_quit)
                           debug_ipc::MessageLoop::Current()->QuitNow();
                       });
    if (!called) {
      // Needs async completion.
      needs_quit = true;
      loop().Run();
    }
    return result;
  }

  ErrOrValue SyncEvalUnaryOperator(ExprTokenType op, const ExprValue& right) {
    ExprToken token(op, "", 0);

    bool needs_quit = false;
    bool called = false;
    ErrOrValue result((ExprValue()));
    EvalUnaryOperator(token, right, [&needs_quit, &called, &result](ErrOrValue value) {
      called = true;
      result = value;
      if (needs_quit)
        debug_ipc::MessageLoop::Current()->QuitNow();
    });
    if (!called) {
      // Needs async completion.
      needs_quit = true;
      loop().Run();
    }
    return result;
  }

  template <typename T>
  void DoUnaryMinusTest(T in) {
    ExprValue original(in);

    ErrOrValue out = SyncEvalUnaryOperator(ExprTokenType::kMinus, original);
    ASSERT_TRUE(out.ok()) << out.err().msg();

    // This checked that the type conversions have followed C rules. This is the expected value
    // (int/unsigned unchanged, everything smaller than an int is promoted to an int, everything
    // larger remains unchanged).
    auto expected = -in;

    // The type of the output should be the same as the input for unary '-'.
    // TODO(brettw) the actual type pointer should be the same.
    EXPECT_EQ(sizeof(expected), out.value().data().size());
    if (std::is_unsigned<decltype(expected)>::value) {
      EXPECT_EQ(BaseType::kBaseTypeUnsigned, out.value().GetBaseType());
    } else {
      EXPECT_EQ(BaseType::kBaseTypeSigned, out.value().GetBaseType());
    }
    EXPECT_EQ(expected, out.value().GetAs<decltype(expected)>());
  }

  template <typename T>
  void DoUnaryMinusTypeTest() {
    DoUnaryMinusTest<T>(0);
    DoUnaryMinusTest<T>(std::numeric_limits<T>::max());
    DoUnaryMinusTest<T>(std::numeric_limits<T>::lowest());
  }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
};

}  // namespace

TEST_F(EvalOperators, Assignment) {
  auto int32_type = MakeInt32Type();

  // The casting test provides most tests for conversions so this test just
  // checks that the correct values are written and returned.
  constexpr uint64_t kAddress = 0x98723461923;
  ExprValue dest(int32_type, {0, 0, 0, 0}, ExprValueSource(kAddress));

  std::vector<uint8_t> data{0x12, 0x34, 0x56, 0x78};
  ExprValue source(int32_type, data, ExprValueSource());

  ErrOrValue out = SyncEvalBinaryOperator(dest, ExprTokenType::kEquals, source);

  // Written value returned.
  ASSERT_FALSE(out.has_error());
  EXPECT_EQ(source, out.value());

  // Memory written to target.
  auto mem_writes = eval_context()->data_provider()->GetMemoryWrites();
  ASSERT_EQ(1u, mem_writes.size());
  EXPECT_EQ(kAddress, mem_writes[0].first);
  EXPECT_EQ(data, mem_writes[0].second);
}

TEST_F(EvalOperators, IntArithmetic) {
  // Simple signed arithmatic of 32-bit types. We promote all math results to 64-bit.
  ErrOrValue out = SyncEvalBinaryOperator(ExprValue(static_cast<int32_t>(12)), ExprTokenType::kPlus,
                                          ExprValue(static_cast<int32_t>(-1)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(11, out.value().GetAs<int64_t>());

  // Type promotion to larger size. This uses a custom 64-bit int type so we can tell it's been
  // preserved. This is "127 + (-2)"
  auto weird_64 = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 8, "Weird64");
  out =
      SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(0x7f)), ExprTokenType::kPlus,
                             ExprValue(weird_64, {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(125, out.value().GetAs<int64_t>());
  EXPECT_EQ(weird_64.get(), out.value().type());

  // Promotion to unsigned when sizes match.
  auto int32_type = MakeInt32Type();
  auto uint32_type = MakeUint32Type();
  out = SyncEvalBinaryOperator(ExprValue(int32_type, {1, 0, 0, 0}), ExprTokenType::kPlus,
                               ExprValue(uint32_type, {2, 0, 0, 0}));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(3, out.value().GetAs<int64_t>());
  EXPECT_EQ(BaseType::kBaseTypeUnsigned, out.value().type()->AsBaseType()->base_type());

  // Signed subtraction.
  out = SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(100)), ExprTokenType::kMinus,
                               ExprValue(static_cast<int8_t>(-100)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(200, out.value().GetAs<int64_t>());

  // Overflow of input type with multiplication.
  out = SyncEvalBinaryOperator(ExprValue(static_cast<int8_t>(100)), ExprTokenType::kStar,
                               ExprValue(static_cast<int8_t>(100)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(10000, out.value().GetAs<int64_t>());

  // Boundary condition, should promote to unsigned 64-bit and do the multiplication.
  out = SyncEvalBinaryOperator(ExprValue(static_cast<uint32_t>(0xffffffff)), ExprTokenType::kStar,
                               ExprValue(static_cast<uint32_t>(0xffffffff)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0xfffffffe00000001, out.value().GetAs<uint64_t>());

  // Signed integer division.
  out = SyncEvalBinaryOperator(ExprValue(100), ExprTokenType::kSlash, ExprValue(-12));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(-8, out.value().GetAs<int64_t>());

  // Unsigned integer division. "100 / (unsigned)-12" does give 0.
  out = SyncEvalBinaryOperator(ExprValue(100), ExprTokenType::kSlash,
                               ExprValue(static_cast<unsigned>(-12)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0, out.value().GetAs<int64_t>());

  // Modulo.
  out = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kPercent,
                               ExprValue(static_cast<unsigned>(100)));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(8, out.value().GetAs<int64_t>());

  // Division by 0.
  out = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kSlash, ExprValue(0));
  EXPECT_TRUE(out.has_error());
  EXPECT_EQ("Division by 0.", out.err().msg());

  // Modulo by 0.
  out = SyncEvalBinaryOperator(ExprValue(108), ExprTokenType::kPercent, ExprValue(0));
  EXPECT_TRUE(out.has_error());
  EXPECT_EQ("Division by 0.", out.err().msg());

  // Bitwise |
  out = SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kBitwiseOr, ExprValue(0b1100));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0b1100, out.value().GetAs<int64_t>());

  // Bitwise &
  out = SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kAmpersand, ExprValue(0b1100));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0b0100, out.value().GetAs<int64_t>());

  // ^
  out = SyncEvalBinaryOperator(ExprValue(0b0100), ExprTokenType::kCaret, ExprValue(0b1100));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0b1000, out.value().GetAs<int64_t>());
}

TEST_F(EvalOperators, FloatArithmetic) {
  // Double-precision division.
  ErrOrValue out = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kSlash, ExprValue(10.0));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(21.0 / 10.0, out.value().GetAs<double>());

  // Floating-point division.
  out = SyncEvalBinaryOperator(ExprValue(21.0f), ExprTokenType::kSlash, ExprValue(10.0f));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(21.0f / 10.0f, out.value().GetAs<float>());

  // Promotion from float to double.
  out = SyncEvalBinaryOperator(ExprValue(21.0f), ExprTokenType::kSlash, ExprValue(10.0));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(21.0 / 10.0, out.value().GetAs<double>());

  // Promotion from int to float.
  out = SyncEvalBinaryOperator(ExprValue(21), ExprTokenType::kSlash, ExprValue(10.0f));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(21.0f / 10.0f, out.value().GetAs<float>());

  // Division by 0.
  out = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kSlash, ExprValue(0.0));
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(21.0 / 0.0, out.value().GetAs<double>());  // Should be "inf".

  // Modulo is an error.
  out = SyncEvalBinaryOperator(ExprValue(21.0), ExprTokenType::kPercent, ExprValue(5));
  EXPECT_TRUE(out.has_error());
  // Note: empty '' is because the test infrastructure doesn't set up a "value" for the token is
  // passes in. In real life it will be '%'.
  EXPECT_EQ("Operator '' not defined for floating point.", out.err().msg());
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
  ErrOrValue out = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kPlus, eight);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(kPtrVal1 + (8 * sizeof(int32_t)), out.value().GetAs<uint64_t>());

  // 8 + int32_ptr.
  out = SyncEvalBinaryOperator(eight, ExprTokenType::kPlus, int32_ptr);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(kPtrVal1 + (8 * sizeof(int32_t)), out.value().GetAs<uint64_t>());

  // int32_ptr - 8.
  out = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, eight);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(kPtrVal1 - (8 * sizeof(int32_t)), out.value().GetAs<uint64_t>());

  // 8 - int32_ptr -> Error.
  out = SyncEvalBinaryOperator(eight, ExprTokenType::kMinus, int32_ptr);
  EXPECT_TRUE(out.has_error());

  // int32_ptr - int32_ptr2.
  constexpr uint64_t kPtrVal2 = 0x120000;
  ExprValue int32_ptr2(kPtrVal2, int32_ptr_type);
  out = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, int32_ptr2);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  int64_t ptr1_2_diff = static_cast<int64_t>((kPtrVal1 - kPtrVal2) / sizeof(int32_t));
  EXPECT_EQ(ptr1_2_diff, out.value().GetAs<int64_t>());

  // int32_ptr2 - int32_ptr.
  out = SyncEvalBinaryOperator(int32_ptr2, ExprTokenType::kMinus, int32_ptr);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(-ptr1_2_diff, out.value().GetAs<int64_t>());

  // int32_ptr * 8 -> Error.
  out = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kStar, eight);
  EXPECT_TRUE(out.has_error());

  // int32_ptr2 + int32_ptr -> error;
  out = SyncEvalBinaryOperator(int32_ptr2, ExprTokenType::kPlus, int32_ptr);
  EXPECT_TRUE(out.has_error());

  // int32_ptr - int64_ptr -> Error.
  constexpr uint64_t kPtrVal3 = 0x9900;
  ExprValue int64_ptr(kPtrVal3, int64_ptr_type);
  out = SyncEvalBinaryOperator(int32_ptr, ExprTokenType::kMinus, int64_ptr);
  ASSERT_TRUE(out.has_error());
  EXPECT_EQ("Can't subtract pointers of different types 'int32_t*' and 'int64_t*'.",
            out.err().msg());

  // Two pointers near overflow.
  constexpr uint64_t kLargePtr1 = 0xffffffffffffff00;
  ExprValue large_ptr1(kLargePtr1, int32_ptr_type);
  constexpr uint64_t kLargePtr2 = 0xffffffffffffff80;
  ExprValue large_ptr2(kLargePtr2, int32_ptr_type);

  // large_ptr1 - large_ptr2.
  out = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kMinus, large_ptr2);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ((-0x80) / static_cast<int>(sizeof(int32_t)), out.value().GetAs<int64_t>());

  // large_ptr2 - large_ptr1.
  out = SyncEvalBinaryOperator(large_ptr2, ExprTokenType::kMinus, large_ptr1);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0x80 / static_cast<int>(sizeof(int32_t)), out.value().GetAs<int64_t>());

  // large_ptr1 + 8.
  out = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kPlus, eight);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(kLargePtr1 + (8 * sizeof(int32_t)), out.value().GetAs<uint64_t>());

  // Wraparound of 64-bit pointer addition. This threshold will force 0xffffffffffffff00 to wrap
  // when doing int32_t operations.
  ExprValue threshold(static_cast<int>(0x100 / sizeof(uint32_t)));
  out = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kPlus, threshold);
  ASSERT_FALSE(out.has_error()) << out.err().msg();
  EXPECT_EQ(0u, out.value().GetAs<uint64_t>());

  // Try | which should fail on pointers.
  out = SyncEvalBinaryOperator(large_ptr1, ExprTokenType::kBitwiseOr, eight);
  ASSERT_TRUE(out.has_error());
}

TEST_F(EvalOperators, UnaryMinus) {
  // Test the limits of all built-in types.
  DoUnaryMinusTypeTest<int8_t>();
  DoUnaryMinusTypeTest<uint8_t>();
  DoUnaryMinusTypeTest<int16_t>();
  DoUnaryMinusTypeTest<uint16_t>();
  DoUnaryMinusTypeTest<int32_t>();
  DoUnaryMinusTypeTest<uint32_t>();
  DoUnaryMinusTypeTest<int64_t>();
  DoUnaryMinusTypeTest<uint64_t>();

  // Try an unsupported value (a 3-byte signed). This should throw an error and
  // compute an empty value.
  ExprValue original(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 3, "uint24_t"),
                     {0, 0, 0});
  ErrOrValue out = SyncEvalUnaryOperator(ExprTokenType::kMinus, original);
  ASSERT_TRUE(out.err().has_error());
  // Note: in real life the operator string will be inside the '' but the test harness doesn't
  // set the actual operator text.
  EXPECT_EQ("Unsupported size for unary operator ''.", out.err().msg());
}

TEST_F(EvalOperators, UnaryBang) {
  // Nonzero char -> false.
  ErrOrValue out = SyncEvalUnaryOperator(ExprTokenType::kBang, ExprValue('a'));
  ASSERT_TRUE(out.ok());
  ASSERT_EQ(1u, out.value().data().size());
  EXPECT_EQ(0, out.value().GetAs<uint8_t>());
  EXPECT_EQ("bool", out.value().type()->GetFullName());

  // !0 in 64-bit = true.
  out = SyncEvalUnaryOperator(ExprTokenType::kBang, ExprValue(static_cast<uint64_t>(0)));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());

  // Pointer.
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, MakeInt32Type());
  out = SyncEvalUnaryOperator(ExprTokenType::kBang, ExprValue(ptr_type, {1, 2, 3, 4, 5, 6, 7, 8}));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(0, out.value().GetAs<uint8_t>());

  // Double.
  out = SyncEvalUnaryOperator(ExprTokenType::kBang, ExprValue(0.0));
  ASSERT_TRUE(out.ok());
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());

  // Try one that's not a number.
  auto coll = MakeCollectionType(DwarfTag::kStructureType, "Struct", {});
  out = SyncEvalUnaryOperator(ExprTokenType::kBang, ExprValue(coll, {}));
  ASSERT_TRUE(out.has_error());
  EXPECT_EQ("Invalid non-numeric type 'Struct' for operator.", out.err().msg());
}

TEST_F(EvalOperators, Comparison) {
  // (int8_t)1 == (int)1
  ExprValue char_one(static_cast<int8_t>(1));
  EXPECT_EQ(1u, char_one.data().size());  // Validate construction.
  ExprValue int_one(static_cast<int32_t>(1));
  ErrOrValue out = SyncEvalBinaryOperator(char_one, ExprTokenType::kEquality, int_one);
  ASSERT_TRUE(out.ok());
  ASSERT_EQ(1u, out.value().data().size());
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());
  EXPECT_EQ("bool", out.value().type()->GetFullName());

  // (int)1 != (int8_t)1
  out = SyncEvalBinaryOperator(char_one, ExprTokenType::kInequality, int_one);
  EXPECT_FALSE(out.value().GetAs<uint8_t>());

  // 1.0 <= 1
  ExprValue double_one(1.0);
  out = SyncEvalBinaryOperator(double_one, ExprTokenType::kLessEqual, int_one);
  EXPECT_TRUE(out.value().GetAs<uint8_t>());

  // 1.0 < 1
  out = SyncEvalBinaryOperator(double_one, ExprTokenType::kLess, int_one);
  EXPECT_FALSE(out.value().GetAs<uint8_t>());

  // 0 > 1.0
  ExprValue int_zero(0);
  out = SyncEvalBinaryOperator(int_zero, ExprTokenType::kGreater, double_one);
  EXPECT_FALSE(out.value().GetAs<uint8_t>());

  // 0 >= 1.0
  out = SyncEvalBinaryOperator(int_zero, ExprTokenType::kGreaterEqual, double_one);
  EXPECT_FALSE(out.value().GetAs<uint8_t>());

  // 1 >= 1.0
  out = SyncEvalBinaryOperator(int_one, ExprTokenType::kGreaterEqual, double_one);
  EXPECT_TRUE(out.value().GetAs<uint8_t>());

  // true > 0
  ExprValue true_value(true);
  out = SyncEvalBinaryOperator(true_value, ExprTokenType::kGreater, int_zero);
  EXPECT_TRUE(out.value().GetAs<uint8_t>());

  // 0 <=> 1 is recognised but an error.
  out = SyncEvalBinaryOperator(int_zero, ExprTokenType::kSpaceship, int_one);
  ASSERT_FALSE(out.ok());
}

TEST_F(EvalOperators, Logical) {
  // (int8_t)1 || (int)1
  ExprValue char_one(static_cast<int8_t>(1));
  ExprValue int_one(static_cast<int32_t>(1));
  ErrOrValue out = SyncEvalBinaryOperator(char_one, ExprTokenType::kLogicalOr, int_one);
  ASSERT_TRUE(out.ok());
  ASSERT_EQ(1u, out.value().data().size());
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());
  EXPECT_EQ("bool", out.value().type()->GetFullName());

  // 1 || 0
  ExprValue int_zero(0);
  out = SyncEvalBinaryOperator(int_one, ExprTokenType::kLogicalOr, int_zero);
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());

  // 0 || 0
  out = SyncEvalBinaryOperator(int_zero, ExprTokenType::kLogicalOr, int_zero);
  EXPECT_EQ(0, out.value().GetAs<uint8_t>());

  // 1 && 1
  out = SyncEvalBinaryOperator(int_one, ExprTokenType::kDoubleAnd, int_one);
  EXPECT_EQ(1, out.value().GetAs<uint8_t>());

  // 0 && 1
  out = SyncEvalBinaryOperator(int_zero, ExprTokenType::kDoubleAnd, int_one);
  EXPECT_EQ(0, out.value().GetAs<uint8_t>());
}

// Tests that && and || don't evaluate the right-hand side if not necessary.
TEST_F(EvalOperators, LogicalShortCircuit) {
  // 1 || <error>
  auto or_node = fxl::MakeRefCounted<BinaryOpExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, ExprValue(1)),
      ExprToken(ExprTokenType::kLogicalOr, "||", 0),
      fxl::MakeRefCounted<MockExprNode>(true, Err("Should not eval.")));

  // Should evalutate to true and not error.
  bool called = false;
  or_node->Eval(eval_context(), [&called](const Err& e, ExprValue v) {
    called = true;
    EXPECT_FALSE(e.has_error());
    EXPECT_EQ(1, v.GetAs<uint8_t>());
  });
  EXPECT_TRUE(called);  // Should eval synchronously.

  // 0 && <error>
  auto and_node = fxl::MakeRefCounted<BinaryOpExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, ExprValue(0)),
      ExprToken(ExprTokenType::kDoubleAnd, "&&", 0),
      fxl::MakeRefCounted<MockExprNode>(true, Err("Should not eval.")));

  // Should evalutate to true and not error.
  called = false;
  and_node->Eval(eval_context(), [&called](const Err& e, ExprValue v) {
    called = true;
    EXPECT_FALSE(e.has_error());
    EXPECT_EQ(0, v.GetAs<uint8_t>());
  });
  EXPECT_TRUE(called);  // Should eval synchronously.
}

}  // namespace zxdb
