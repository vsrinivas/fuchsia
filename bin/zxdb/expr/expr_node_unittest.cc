// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <type_traits>

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class TestEvalContext : public ExprEvalContext {
 public:
  TestEvalContext() = default;
  ~TestEvalContext() = default;

  void AddVariable(const std::string& name, ExprValue v) { values_[name] = v; }

  // ExprEvalContext implementation.
  void GetVariable(
      const std::string& name,
      std::function<void(const Err& err, ExprValue value)> cb) override {
    auto found = values_.find(name);
    if (found == values_.end())
      cb(Err("Not found"), ExprValue());
    else
      cb(Err(), found->second);
  }

 private:
  std::map<std::string, ExprValue> values_;
};

}  // namespace

TEST(ExprNode, EvalIdentifier) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue foo_expected(12);
  context->AddVariable("foo", foo_expected);

  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // This identifier should be found synchronously and returned.
  IdentifierExprNode good_identifier(
      ExprToken(ExprToken::Type::kName, "foo", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  good_identifier.Eval(eval_context, [&called, &out_err, &out_value](
                                         const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });

  // This should succeed synchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(foo_expected, out_value);

  // This identifier should be not found.
  IdentifierExprNode bad_identifier(
      ExprToken(ExprToken::Type::kName, "bar", 0));
  called = false;
  out_value = ExprValue();
  bad_identifier.Eval(eval_context, [&called, &out_err, &out_value](
                                        const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = ExprValue();  // value;
  });

  // It should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ(ExprValue(), out_value);
}

template <typename T>
void DoUnaryMinusTest(T in) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue foo_expected(in);
  context->AddVariable("foo", foo_expected);
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  auto identifier = std::make_unique<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));

  // Validate the value by itself. This also has the effect of checking the
  // ExprValue type-specific constructor.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  identifier->Eval(eval_context, [&called, &out_err, &out_value](
                                     const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });
  EXPECT_TRUE(called);
  EXPECT_EQ(sizeof(T), out_value.data().size());

  called = false;
  out_err = Err();
  out_value = ExprValue();

  // Apply a unary '-' to that value.
  UnaryOpExprNode unary(ExprToken(ExprToken::kMinus, "-", 0),
                        std::move(identifier));
  unary.Eval(eval_context,
             [&called, &out_err, &out_value](const Err& err, ExprValue value) {
               called = true;
               out_err = err;
               out_value = value;
             });

  // This checked that the type conversions have followed C rules. This is
  // the expected value (int/unsigned unchanged, everything smaller than an int
  // is promoted to an int, everything larger remains unchanged).
  auto expected = -in;

  // The type of the output should be the same as the input for unary '-'.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(sizeof(expected), out_value.data().size());
  if (std::is_unsigned<decltype(expected)>::value) {
    EXPECT_EQ(BaseType::kBaseTypeUnsigned, out_value.GetBaseType());
  } else {
    EXPECT_EQ(BaseType::kBaseTypeSigned, out_value.GetBaseType());
  }
  EXPECT_EQ(expected, out_value.GetAs<decltype(expected)>());
}

template <typename T>
void DoUnaryMinusTypeTest() {
  DoUnaryMinusTest<T>(0);
  DoUnaryMinusTest<T>(std::numeric_limits<T>::max());
  DoUnaryMinusTest<T>(std::numeric_limits<T>::lowest());
}

TEST(ExprNode, UnaryMinus) {
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
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue expected(ExprValue::CreateSyntheticBaseType(
                         BaseType::kBaseTypeUnsigned, "uint24_t", 3),
                     {0, 0, 0});
  context->AddVariable("foo", expected);
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  auto identifier = std::make_unique<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));
  UnaryOpExprNode unary(ExprToken(ExprToken::kMinus, "-", 0),
                        std::move(identifier));

  bool called = false;
  Err out_err;
  ExprValue out_value;
  unary.Eval(eval_context,
             [&called, &out_err, &out_value](const Err& err, ExprValue value) {
               called = true;
               out_err = err;
               out_value = value;
             });
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ("Negation for this value is not supported.", out_err.msg());
  EXPECT_EQ(ExprValue(), out_value);
}

}  // namespace zxdb
