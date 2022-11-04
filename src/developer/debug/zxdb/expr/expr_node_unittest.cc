// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_node.h"

#include <map>
#include <type_traits>

#include <gtest/gtest.h>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/abi_null.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/eval_test_support.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/mock_expr_node.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/expr/vm_exec.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

// NOTE: additional tests are in expr_unittest.cc which run from the source code (more complex
// structures are easier to express that way).

class ExprNodeTest : public TestWithLoop {
 public:
  ErrOrValue EvalAsBytecode(const fxl::RefPtr<ExprNode>& node,
                            const fxl::RefPtr<EvalContext>& context) {
    VmStream stream;
    node->EmitBytecode(stream);

    ErrOrValue result(Err("Unset"));
    bool called = false;
    VmExec(context, std::move(stream), [&](ErrOrValue in_result) {
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

// A PrettyType with a getter that returns a constant value.
class MockGetterPrettyType : public PrettyType {
 public:
  static const char kGetterName[];
  static const char kMemberName[];
  static const int kGetterValue;
  static const int kMemberValue;

  MockGetterPrettyType() : PrettyType({{kGetterName, "5"}}) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override {}

  EvalFunction GetMember(const std::string& member_name) const override {
    if (member_name == kMemberName) {
      return [](const fxl::RefPtr<EvalContext>&, const ExprValue& object_value, EvalCallback cb) {
        cb(ExprValue(kMemberValue));
      };
    }

    return EvalFunction();
  }
};
const char MockGetterPrettyType::kGetterName[] = "get5";
const char MockGetterPrettyType::kMemberName[] = "member";
const int MockGetterPrettyType::kGetterValue = 5;
const int MockGetterPrettyType::kMemberValue = 42;

// A PrettyType with a dereference function that returns a constant value.
class MockDerefPrettyType : public PrettyType {
 public:
  MockDerefPrettyType(ExprValue val) : PrettyType(), val_(std::move(val)) {}

  void Format(FormatNode* node, const FormatOptions& options,
              const fxl::RefPtr<EvalContext>& context, fit::deferred_callback cb) override {}
  EvalFunction GetDereferencer() const override {
    return [val = val_](const fxl::RefPtr<EvalContext>&, ExprValue, EvalCallback cb) { cb(val); };
  }

 private:
  ExprValue val_;
};

}  // namespace

TEST_F(ExprNodeTest, EvalIdentifier) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  ExprValue foo_expected(12);
  context->AddVariable("foo", foo_expected);

  // This identifier should be found synchronously and returned.
  auto good_identifier = fxl::MakeRefCounted<IdentifierExprNode>("foo");
  EXPECT_EQ(ErrOrValue(foo_expected), EvalAsBytecode(good_identifier, context));

  // This identifier should be not found.
  auto bad_identifier = fxl::MakeRefCounted<IdentifierExprNode>("bar");
  EXPECT_TRUE(EvalAsBytecode(bad_identifier, context).has_error());
}

TEST_F(ExprNodeTest, LiteralChar) {
  ExprToken char_token(ExprTokenType::kCharLiteral, "a", 0);
  auto char_node = fxl::MakeRefCounted<LiteralExprNode>(ExprLanguage::kC, char_token);

  // C character.
  auto c_context = fxl::MakeRefCounted<MockEvalContext>();
  c_context->set_language(ExprLanguage::kC);

  // Should be one byte = 'a'.
  ErrOrValue result = EvalAsBytecode(char_node, c_context);
  EXPECT_FALSE(result.has_error());
  EXPECT_EQ(1u, result.value().data().size());
  EXPECT_EQ('a', result.value().GetAs<int8_t>());

  // Rust character.
  auto rust_context = fxl::MakeRefCounted<MockEvalContext>();
  rust_context->set_language(ExprLanguage::kRust);

  // Should be four bytes.
  auto rust_char_node = fxl::MakeRefCounted<LiteralExprNode>(ExprLanguage::kRust, char_token);
  result = EvalAsBytecode(rust_char_node, rust_context);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(4u, result.value().data().size());
  EXPECT_EQ(static_cast<uint32_t>('a'), result.value().GetAs<uint32_t>());
}

// This test mocks at the SymbolDataProvider level because most of the dereference logic is in the
// EvalContextImpl.
TEST_F(ExprNodeTest, DereferenceReferencePointer) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto context = fxl::MakeRefCounted<EvalContextImpl>(std::make_shared<AbiNull>(),
                                                      fxl::WeakPtr<const ProcessSymbols>(),
                                                      data_provider, ExprLanguage::kC);

  // Dereferencing should remove the const on the pointer but not the pointee.
  auto base_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto const_base_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, base_type);
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, const_base_type);
  auto const_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, ptr_type);

  // The value being pointed to.
  constexpr uint32_t kValue = 0x12345678;
  constexpr uint64_t kAddress = 0x1020;
  data_provider->AddMemory(kAddress, {0x78, 0x56, 0x34, 0x12});

  // The pointer.
  ExprValue ptr_value(const_ptr_type, {0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

  // Execute the dereference.
  auto deref_node =
      fxl::MakeRefCounted<DereferenceExprNode>(fxl::MakeRefCounted<MockExprNode>(true, ptr_value));
  ErrOrValue result = EvalAsBytecode(deref_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(const_base_type.get(), result.value().type());
  ASSERT_EQ(4u, result.value().data().size());
  EXPECT_EQ(kValue, result.value().GetAs<uint32_t>());

  // Now go backwards and get the address of the value.
  auto addr_node = fxl::MakeRefCounted<AddressOfExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, result.value()));
  result = EvalAsBytecode(addr_node, context);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(8u, result.value().data().size());
  EXPECT_EQ(kAddress, result.value().GetAs<uint64_t>());

  // The type should be a pointer modifier on the old type. The pointer modifier will be a
  // dynamically created one so won't match the original we made above, but the underlying "const
  // int" should still match.
  const ModifiedType* out_mod_type = result.value().type()->As<ModifiedType>();
  ASSERT_TRUE(out_mod_type);
  EXPECT_EQ(DwarfTag::kPointerType, out_mod_type->tag());
  EXPECT_EQ(const_base_type.get(), out_mod_type->modified().Get()->As<ModifiedType>());
  EXPECT_EQ("const uint32_t*", out_mod_type->GetFullName());
}

