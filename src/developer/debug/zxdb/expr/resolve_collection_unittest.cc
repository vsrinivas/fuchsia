// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_collection.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
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
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ResolveCollectionTest : public TestWithLoop {
 public:
  ResolveCollectionTest() : module_symbol_context_(ProcessSymbolsTestSetup::kDefaultLoadAddress) {}

  void SetUp() override {
    TestWithLoop::SetUp();

    module_symbols_ = process_setup_.InjectMockModule();
    index_root_ = &module_symbols_->index().root();  // Root of the index for module 1.

    data_provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();

    // With the mock symbol system above, we make a real EvalContext that uses it.
    eval_context_ =
        fxl::MakeRefCounted<EvalContextImpl>(process_setup_.process().GetWeakPtr(), data_provider_,
                                             ExprLanguage::kC, fxl::RefPtr<CodeBlock>());
  }
  void TearDown() override {
    index_root_ = nullptr;
    data_provider_.reset();
    eval_context_.reset();
    module_symbols_ = nullptr;

    TestWithLoop::TearDown();
  }

 protected:
  ProcessSymbolsTestSetup process_setup_;

  // Injected module.
  MockModuleSymbols* module_symbols_;  // Owned by process_setup_.
  SymbolContext module_symbol_context_;
  IndexNode* index_root_ = nullptr;

  fxl::RefPtr<MockSymbolDataProvider> data_provider_;
  fxl::RefPtr<EvalContextImpl> eval_context_;
};

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
ErrOrValue ResolveMemberFromString(const fxl::RefPtr<EvalContext>& eval_context,
                                   const ExprValue& base, const std::string& name) {
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier(name, &ident);
  if (err.has_error())
    return err;

  return ResolveNonstaticMember(eval_context, base, ident);
}

}  // namespace

TEST_F(ResolveCollectionTest, GoodMemberAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Make this const volatile to add extra layers.
  auto vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kVolatileType, sc);
  auto const_vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, vol_sc);

  // This struct has the values 1 and 2 in it.
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(const_vol_sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Resolve A.
  ErrOrValue out = ResolveNonstaticMember(eval_context_, base, FoundMember(a_data));
  ASSERT_TRUE(out.ok()) << out.err().msg();
  EXPECT_EQ("int32_t", out.value().type()->GetAssignedName());
  EXPECT_EQ(4u, out.value().data().size());
  EXPECT_EQ(1, out.value().GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr, out.value().source().address());

  // Resolve A by name.
  ErrOrValue out_by_name = ResolveMemberFromString(eval_context_, base, "a");
  ASSERT_TRUE(out_by_name.ok());
  EXPECT_EQ(out.value(), out_by_name.value());

  // Resolve B.
  out = ResolveNonstaticMember(eval_context_, base, FoundMember(b_data));
  ASSERT_TRUE(out.ok()) << out.err().msg();
  EXPECT_EQ("int32_t", out.value().type()->GetAssignedName());
  EXPECT_EQ(4u, out.value().data().size());
  EXPECT_EQ(2, out.value().GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr + 4, out.value().source().address());

  // Resolve B by name.
  out_by_name = ResolveMemberFromString(eval_context_, base, "b");
  ASSERT_TRUE(out_by_name.ok());
  EXPECT_EQ(out.value(), out_by_name.value());
}

