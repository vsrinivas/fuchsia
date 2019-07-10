// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_collection.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ResolveCollectionTest : public TestWithLoop {};

// Defines a class with two member types "a" and "b". It puts the definitions of "a" and "b' members
// into the two out params.
fxl::RefPtr<Collection> GetTestClassType(const DataMember** member_a, const DataMember** member_b) {
  auto int32_type = MakeInt32Type();
  auto sc =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", int32_type}, {"b", int32_type}});

  *member_a = sc->data_members()[0].Get()->AsDataMember();
  *member_b = sc->data_members()[1].Get()->AsDataMember();
  return sc;
}

// Helper function that calls ResolveMember with an identifier with the containing value.
Err ResolveMemberFromString(fxl::RefPtr<EvalContext> eval_context, const ExprValue& base,
                            const std::string& name, ExprValue* out) {
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier(name, &ident);
  if (err.has_error())
    return err;

  return ResolveMember(eval_context, base, ident, out);
}

}  // namespace

TEST_F(ResolveCollectionTest, GoodMemberAccess) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Make this const volatile to add extra layers.
  auto vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kVolatileType, LazySymbol(sc));
  auto const_vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, LazySymbol(vol_sc));

  // This struct has the values 1 and 2 in it.
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(const_vol_sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Resolve A.
  ExprValue out;
  Err err = ResolveMember(eval_context, base, a_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(1, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr, out.source().address());

  // Resolve A by name.
  ExprValue out_by_name;
  err = ResolveMemberFromString(eval_context, base, "a", &out_by_name);
  EXPECT_EQ(out, out_by_name);

  // Resolve B.
  out = ExprValue();
  err = ResolveMember(eval_context, base, b_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(2, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr + 4, out.source().address());

  // Resolve B by name.
  out_by_name = ExprValue();
  err = ResolveMemberFromString(eval_context, base, "b", &out_by_name);
  EXPECT_EQ(out, out_by_name);
}

// Tests that "a->b" can be resolved when the type of "a" is a foward definition. This requires
// looking up the symbol in the index to find its definition.
TEST_F(ResolveCollectionTest, ForwardDefinitionPtr) {
  // Need a bunch of symbol stuff to have the index.
  ProcessSymbolsTestSetup setup;
  auto mod_ref = std::make_unique<MockModuleSymbols>("mod.so");
  MockModuleSymbols* mod = mod_ref.get();  // Save for later.

  constexpr uint64_t kLoadAddress = 0x1000000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod1", "1234", kLoadAddress, std::move(mod_ref));
  auto& root = mod->index().root();  // Root of the index for module 1.

  auto provider = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // With the mock symbol system above, we make a real EvalContext that uses it.
  auto context = fxl::MakeRefCounted<EvalContextImpl>(setup.process().GetWeakPtr(), symbol_context,
                                                      provider, fxl::RefPtr<CodeBlock>());

  // Forward-declared type.
  const char kMyStructName[] = "MyStruct";
  auto forward_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_decl->set_assigned_name(kMyStructName);
  forward_decl->set_is_declaration(true);

  // Pointer to the forward declared type.
  auto forward_decl_ptr =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol(forward_decl));

  // Make a definition for the type and index it. It has one 32-bit data member.
  auto int32_type = MakeInt32Type();
  auto def = MakeCollectionType(DwarfTag::kStructureType, kMyStructName, {{"a", int32_type}});
  TestIndexedSymbol indexed_def(mod, &root, kMyStructName, def);

  // Define the data for the object. It has a 32-bit little-endian value.
  const uint64_t kObjectAddr = 0x12345678;
  const uint8_t kIntValue = 42;
  provider->AddMemory(kObjectAddr, {kIntValue, 0, 0, 0});

  // This pointer value references the memory above and its type is the forward declaration which
  // does not define the members.
  ExprValue ptr_value(forward_decl_ptr, {0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0});

  ParsedIdentifier a_ident;
  Err err = ExprParser::ParseIdentifier("a", &a_ident);
  ASSERT_FALSE(err.has_error());

  // Resolve by name on an object with the type referencing the forward declaration.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolveMemberByPointer(
      context, ptr_value, a_ident,
      [&called, &out_err, &out_value](const Err& err, fxl::RefPtr<DataMember>, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  // Requesting the memory for the pointer is async.
  EXPECT_FALSE(out_err.has_error()) << err.msg();
  EXPECT_FALSE(called);
  loop().PostTask(FROM_HERE, [loop = &loop()]() { loop->QuitNow(); });
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << err.msg();

  // Should have resolved to the int32.
  ASSERT_EQ(int32_type.get(), out_value.type());
  EXPECT_EQ(kIntValue, out_value.GetAs<int32_t>());
}

TEST_F(ResolveCollectionTest, BadMemberArgs) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Test null base class pointer.
  ExprValue out;
  Err err = ResolveMember(eval_context, ExprValue(), a_data, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't resolve data member on non-struct/class value.", err.msg());

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}, ExprValueSource(kBaseAddr));

  // Null data member pointer.
  out = ExprValue();
  err = ResolveMember(eval_context, base, nullptr, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", err.msg());
}

TEST_F(ResolveCollectionTest, BadMemberAccess) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}, ExprValueSource(kBaseAddr));

  // Lookup by name that doesn't exist.
  ExprValue out;
  Err err = ResolveMemberFromString(eval_context, base, "c", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No member 'c' in struct 'Foo'.", err.msg());

  // Lookup by a DataMember that references outside of the struct (in this case, by one byte).
  auto bad_member = fxl::MakeRefCounted<DataMember>();
  bad_member->set_assigned_name("c");
  bad_member->set_type(LazySymbol(MakeInt32Type()));
  bad_member->set_member_location(5);

  out = ExprValue();
  err = ResolveMember(eval_context, base, bad_member.get(), &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", err.msg());
}

// Tests foo.bar where bar is in a derived class of foo's type.
TEST_F(ResolveCollectionTest, DerivedClass) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  const DataMember* a_data;
  const DataMember* b_data;
  auto base = GetTestClassType(&a_data, &b_data);

  auto derived = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);

  uint32_t base_offset = 4;  // Offset in derived of base.
  auto inherited = fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base), base_offset);
  derived->set_inherited_from({LazySymbol(inherited)});

  // This struct has the values 1 and 2 in it, offset by 4 bytes (the offset within "derived" of
  // "base").
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue value(derived, {0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                  ExprValueSource(kBaseAddr));

  // Resolve B by name.
  ExprValue out;
  Err err = ResolveMemberFromString(eval_context, value, "b", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(2, out.GetAs<int32_t>());

  // Offset of B in "derived".
  EXPECT_EQ(kBaseAddr + base_offset + 4, out.source().address());

  // Test extracting the base class from the derived one.
  ExprValue base_value;
  err = ResolveInherited(value, inherited.get(), &base_value);
  EXPECT_FALSE(err.has_error());

  ExprValue expected_base(base, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                          ExprValueSource(kBaseAddr + base_offset));
  EXPECT_EQ(expected_base, base_value);

  // Test the other variant of ResolveInherited.
  base_value = ExprValue();
  err = ResolveInherited(value, base, base_offset, &base_value);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(expected_base, base_value);
}

}  // namespace zxdb