TEST_F(ExprNodeTest, DereferenceErrors) {
  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto context = fxl::MakeRefCounted<EvalContextImpl>(std::make_shared<AbiNull>(),
                                                      fxl::WeakPtr<const ProcessSymbols>(),
                                                      data_provider, ExprLanguage::kC);

  auto base_type = MakeInt32Type();
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, base_type);

  // Try to dereference an invalid address.
  ExprValue bad_ptr_value(ptr_type, {0, 0, 0, 0, 0, 0, 0, 0});
  auto bad_deref_node = fxl::MakeRefCounted<DereferenceExprNode>(
      fxl::MakeRefCounted<MockExprNode>(true, bad_ptr_value));
  auto result = EvalAsBytecode(bad_deref_node, context);
  EXPECT_TRUE(result.has_error());
  EXPECT_EQ("Invalid pointer 0x0", result.err().msg());

  // Try to take the address of the invalid expression above. The error should be forwarded.
  auto addr_bad_deref_node = fxl::MakeRefCounted<AddressOfExprNode>(std::move(bad_deref_node));
  result = EvalAsBytecode(addr_bad_deref_node, context);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Invalid pointer 0x0", result.err().msg());

  // Dereference an undefined value.
  auto undef_node = fxl::MakeRefCounted<MockExprNode>(true, Err("Undefined"));
  auto undef_deref_node = fxl::MakeRefCounted<DereferenceExprNode>(std::move(undef_node));
  result = EvalAsBytecode(undef_deref_node, context);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Undefined", result.err().msg());
}