// Tests that "a->b" can be resolved when the type of "a" is a forward definition. This requires
// looking up the symbol in the index to find its definition.
TEST_F(ResolveCollectionTest, ForwardDefinitionPtr) {
  // Forward-declared type.
  const char kMyStructName[] = "MyStruct";
  auto forward_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_decl->set_assigned_name(kMyStructName);
  forward_decl->set_is_declaration(true);

  // Pointer to the forward declared type.
  auto forward_decl_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, forward_decl);

  // Make a definition for the type and index it. It has one 32-bit data member.
  auto int32_type = MakeInt32Type();
  auto def = MakeCollectionType(DwarfTag::kStructureType, kMyStructName, {{"a", int32_type}});
  TestIndexedSymbol indexed_def(module_symbols_, index_root_, kMyStructName, def);

  // Define the data for the object. It has a 32-bit little-endian value.
  const uint64_t kObjectAddr = 0x12345678;
  const uint8_t kIntValue = 42;
  data_provider_->AddMemory(kObjectAddr, {kIntValue, 0, 0, 0});

  // This pointer value references the memory above and its type is the forward declaration which
  // does not define the members.
  ExprValue ptr_value(forward_decl_ptr, {0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0});

  ParsedIdentifier a_ident;
  Err err = ExprParser::ParseIdentifier("a", &a_ident);
  ASSERT_FALSE(err.has_error());

  // Resolve by name on an object with the type referencing the forward declaration.
  bool called = false;
  ErrOrValue out((ExprValue()));
  ResolveMemberByPointer(eval_context_, ptr_value, a_ident,
                         [&called, &out](ErrOrValue value, const FoundMember&) {
                           called = true;
                           out = std::move(value);
                         });

  // Requesting the memory for the pointer is async.
  EXPECT_FALSE(called);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);
  ASSERT_FALSE(out.has_error()) << err.msg();

  // Should have resolved to the int32.
  ASSERT_EQ(int32_type.get(), out.value().type());
  EXPECT_EQ(kIntValue, out.value().GetAs<int32_t>());
}

// Tests that a member type can be a forward definition and we can still find the size to extract it
// properly. This happens for std::string which is an extern template. The full definition is
// included only in libc++ even though the full definition is known at the time a struct including
// it is compiled.
TEST_F(ResolveCollectionTest, ForwardDefMember) {
  // Forward-declared type.
  const char kFwdDeclaredName[] = "FwdDeclared";
  auto forward_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_decl->set_assigned_name(kFwdDeclaredName);
  forward_decl->set_is_declaration(true);
  EXPECT_EQ(0u, forward_decl->byte_size());  // Forward-decls don't have sizes.

  // Real definition of the type in the index.
  auto int32_type = MakeInt32Type();
  auto def = MakeCollectionType(DwarfTag::kStructureType, kFwdDeclaredName, {{"a", int32_type}});
  TestIndexedSymbol indexed_def(module_symbols_, index_root_, kFwdDeclaredName, def);

  // Struct that contains a reference to the forward-declared type as a member.
  const char kMemberName[] = "a";
  auto containing =
      MakeCollectionType(DwarfTag::kStructureType, "Containing", {{kMemberName, forward_decl}});
  containing->set_byte_size(def->byte_size());
  ExprValue containing_value(containing, {1, 0, 0, 0});

  // Now resolve the member.
  auto result =
      ResolveNonstaticMember(eval_context_, containing_value, ParsedIdentifier(kMemberName));
  ASSERT_TRUE(result.ok());

  // The result should be the right size which it should have picked up from the index, but the
  // actual type should be the forward declaration (in this case, it might be more convenient if
  // the return value was the definition since it's equivalent, but in practice there might by
  // typedefs or C-V qualifiers so we always need to return the type specified in the struct
  // definition.
  EXPECT_EQ(def->byte_size(), result.value().data().size());
  EXPECT_EQ(forward_decl.get(), result.value().type());
}

