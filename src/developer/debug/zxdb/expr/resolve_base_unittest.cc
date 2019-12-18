// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_base.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ResolveBase : public TestWithLoop {};

}  // namespace

// Given a class without a vtable, verifies that the DerivedTypeForVtable does a synchronous no-op.
TEST_F(ResolveBase, PromotePtrRefToDerived_NotVirtual) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  auto not_virtual =
      MakeCollectionType(DwarfTag::kStructureType, "MyStruct", {{"a", MakeInt32Type()}});
  std::vector<uint8_t> data{42, 0, 0, 0};  // int32_t = 42.

  ExprValue value(not_virtual, data);

  bool called = false;
  PromotePtrRefToDerived(eval_context, value, [&called, original_value = value](ErrOrValue result) {
    called = true;
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(original_value, result.value());

    // In this test the type object pointers should be the same (not normally tested in value
    // equality) since the value should be the same one just forwarded.
    EXPECT_EQ(original_value.type(), result.value().type());
  });
  EXPECT_TRUE(called);
}

TEST_F(ResolveBase, PromotePtrRefToDerived) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Clang uses "vtbl_ptr_type*" as the type for the vtable pointers at the beginning of a virtual
  // class. Clang defines the vtable as being pointers to functions "int()", so a pointer to a table
  // is a pointer to that. For simplicity, we define it as uint64_t instead of "int()".
  auto vtbl_entry_type = MakeUint64Type();  // Pointer to function "int()" in real life.
  auto vtbl_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_entry_type);
  auto vtbl_ptr_type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_ptr_type);

  // Base class.
  auto int32_type = MakeInt32Type();
  auto base_class =
      MakeCollectionType(DwarfTag::kStructureType, "BaseClass",
                         {{"_vptr$BaseClass", vtbl_ptr_type_ptr}, {"base_i", int32_type}});
  // The artificial flag must be set on the vtable pointer.
  const_cast<DataMember*>(base_class->data_members()[0].Get()->AsDataMember())
      ->set_artificial(true);
  auto const_base_class = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, base_class);
  auto ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, const_base_class);
  auto const_ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, ptr_const_base_class);
  eval_context->AddType(base_class);

  // Derived class.
  //
  // Leave room at the beginning of the structure for the base class' data. Note that the
  // DerivedClass doesn't need a _vptr because it has no vtable of its own (its only virtual
  // functions are on the BaseClass).
  //
  // This starts the base class at an offset inside of the derived one, leaving empty bytes at the
  // beginning. This simulates having multiple inheritance and tests the offset management.
  constexpr uint32_t kBaseOffset = 4;
  auto derived_class = MakeCollectionTypeWithOffset(
      DwarfTag::kStructureType, "DerivedClass", kBaseOffset + base_class->byte_size(),
      {{"_vptr$DerivedClass", vtbl_ptr_type_ptr}, {"derived_i", int32_type}});
  auto inherited_from = fxl::MakeRefCounted<InheritedFrom>(base_class, kBaseOffset);
  derived_class->set_inherited_from({LazySymbol(inherited_from)});
  eval_context->AddType(derived_class);

  constexpr TargetPointer kVtableAddress = 0x200000;

  constexpr TargetPointer kDerivedAddress = 0x1000;
  constexpr TargetPointer kBaseAddress = kDerivedAddress + kBaseOffset;

  std::vector<uint8_t> derived_data{
      // 4-bytes initial padding to make sure we handle offsets correctly.
      0, 0, 0, 0,

      // Base class' data to start with.
      0, 0, 0x20, 0, 0, 0, 0, 0,  // _vptr$BaseClass = kVtableAddress.
      42, 0, 0, 0,                // base_t = 42.

      // Derived data follows.
      99, 0, 0, 0  // derived_i = 99.
  };
  eval_context->data_provider()->AddMemory(kDerivedAddress, derived_data);

  // -----------------------------------------------------------------------------------------------
  // Part 1: vtable pointer is invalid.

  // Input Base*.
  ExprValue base_ptr(kBaseAddress, const_ptr_const_base_class);

  // Should run asynchronously and produce success.
  ErrOrValue result(Err("Not called"));
  PromotePtrRefToDerived(eval_context, base_ptr, [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok());

  // We did not hook up the vtable memory above so the resolution will fail. It should fall back on
  // returning the input rather than forwarding an error.
  EXPECT_EQ(base_ptr, result.value());
  EXPECT_EQ("const BaseClass* const", result.value().type()->GetFullName());

  // -----------------------------------------------------------------------------------------------
  // Part 2: vtable pointer points to "Base".

  auto base_vtable = fxl::MakeRefCounted<ElfSymbol>(
      nullptr, ElfSymbolRecord(ElfSymbolType::kNormal, kVtableAddress, "_ZTV9BaseClass"));
  eval_context->AddLocation(
      kVtableAddress,
      Location(kVtableAddress, FileLine(), 0, SymbolContext::ForRelativeAddresses(), base_vtable));

  result = Err("Not called");
  PromotePtrRefToDerived(eval_context, base_ptr, [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(base_ptr, result.value());  // Should give same input as output.

  // -----------------------------------------------------------------------------------------------
  // Part 3: vtable pointer points to "Derived".

  auto derived_vtable = fxl::MakeRefCounted<ElfSymbol>(
      nullptr, ElfSymbolRecord(ElfSymbolType::kNormal, kVtableAddress, "_ZTV12DerivedClass"));
  eval_context->AddLocation(
      kVtableAddress, Location(kVtableAddress, FileLine(), 0, SymbolContext::ForRelativeAddresses(),
                               derived_vtable));

  // The result should be a  pointing to the derived address.
  result = Err("Not called");
  PromotePtrRefToDerived(eval_context, base_ptr, [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok());

  // Now that the memory has been hooked up, the result should be a const*const (consts copied from
  // the original base type) with the derived address.
  uint64_t result64 = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result64).ok());
  EXPECT_EQ(kDerivedAddress, result64);
  EXPECT_EQ("const DerivedClass* const", result.value().type()->GetFullName());
}

}  // namespace zxdb