// This also tests ExprNode::EvalFollowReferences() by making the index a reference type.
TEST_F(ExprNodeTest, ArrayAccess) {
  // The base address of the array (of type uint32_t*).
  auto uint32_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 4, "uint32_t");
  auto uint32_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, uint32_type);
  constexpr uint64_t kAddress = 0x12345678;
  ExprValue pointer_value(uint32_ptr_type, {0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00});
  auto pointer_node = fxl::MakeRefCounted<MockExprNode>(false, pointer_value);

  // The index value (= 5) lives in memory as a 32-bit little-endian value.
  constexpr uint64_t kRefAddress = 0x5000;
  constexpr uint8_t kIndex = 5;
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->data_provider()->AddMemory(kRefAddress, {kIndex, 0, 0, 0});

  // The index expression is a reference to the index we saved above, and the
  // reference data is the address.
  auto uint32_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, uint32_type);
  auto index = fxl::MakeRefCounted<MockExprNode>(
      false, ExprValue(uint32_ref_type, {0, 0x50, 0, 0, 0, 0, 0, 0}));

  // The node to evaluate the access. Note the pointer are index nodes are moved here so the source
  // reference is gone. This allows us to test that they stay in scope during an async call below.
  auto access = fxl::MakeRefCounted<ArrayAccessExprNode>(std::move(pointer_node), std::move(index));

  // We expect it to read @ kAddress[kIndex]. Insert a value there.
  constexpr uint64_t kExpectedAddr = kAddress + 4 * kIndex;
  constexpr uint32_t kExpectedValue = 0x11223344;
  context->data_provider()->AddMemory(kExpectedAddr, {0x44, 0x33, 0x22, 0x11});

  auto result = EvalAsBytecode(access, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(uint32_type.get(), result.value().type());
  EXPECT_EQ(kExpectedValue, result.value().GetAs<uint32_t>());
  EXPECT_EQ(kExpectedAddr, result.value().source().address());
}

