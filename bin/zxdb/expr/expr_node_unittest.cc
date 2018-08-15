// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <type_traits>

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
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
  void Dereference(
      const ExprValue& value,
      std::function<void(const Err& err, ExprValue value)> cb) override {}

 private:
  std::map<std::string, ExprValue> values_;
};

class ExprNodeTest : public testing::Test {
 public:
  ExprNodeTest() { loop_.Init(); }
  ~ExprNodeTest() { loop_.Cleanup(); }

  debug_ipc::MessageLoop& loop() { return loop_; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
};

}  // namespace

TEST_F(ExprNodeTest, EvalIdentifier) {
  auto context = fxl::MakeRefCounted<TestEvalContext>();
  ExprValue foo_expected(12);
  context->AddVariable("foo", foo_expected);

  // This identifier should be found synchronously and returned.
  IdentifierExprNode good_identifier(
      ExprToken(ExprToken::Type::kName, "foo", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  good_identifier.Eval(context, [&called, &out_err, &out_value](
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
  bad_identifier.Eval(context, [&called, &out_err, &out_value](
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

  auto identifier = std::make_unique<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));

  // Validate the value by itself. This also has the effect of checking the
  // ExprValue type-specific constructor.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  identifier->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                            ExprValue value) {
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
  unary.Eval(context,
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

TEST_F(ExprNodeTest, UnaryMinus) {
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
  ExprValue expected(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 3, "uint24_t"),
      {0, 0, 0});
  context->AddVariable("foo", expected);

  auto identifier = std::make_unique<IdentifierExprNode>(
      ExprToken(ExprToken::kName, "foo", 0));
  UnaryOpExprNode unary(ExprToken(ExprToken::kMinus, "-", 0),
                        std::move(identifier));

  bool called = false;
  Err out_err;
  ExprValue out_value;
  unary.Eval(context,
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

// This test mocks at the SymbolDataProdiver level because most of the
// dereference logic is in the SymbolEvalContext.
TEST_F(ExprNodeTest, DereferenceReference) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(), data_provider,
      fxl::RefPtr<CodeBlock>());

  // Dereferencing should remove the const on the pointer but not the pointee.
  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto const_base_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagConstType, LazySymbol(base_type));
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(const_base_type));
  auto const_ptr_type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagConstType,
                                                          LazySymbol(ptr_type));

  // The value being pointed to.
  constexpr uint32_t kValue = 0x12345678;
  constexpr uint64_t kAddress = 0x1020;
  data_provider->AddMemory(kAddress, {0x78, 0x56, 0x34, 0x12});

  // The pointer.
  ExprValue ptr_value(const_ptr_type,
                      {0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Execute the dereference.
  auto deref_node = std::make_unique<DereferenceExprNode>(
      std::make_unique<ConstantExprNode>(ptr_value));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  deref_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                            ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should complete asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // The type should be the const base type.
  EXPECT_EQ(const_base_type.get(), out_value.type());

  ASSERT_EQ(4u, out_value.data().size());
  EXPECT_EQ(kValue, out_value.GetAs<uint32_t>());

  // Now go backwards and get the address of the value.
  auto addr_node = std::make_unique<AddressOfExprNode>(
      std::make_unique<ConstantExprNode>(out_value));

  called = false;
  out_err = Err();
  out_value = ExprValue();
  addr_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                           ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });

  // Taking the address should always complete synchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // The value should be the address.
  ASSERT_EQ(8u, out_value.data().size());
  EXPECT_EQ(kAddress, out_value.GetAs<uint64_t>());

  // The type should be a pointer modifier on the old type. The pointer
  // modifier will be a dynamically created one so won't match the original we
  // made above, but the underlying "const int" should still match.
  const ModifiedType* out_mod_type = out_value.type()->AsModifiedType();
  ASSERT_TRUE(out_mod_type);
  EXPECT_EQ(Symbol::kTagPointerType, out_mod_type->tag());
  EXPECT_EQ(const_base_type.get(),
            out_mod_type->modified().Get()->AsModifiedType());
  EXPECT_EQ("const uint32_t*", out_mod_type->GetFullName());

  // Try to dereference an invalid address.
  ExprValue bad_ptr_value(const_ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  auto bad_deref_node = std::make_unique<DereferenceExprNode>(
      std::make_unique<ConstantExprNode>(bad_ptr_value));
  called = false;
  out_err = Err();
  out_value = ExprValue();
  bad_deref_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                            ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should complete asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ("MockSymbolDataProvider::GetMemoryAsync: Memory not found 0x0",
            out_err.msg());

}

}  // namespace zxdb