TEST_F(ResolveCollectionTest, ExternStaticMember) {
  // This test doesn't do an end-to-end resolution of the EvalContextImpl resolving extern variables
  // since that requires a lot of setup and is tested by the EvalContextImpl unit tests. Instead
  // this test only tests the resolve_collection code and validates that the extern variable was
  // detected and the right EvalContext function was called.
  const char kName[] = "member_name";

  // External data member.
  auto extern_member = fxl::MakeRefCounted<DataMember>(kName, MakeInt32Type(), 0);
  extern_member->set_is_external(true);

  // Collection with the member.
  auto collection = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  SymbolTestParentSetter extern_member_parent(extern_member, collection);

  collection->set_assigned_name("Collection");
  collection->set_data_members({LazySymbol(extern_member)});

  // The collection needs no storage since the member is static.
  ExprValue collection_value(collection, {});

  auto mock_eval_context = fxl::MakeRefCounted<MockEvalContext>();
  ExprValue expected(42);
  mock_eval_context->AddVariable(extern_member.get(), expected);

  bool called = false;
  ResolveMember(mock_eval_context, collection_value, ParsedIdentifier(kName),
                [&called, expected](ErrOrValue result) {
                  called = true;

                  EXPECT_FALSE(result.has_error());
                  EXPECT_EQ(expected, result.value());
                });
  EXPECT_TRUE(called);
}

TEST_F(ResolveCollectionTest, BadMemberArgs) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Test null base class pointer.
  ErrOrValue out = ResolveNonstaticMember(eval_context_, ExprValue(), FoundMember(a_data));
  ASSERT_TRUE(out.has_error());
  EXPECT_EQ("Can't resolve data member on non-struct/class value.", out.err().msg());

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}, ExprValueSource(kBaseAddr));

  // Null data member pointer.
  out = ResolveNonstaticMember(eval_context_, base, FoundMember());
  EXPECT_TRUE(out.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", out.err().msg());
}

TEST_F(ResolveCollectionTest, BadMemberAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00}, ExprValueSource(kBaseAddr));

  // Lookup by name that doesn't exist.
  ErrOrValue out = ResolveMemberFromString(eval_context_, base, "c");
  ASSERT_TRUE(out.has_error());
  EXPECT_EQ("No member 'c' in struct 'Foo'.", out.err().msg());

  // Lookup by a DataMember that references outside of the struct (in this case, by one byte).
  auto bad_member = fxl::MakeRefCounted<DataMember>();
  bad_member->set_assigned_name("c");
  bad_member->set_type(MakeInt32Type());
  bad_member->set_member_location(5);

  out = ResolveNonstaticMember(eval_context_, base, FoundMember(bad_member.get()));
  ASSERT_TRUE(out.has_error());
  EXPECT_EQ("Invalid data offset 5 in object of size 8.", out.err().msg());
}

// Tests foo.bar where bar is in a derived class of foo's type.
TEST_F(ResolveCollectionTest, DerivedClass) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto base = GetTestClassType(&a_data, &b_data);

  auto derived = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);

  uint32_t base_offset = 4;  // Offset in derived of base.
  auto inherited = fxl::MakeRefCounted<InheritedFrom>(base, base_offset);
  derived->set_inherited_from({LazySymbol(inherited)});

  // This struct has the values 1 and 2 in it, offset by 4 bytes (the offset within "derived" of
  // "base").
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue value(derived, {0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                  ExprValueSource(kBaseAddr));

  // Resolve B by name.
  ErrOrValue out = ResolveMemberFromString(eval_context_, value, "b");
  ASSERT_TRUE(out.ok()) << out.err().msg();
  EXPECT_EQ("int32_t", out.value().type()->GetAssignedName());
  EXPECT_EQ(4u, out.value().data().size());
  EXPECT_EQ(2, out.value().GetAs<int32_t>());

  // Offset of B in "derived".
  EXPECT_EQ(kBaseAddr + base_offset + 4, out.value().source().address());

  // Test extracting the base class from the derived one.
  ErrOrValue base_value = ResolveInherited(eval_context_, value, inherited.get());
  ASSERT_TRUE(base_value.ok());

  ExprValue expected_base(base, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                          ExprValueSource(kBaseAddr + base_offset));
  EXPECT_EQ(expected_base, base_value.value());

  // Test the other variant of ResolveInherited.
  base_value = ResolveInherited(eval_context_, value, base, base_offset);
  ASSERT_TRUE(base_value.ok());
  EXPECT_EQ(expected_base, base_value.value());
}