// This is more of an integration smoke test for "." and "->". The details are tested in
// resolve_collection_unittest.cc.
TEST_F(ExprNodeTest, MemberAccess) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Define a class.
  auto int32_type = MakeInt32Type();
  auto sc =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", int32_type}, {"b", int32_type}});

  // Set up a call to do "." synchronously.
  auto struct_node =
      fxl::MakeRefCounted<MockExprNode>(true, ExprValue(sc, {0x78, 0x56, 0x34, 0x12}));
  auto access_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_node, ExprToken(ExprTokenType::kDot, ".", 0), ParsedIdentifier("a"));

  // Do the call.
  auto result = EvalAsBytecode(access_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(0x12345678, result.value().GetAs<int32_t>());

  // Test indirection: "foo->a".
  auto foo_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, sc);
  // Add memory in two chunks since the mock data provider can only respond with the addresses it's
  // given.
  constexpr uint64_t kAddress = 0x1000;
  context->data_provider()->AddMemory(kAddress, {0x44, 0x33, 0x22, 0x11});
  context->data_provider()->AddMemory(kAddress + 4, {0x88, 0x77, 0x66, 0x55});

  // Make this one evaluate the left-hand-size asynchronously. This value references kAddress
  // (little-endian).
  auto struct_ptr_node = fxl::MakeRefCounted<MockExprNode>(
      false, ExprValue(foo_ptr_type, {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  auto access_ptr_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_ptr_node, ExprToken(ExprTokenType::kArrow, "->", 0), ParsedIdentifier("b"));

  // Do the call.
  result = EvalAsBytecode(access_ptr_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(sizeof(int32_t), result.value().data().size());
  EXPECT_EQ(0x55667788, result.value().GetAs<int32_t>());
}

// Tests that Rust references are autodereferenced by the . operator.
TEST_F(ExprNodeTest, RustMemberAccess) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  auto unit = fxl::MakeRefCounted<CompileUnit>(fxl::WeakPtr<ModuleSymbols>(), nullptr,
                                               DwarfLang::kRust, "module.so", std::nullopt);

  // Define a class.
  auto int32_type = MakeInt32Type();
  auto sc =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", int32_type}, {"b", int32_type}});
  SymbolTestParentSetter sc_parent(sc, unit);

  // Define a reference type.
  auto foo_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, sc);
  SymbolTestParentSetter foo_ptr_type_parent(foo_ptr_type, unit);
  foo_ptr_type->set_assigned_name("&Foo");
  // Add memory in two chunks since the mock data provider can only respond with the addresses it's
  // given.
  constexpr uint64_t kAddress = 0x1000;
  context->data_provider()->AddMemory(kAddress, {0x44, 0x33, 0x22, 0x11});
  context->data_provider()->AddMemory(kAddress + 4, {0x88, 0x77, 0x66, 0x55});

  // Make this one evaluate the left-hand-size asynchronously. This value references kAddress
  // (little-endian).
  auto struct_ptr_node = fxl::MakeRefCounted<MockExprNode>(
      false, ExprValue(foo_ptr_type, {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  auto access_ptr_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      struct_ptr_node, ExprToken(ExprTokenType::kDot, ".", 0), ParsedIdentifier("b"));

  // Do the call.
  auto result = EvalAsBytecode(access_ptr_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(0x55667788, result.value().GetAs<int32_t>());
}

// Tests dereferencing via "*" and "->" with a type that has a pretty type.
TEST_F(ExprNodeTest, PrettyDereference) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Make a struct to return, it has one 32-bit value.
  auto int32_type = MakeInt32Type();
  auto struct_type =
      MakeCollectionType(DwarfTag::kStructureType, "StructType", {{"a", int32_type}});
  constexpr uint8_t kAValue = 42;
  ExprValue struct_value(struct_type, {kAValue, 0, 0, 0});  // ReturnType.a = kAValue.

  // Register the PrettyType that provides a getter. It always returns struct_value.
  const char kTypeName[] = "MyType";
  IdentifierGlob glob;
  ASSERT_FALSE(glob.Init(kTypeName).has_error());
  context->pretty_type_manager().Add(ExprLanguage::kC, glob,
                                     std::make_unique<MockDerefPrettyType>(struct_value));

  // Value of MyType to pass to the evaluator. The contents of this don't matter, only the type
  // name will be matched.
  auto my_type = MakeCollectionType(DwarfTag::kStructureType, kTypeName, {});
  ExprValue my_value(my_type, {});
  auto my_node = fxl::MakeRefCounted<MockExprNode>(true, my_value);

  // Dereferencing MyType should yield the pretty type result |struct_type| above.
  auto deref_node = fxl::MakeRefCounted<DereferenceExprNode>(my_node);
  auto result = EvalAsBytecode(deref_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(struct_value, result.value());

  // Accessing "MyType->a" should use the PrettyType to dereference to the |struct_type| and then
  // resolve the member "a" on it, giving kAValue as the result.
  auto member_node = fxl::MakeRefCounted<MemberAccessExprNode>(
      my_node, ExprToken(ExprTokenType::kArrow, "->", 0), ParsedIdentifier("a"));
  result = EvalAsBytecode(member_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(kAValue, result.value().GetAs<int32_t>());
}

// The casting tests cover most casting-related functionality. This acts as a smoketest that it's
// hooked up, and specifically tests the tricky special-casing of casting references to references
// (which shouldn't expand the reference value).
TEST_F(ExprNodeTest, Cast) {
  DerivedClassTestSetup d;
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Base2& base2_ref_value = base2_value;
  // static_cast<Derived&>(base2_ref_value);  // <- cast_ref_ref_node
  auto base2_ref_node = fxl::MakeRefCounted<MockExprNode>(true, d.base2_ref_value);
  auto derived_ref_type_node =
      fxl::MakeRefCounted<TypeExprNode>(d.derived_ref_type, d.derived_ref_type);
  auto cast_ref_ref_node = fxl::MakeRefCounted<CastExprNode>(
      CastType::kStatic, std::move(derived_ref_type_node), std::move(base2_ref_node));

  // Do the call.
  auto result = EvalAsBytecode(cast_ref_ref_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(d.derived_ref_value, result.value());

  // Now cast a ref to an object. This should dereference the object and find the base class inside
  // of it.
  // static_cast<Base2>(derived_ref_value)
  auto derived_ref_node = fxl::MakeRefCounted<MockExprNode>(true, d.derived_ref_value);
  auto base2_type_node = fxl::MakeRefCounted<TypeExprNode>(d.base2_type, d.base2_type);
  auto cast_node = fxl::MakeRefCounted<CastExprNode>(CastType::kStatic, std::move(base2_type_node),
                                                     std::move(derived_ref_node));

  // Provide the memory for the derived type for when we dereference the ref.
  context->data_provider()->AddMemory(d.kDerivedAddr, d.derived_value.data().bytes());

  result = EvalAsBytecode(cast_node, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(d.base2_value, result.value());
}

// Tests integration with the PrettyType's member mechanism. A PrettyType provides a getter function
// that can evaluate a value on an object that looks like a member access.
TEST_F(ExprNodeTest, PrettyTypeMember) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Register the PrettyType that provides a getter.
  const char kTypeName[] = "MyType";
  IdentifierGlob glob;
  ASSERT_FALSE(glob.Init(kTypeName).has_error());
  context->pretty_type_manager().Add(ExprLanguage::kC, glob,
                                     std::make_unique<MockGetterPrettyType>());

  // Object on left side of the ".".
  auto type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, kTypeName);
  type->set_byte_size(1);  // Make it not zero size.
  ExprValue value(type, {});
  auto content = fxl::MakeRefCounted<MockExprNode>(true, value);

  // Evaluate "value.<kMemberName>`
  auto dot_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      content, ExprToken(ExprTokenType::kDot, ".", 0),
      ParsedIdentifier(MockGetterPrettyType::kMemberName));

  // Evaluate.
  auto result = EvalAsBytecode(dot_access, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(MockGetterPrettyType::kMemberValue, result.value().GetAs<int32_t>());

  // Now try one with a pointer.
  auto type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, type);
  constexpr uint64_t kAddress = 0x110000;
  ExprValue pointer_value(type_ptr, {0x00, 0x00, 0x11, 0, 0, 0, 0, 0});
  auto pointer = fxl::MakeRefCounted<MockExprNode>(true, pointer_value);

  // Data pointed to by the pointer (object is one byte, doesn't matter what value).
  context->data_provider()->AddMemory(kAddress, {0x00});

  auto arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      pointer, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kMemberName));

  // Evaluate.
  result = EvalAsBytecode(arrow_access, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(MockGetterPrettyType::kMemberValue, result.value().GetAs<int>());

  // Try an non-pointer with the "->" operator.
  auto invalid_arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      content, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kMemberName));

  result = EvalAsBytecode(invalid_arrow_access, context);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Attempting to dereference 'MyType' which is not a pointer.", result.err().msg());

  // Combine a custom dereferencer with a custom getter. So "needs_deref->getter()" where
  // needs_deref's type provides a pretty dereference operator.
  const char kDerefTypeName[] = "NeedsDeref";
  IdentifierGlob deref_glob;
  ASSERT_FALSE(deref_glob.Init(kDerefTypeName).has_error());
  context->pretty_type_manager().Add(ExprLanguage::kC, deref_glob,
                                     std::make_unique<MockDerefPrettyType>(value));

  // This is the node that returns the NeedsDeref type. Its value is unimportant.
  auto needs_deref_type = MakeCollectionType(DwarfTag::kStructureType, kDerefTypeName, {});
  ExprValue needs_deref_value(needs_deref_type, {});
  auto needs_deref_node = fxl::MakeRefCounted<MockExprNode>(true, needs_deref_value);

  // Nodes that represent the call "needs_deref->get5()";
  auto pretty_arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      needs_deref_node, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kMemberName));

  // Bytecode version.
  result = EvalAsBytecode(pretty_arrow_access, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(MockGetterPrettyType::kMemberValue, result.value().GetAs<int>());
}

