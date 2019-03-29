// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <type_traits>

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_expr_eval_context.h"
#include "src/developer/debug/zxdb/expr/mock_expr_node.h"
#include "src/developer/debug/zxdb/expr/symbol_eval_context.h"
#include "src/developer/debug/zxdb/expr/symbol_variable_resolver.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ExprNodeTest : public TestWithLoop {};

}  // namespace

TEST_F(ExprNodeTest, EvalIdentifier) {
  auto context = fxl::MakeRefCounted<MockExprEvalContext>();
  ExprValue foo_expected(12);
  context->AddVariable("foo", foo_expected);

  // This identifier should be found synchronously and returned.
  auto good_identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprTokenType::kName, "foo", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  good_identifier->Eval(context, [&called, &out_err, &out_value](
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
  auto bad_identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprTokenType::kName, "bar", 0));
  called = false;
  out_value = ExprValue();
  bad_identifier->Eval(context, [&called, &out_err, &out_value](
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
  auto context = fxl::MakeRefCounted<MockExprEvalContext>();
  ExprValue foo_expected(in);
  context->AddVariable("foo", foo_expected);

  auto identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprTokenType::kName, "foo", 0));

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
  auto unary = fxl::MakeRefCounted<UnaryOpExprNode>(
      ExprToken(ExprTokenType::kMinus, "-", 0), std::move(identifier));
  unary->Eval(context,
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
  auto context = fxl::MakeRefCounted<MockExprEvalContext>();
  ExprValue expected(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 3, "uint24_t"),
      {0, 0, 0});
  context->AddVariable("foo", expected);

  auto identifier = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprTokenType::kName, "foo", 0));
  auto unary = fxl::MakeRefCounted<UnaryOpExprNode>(
      ExprToken(ExprTokenType::kMinus, "-", 0), std::move(identifier));

  bool called = false;
  Err out_err;
  ExprValue out_value;
  unary->Eval(context,
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

// This test mocks at the SymbolDataProvider level because most of the
// dereference logic is in the SymbolEvalContext.
TEST_F(ExprNodeTest, DereferenceReferencePointer) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), data_provider, nullptr);

  // Dereferencing should remove the const on the pointer but not the pointee.
  auto base_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto const_base_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kConstType, LazySymbol(base_type));
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(const_base_type));
  auto const_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType,
                                                          LazySymbol(ptr_type));

  // The value being pointed to.
  constexpr uint32_t kValue = 0x12345678;
  constexpr uint64_t kAddress = 0x1020;
  data_provider->AddMemory(kAddress, {0x78, 0x56, 0x34, 0x12});

  // The pointer.
  ExprValue ptr_value(const_ptr_type,
                      {0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Execute the dereference.
  auto deref_node = fxl::MakeRefCounted<DereferenceExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, ptr_value));
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
  auto addr_node = fxl::MakeRefCounted<AddressOfExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, out_value));

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
  EXPECT_EQ(DwarfTag::kPointerType, out_mod_type->tag());
  EXPECT_EQ(const_base_type.get(),
            out_mod_type->modified().Get()->AsModifiedType());
  EXPECT_EQ("const uint32_t*", out_mod_type->GetFullName());

  // Try to dereference an invalid address.
  ExprValue bad_ptr_value(const_ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  auto bad_deref_node = fxl::MakeRefCounted<DereferenceExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, bad_ptr_value));
  called = false;
  out_err = Err();
  out_value = ExprValue();
  bad_deref_node->Eval(context, [&called, &out_err, &out_value](
                                    const Err& err, ExprValue value) {
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
  EXPECT_EQ("Invalid pointer 0x0", out_err.msg());
}

// This also tests ExprNode::EvalFollowReferences() by making the index a
// reference type.
TEST_F(ExprNodeTest, ArrayAccess) {
  // The base address of the array (of type uint32_t*).
  auto uint32_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto uint32_ptr_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(uint32_type));
  constexpr uint64_t kAddress = 0x12345678;
  ExprValue pointer_value(uint32_ptr_type,
                          {0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00});
  auto pointer_node = fxl::MakeRefCounted<MockExprNode>(false, pointer_value);

  // The index value (= 5) lives in memory as a 32-bit little-endian value.
  constexpr uint64_t kRefAddress = 0x5000;
  constexpr uint8_t kIndex = 5;
  auto context = fxl::MakeRefCounted<MockExprEvalContext>();
  context->data_provider()->AddMemory(kRefAddress, {kIndex, 0, 0, 0});

  // The index expression is a reference to the index we saved above, and the
  // reference data is the address.
  auto uint32_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kReferenceType, LazySymbol(uint32_type));
  auto index = fxl::MakeRefCounted<MockExprNode>(
      false, ExprValue(uint32_ref_type, {0, 0x50, 0, 0, 0, 0, 0, 0}));

  // The node to evaluate the access. Note the pointer are index nodes are
  // moved here so the source reference is gone. This allows us to test that
  // they stay in scope during an async call below.
  auto access = fxl::MakeRefCounted<ArrayAccessExprNode>(
      std::move(pointer_node), std::move(index));

  // We expect it to read @ kAddress[kIndex]. Insert a value there.
  constexpr uint64_t kExpectedAddr = kAddress + 4 * kIndex;
  constexpr uint32_t kExpectedValue = 0x11223344;
  context->data_provider()->AddMemory(kExpectedAddr, {0x44, 0x33, 0x22, 0x11});

  // Execute.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  access->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                        ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // The two parts of the expression were set as async above, so it should not
  // have been called yet.
  EXPECT_FALSE(called);

  // Clear out references to the stuff being executed. It should not crash, the
  // relevant data should remain alive.
  context.reset();
  access.reset();

  loop().Run();

  // Should have succeeded asynchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // Should have found our data at the right place.
  EXPECT_EQ(uint32_type.get(), out_value.type());
  EXPECT_EQ(kExpectedValue, out_value.GetAs<uint32_t>());
  EXPECT_EQ(kExpectedAddr, out_value.source().address());
}