// Like DerivedClass but using virtual inheritance. This data was copied from a test program.
TEST_F(ResolveCollectionTest, VirtualInheritance) {
  // This is the vtable information (from the ELF file).
  constexpr uint64_t kVtableAddress = 0x70f355000;
  const std::vector<uint8_t> vtable_data{
      0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x00: Offset of base from derived.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x08: ?
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x10: ?
      0xf0, 0x11, 0x15, 0x0f, 0x07, 0x00, 0x00, 0x00,  // +0x18: Derived Vtable (ptr to virt fn).
      0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // +0x20: Offset of derived from base.
      0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // +0x28: ?
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x30: ?
      0x10, 0x12, 0x15, 0x0f, 0x07, 0x00, 0x00, 0x00,  // +0x38: Base vtable (I think).
  };
  data_provider_->AddMemory(kVtableAddress, vtable_data);

  // Derived class data (on the heap).
  constexpr uint64_t kDerivedAddress = 0x9aaa319ba8;
  const std::vector<uint8_t> derived_data{
      0x18, 0x50, 0x35, 0x0f, 0x07, 0x00, 0x00, 0x00,  // Derived vtable.
      0x63, 0x00, 0x00, 0x00,                          // Derived object data (uint32_t) 99.
      0xaa, 0xaa, 0xaa, 0xaa,                          // Padding.
      0x38, 0x50, 0x35, 0x0f, 0x07, 0x00, 0x00, 0x00,  // Base vtable.
      0x2a, 0x00, 0x00, 0x00,                          // Base object data (uint32_t) 42.
      0xaa, 0xaa, 0xaa, 0xaa};                         // Padding.
  data_provider_->AddMemory(kDerivedAddress, derived_data);

  // Base class data is inside the derived data above.
  constexpr uint64_t kBaseOffset = 0x10;
  constexpr uint64_t kBaseAddress = kDerivedAddress + kBaseOffset;
  constexpr size_t kBaseSize = 12;  // Not counting the padding.

  // Clang uses "vtbl_ptr_type*" as the type for the vtable pointers at the beginning of a virtual
  // class. Clang defines the vtable as being pointers to functions "int()", so a pointer to a table
  // is a pointer to that. For simplicity, we define it as uint64_t instead of "int()".
  auto vtbl_entry_type = MakeUint64Type();  // Pointer to function "int()" in real life.
  auto vtbl_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_entry_type);
  auto vtbl_ptr_type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_ptr_type);

  // Base type definition.
  auto int32_type = MakeInt32Type();
  auto base_type =
      MakeCollectionType(DwarfTag::kClassType, "MyBase",
                         {{"_vptr$MyBase", vtbl_ptr_type_ptr}, {"base_i", int32_type}});

  // Derived type definition.
  auto derived_type =
      MakeCollectionType(DwarfTag::kClassType, "MyDerived",
                         {{"_vptr$MyDerived", vtbl_ptr_type_ptr}, {"derived_i", int32_type}});

  // Inheritance information. The derived class' address will be placed at the top of the stack to
  // run this. The computation steps are:
  //
  //   1. derived        = Pointer to derived class.
  //   2. (*derived)     = Dereference derived vtable = 0x70f355018
  //   3. - 0x18         = Compute address of offset of base inside of derived.
  //   4. Dereference    = Offset of base inside of derived = 0x10
  //   5. derived + 0x10 = Location of base.
  std::vector<uint8_t> base_loc_expr{
      llvm::dwarf::DW_OP_dup,   llvm::dwarf::DW_OP_deref, llvm::dwarf::DW_OP_constu, 0x18,
      llvm::dwarf::DW_OP_minus, llvm::dwarf::DW_OP_deref, llvm::dwarf::DW_OP_plus};
  auto inherited = fxl::MakeRefCounted<InheritedFrom>(base_type, base_loc_expr);
  derived_type->set_inherited_from({LazySymbol(inherited)});

  ExprValue derived(derived_type, derived_data, ExprValueSource(kDerivedAddress));

  // Asynchronously evaluate the base class.
  bool called = false;
  ErrOrValue result(Err("Uncalled"));
  ResolveInherited(eval_context_, derived, inherited.get(), module_symbol_context_,
                   [&called, &result](ErrOrValue r) {
                     called = true;
                     result = r;
                   });
  ASSERT_FALSE(called);
  loop().RunUntilNoTasks();
  ASSERT_TRUE(called);

  ASSERT_TRUE(result.ok()) << result.err().msg();
  ExprValue base = result.value();

  // Should be located at the expected place in memory.
  EXPECT_EQ(ExprValueSource::Type::kMemory, base.source().type());
  EXPECT_EQ(kBaseAddress, base.source().address());

  EXPECT_EQ(base_type.get(), base.type());
  std::vector<uint8_t> expected_base_data(derived_data.begin() + kBaseOffset,
                                          derived_data.begin() + kBaseOffset + kBaseSize);
  EXPECT_EQ(expected_base_data, base.data());
}