// Tests integration with the PrettyType's getter mechanism. A PrettyType provides a getter function
// that can evaluate a value on an object that looks like a function call.
TEST_F(ExprNodeTest, PrettyTypeGetter) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Register the PrettyType that provides a getter.
  const char kTypeName[] = "MyType";
  IdentifierGlob glob;
  ASSERT_FALSE(glob.Init(kTypeName).has_error());
  context->pretty_type_manager().Add(ExprLanguage::kC, glob,
                                     std::make_unique<MockGetterPrettyType>());

  // Object on left side of the ".".
  auto type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, kTypeName);
  type->set_byte_size(1);  // Make it not zero size.
  ExprValue value(type, {});
  auto content = fxl::MakeRefCounted<MockExprNode>(true, value);

  // Evaluate "value.<kGetterName>`
  auto dot_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      content, ExprToken(ExprTokenType::kDot, ".", 0),
      ParsedIdentifier(MockGetterPrettyType::kGetterName));
  auto dot_call = fxl::MakeRefCounted<FunctionCallExprNode>(dot_access);

  // Evaluate.
  auto result = EvalAsBytecode(dot_call, context);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(MockGetterPrettyType::kGetterValue, result.value().GetAs<int32_t>());

  // Now try one with a pointer.
  auto type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, type);
  constexpr uint64_t kAddress = 0x110000;
  ExprValue pointer_value(type_ptr, {0x00, 0x00, 0x11, 0, 0, 0, 0, 0});
  auto pointer = fxl::MakeRefCounted<MockExprNode>(true, pointer_value);

  // Data pointed to by the pointer (object is one byte, doesn't matter what value).
  context->data_provider()->AddMemory(kAddress, {0x00});

  auto arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      pointer, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kGetterName));
  auto arrow_call = fxl::MakeRefCounted<FunctionCallExprNode>(arrow_access);

  // Evaluate.
  result = EvalAsBytecode(arrow_call, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(MockGetterPrettyType::kGetterValue, result.value().GetAs<int>());

  // Try an non-pointer with the "->" operator.
  auto invalid_arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      content, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kGetterName));
  auto invalid_arrow_call = fxl::MakeRefCounted<FunctionCallExprNode>(invalid_arrow_access);

  result = EvalAsBytecode(invalid_arrow_call, context);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Attempting to dereference 'MyType' which is not a pointer.", result.err().msg());

  // Combine a custom dereferencer with a custom getter. So "needs_deref->getter()" where
  // needs_deref's type provides a pretty dereference operator.
  const char kDerefTypeName[] = "NeedsDeref";
  IdentifierGlob deref_glob;
  ASSERT_FALSE(deref_glob.Init(kDerefTypeName).has_error());
  context->pretty_type_manager().Add(ExprLanguage::kC, deref_glob,
                                     std::make_unique<MockDerefPrettyType>(value));

  // This is the node that returns the NeedsDeref type. Its value is unimportant.
  auto needs_deref_type = MakeCollectionType(DwarfTag::kStructureType, kDerefTypeName, {});
  ExprValue needs_deref_value(needs_deref_type, {});
  auto needs_deref_node = fxl::MakeRefCounted<MockExprNode>(true, needs_deref_value);

  // Nodes that represent the call "needs_deref->get5()";
  auto pretty_arrow_access = fxl::MakeRefCounted<MemberAccessExprNode>(
      needs_deref_node, ExprToken(ExprTokenType::kArrow, "->", 0),
      ParsedIdentifier(MockGetterPrettyType::kGetterName));
  auto pretty_arrow_call = fxl::MakeRefCounted<FunctionCallExprNode>(pretty_arrow_access);

  result = EvalAsBytecode(pretty_arrow_call, context);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(MockGetterPrettyType::kGetterValue, result.value().GetAs<int>());
}