// This is more of an integration smoke test for "." and "->". The details are
// tested in resolve_collection_unittest.cc.
TEST_F(ExprNodeTest, MemberAccess) {
  auto context = fxl::MakeRefCounted<MockExprEvalContext>();

  // Define a class.
  auto int32_type = MakeInt32Type();
  auto sc = MakeCollectionType(DwarfTag::kStructureType, "Foo",
                               {{"a", int32_type}, {"b", int32_type}});

  // Set up a call to do "." synchronously.
  auto struct_node = fxl::MakeRefCounted<MockExprNode>(
      true, ExprValue(sc, {0x78, 0x56, 0x34, 0x12}));
  auto access_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_node, ExprToken(ExprTokenType::kDot, ".", 0),
      Identifier(ExprToken(ExprTokenType::kName, "a", 0)));

  // Do the call.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  access_node->Eval(context, [&called, &out_err, &out_value](const Err& err,
                                                             ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should have run synchronously.
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(0x12345678, out_value.GetAs<int32_t>());

  // Test indirection: "foo->a".
  auto foo_ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol(sc));
  // Add memory in two chunks since the mock data provider can only respond
  // with the addresses it's given.
  constexpr uint64_t kAddress = 0x1000;
  context->data_provider()->AddMemory(kAddress, {0x44, 0x33, 0x22, 0x11});
  context->data_provider()->AddMemory(kAddress + 4, {0x88, 0x77, 0x66, 0x55});

  // Make this one evaluate the left-hand-size asynchronously. This value
  // references kAddress (little-endian).
  auto struct_ptr_node = fxl::MakeRefCounted<MockExprNode>(
      false, ExprValue(foo_ptr_type,
                       {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  auto access_ptr_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_ptr_node, ExprToken(ExprTokenType::kArrow, "->", 0),
      Identifier(ExprToken(ExprTokenType::kName, "b", 0)));

  // Do the call.
  called = false;
  out_err = Err();
  out_value = ExprValue();
  access_ptr_node->Eval(context, [&called, &out_err, &out_value](
                                     const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  // Should have run asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(sizeof(int32_t), out_value.data().size());
  EXPECT_EQ(0x55667788, out_value.GetAs<int32_t>());
}

}  // namespace zxdb