// Tests resolving data members with "const values" given in the symbols.
TEST_F(ResolveCollectionTest, ConstValue) {
  // Regular member.
  auto int32_type = MakeInt32Type();
  auto int_member = fxl::MakeRefCounted<DataMember>("a", int32_type, 0);

  // Const member. This one doesn't have the external flag set. That is normally the case since
  // for these to be inlined as members they have to be static which will set the is_external()
  // flag. But the spec doesn't require that so we need to handle ConstValue members based only
  // on the presence of the ConstValue attribute.
  const char kConstMemberName[] = "kMember";
  auto const_int32_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, int32_type);
  auto const_member = fxl::MakeRefCounted<DataMember>(kConstMemberName, const_int32_type, 0);
  uint8_t kConstValue = 42;
  const_member->set_const_value(ConstValue({kConstValue, 0, 0, 0}));

  // Check an extern member. This is a different code path.
  const char kExternConstMemberName[] = "kExternMember";
  auto extern_const_member =
      fxl::MakeRefCounted<DataMember>(kExternConstMemberName, const_int32_type, 0);
  uint8_t kExternConstValue = 99;
  extern_const_member->set_const_value(ConstValue({kExternConstValue, 0, 0, 0}));
  extern_const_member->set_is_external(true);

  auto collection = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "MyStruct");
  collection->set_data_members(
      std::vector<LazySymbol>{int_member, const_member, extern_const_member});

  // The collection holds only the non-const-value integer.
  collection->set_byte_size(4);
  ExprValue coll_value(collection, {0xff, 0xff, 0xff, 0xff});

  // Const one.
  ErrOrValue result =
      ResolveNonstaticMember(eval_context_, coll_value, ParsedIdentifier(kConstMemberName));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(const_int32_type.get(), result.value().type());
  EXPECT_EQ(kConstValue, result.value().GetAs<int32_t>());
  EXPECT_EQ(ExprValueSource::Type::kConstant, result.value().source().type());

  // Extern const one. Have to use the non-nonstatic call to fully test the static codepath.
  bool called = false;
  ResolveMember(eval_context_, coll_value, ParsedIdentifier(kExternConstMemberName),
                [&called, &result](ErrOrValue in_result) {
                  called = true;
                  result = std::move(in_result);
                });
  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);

  ASSERT_TRUE(result.ok());
  EXPECT_EQ(const_int32_type.get(), result.value().type());
  EXPECT_EQ(kExternConstValue, result.value().GetAs<int32_t>());
  EXPECT_EQ(ExprValueSource::Type::kConstant, result.value().source().type());
}

}  // namespace zxdb