TEST_F(ExprNodeTest, Sizeof) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // References on raw types should be stripped. Make a one-byte sized type and an 8-byte reference
  // to it.
  auto char_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
  auto char_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, char_type);
  EXPECT_EQ(8u, char_ref_type->byte_size());

  auto char_ref_type_node = fxl::MakeRefCounted<TypeExprNode>(char_ref_type, char_ref_type);
  auto sizeof_char_ref_type = fxl::MakeRefCounted<SizeofExprNode>(char_ref_type_node);

  auto result = EvalAsBytecode(sizeof_char_ref_type, context);
  ASSERT_TRUE(result.ok());
  uint64_t sizeof_value = 0;
  EXPECT_FALSE(result.value().PromoteTo64(&sizeof_value).has_error());
  EXPECT_EQ(1u, sizeof_value);

  // Test sizeof() for an asynchronously-executed boolean value.
  auto char_value_node = fxl::MakeRefCounted<MockExprNode>(false, ExprValue(true));
  auto sizeof_char = fxl::MakeRefCounted<SizeofExprNode>(char_value_node);

  result = EvalAsBytecode(sizeof_char, context);
  ASSERT_TRUE(result.ok());
  sizeof_value = 0;
  EXPECT_FALSE(result.value().PromoteTo64(&sizeof_value).has_error());
  EXPECT_EQ(1u, sizeof_value);
}

}  // namespace zxdb
