// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_base.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/virtual_base_test_setup.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
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
  PromotePtrRefToDerived(eval_context, PromoteToDerived::kPtrOrRef, value,
                         [&called, original_value = value](ErrOrValue result) {
                           called = true;
                           EXPECT_TRUE(result.ok());
                           EXPECT_EQ(original_value, result.value());

                           // In this test the type object pointers should be the same (not normally
                           // tested in value equality) since the value should be the same one just
                           // forwarded.
                           EXPECT_EQ(original_value.type(), result.value().type());
                         });
  EXPECT_TRUE(called);
}

TEST_F(ResolveBase, PromotePtrRefToDerived) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  VirtualBaseTestSetup setup(eval_context.get());

  // Add a bunch of qualifiers to make sure they come out the other end.
  auto const_base_class = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, setup.base_class);
  auto ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, const_base_class);
  auto const_ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, ptr_const_base_class);

  // Input Base*.
  ExprValue base_ptr(setup.kBaseAddress, const_ptr_const_base_class);

  // -----------------------------------------------------------------------------------------------
  // Part 1: vtable pointer points to "Derived".

  // The default setup will have the vtable point to the derived class.
  ErrOrValue result(Err("Not called"));
  PromotePtrRefToDerived(eval_context, PromoteToDerived::kPtrOrRef, base_ptr,
                         [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok());

  // Now that the memory has been hooked up, the result should be a const*const (consts copied from
  // the original base type) with the derived address.
  uint64_t result64 = 0;
  ASSERT_TRUE(result.value().PromoteTo64(&result64).ok());
  EXPECT_EQ(setup.kDerivedAddress, result64);
  EXPECT_EQ("const DerivedClass* const", result.value().type()->GetFullName());

  // -----------------------------------------------------------------------------------------------
  // Part 2: vtable pointer points to "Base".

  // Fix up the vtable pointer to the base class.
  eval_context->AddLocation(setup.kVtableAddress,
                            Location(setup.kVtableAddress, FileLine(), 0,
                                     SymbolContext::ForRelativeAddresses(), setup.base_vtable));

  result = Err("Not called");
  PromotePtrRefToDerived(eval_context, PromoteToDerived::kPtrOrRef, base_ptr,
                         [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ(base_ptr, result.value());  // Should give same input as output.

  // -----------------------------------------------------------------------------------------------
  // Part 3: vtable pointer is invalid.

  // Declare no symbol ad this address.
  eval_context->AddLocation(setup.kVtableAddress,
                            Location(Location::State::kSymbolized, setup.kVtableAddress));

  // Should run asynchronously and produce success.
  result = Err("Not called");
  PromotePtrRefToDerived(eval_context, PromoteToDerived::kPtrOrRef, base_ptr,
                         [&result](ErrOrValue r) { result = r; });
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.ok());

  // We did not hook up the vtable memory above so the resolution will fail. It should fall back on
  // returning the input rather than forwarding an error.
  EXPECT_EQ(base_ptr, result.value());
  EXPECT_EQ("const BaseClass* const", result.value().type()->GetFullName());
}

}  // namespace zxdb
