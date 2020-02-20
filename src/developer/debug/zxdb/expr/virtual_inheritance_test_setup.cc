// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/virtual_inheritance_test_setup.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

VirtualInheritanceTestSetup::VirtualInheritanceTestSetup() {
  auto int32_type = MakeInt32Type();

  // Base.
  base = MakeCollectionType(DwarfTag::kClassType, "Base", {{"base_i", int32_type}});

  // IntermediateBase.
  intermediate_base = MakeCollectionType(DwarfTag::kClassType, "IntermediateBase",
                                         {{"intermediate_base_i", int32_type}});
  FXL_CHECK(intermediate_base->byte_size() == kBaseAddress - kIntermediateBaseAddress);
  // Make room for "Base" which follows the intermediate_base_i data.
  base_inherited = fxl::MakeRefCounted<InheritedFrom>(base, intermediate_base->byte_size());
  intermediate_base->set_byte_size(intermediate_base->byte_size() + base->byte_size());
  intermediate_base->set_inherited_from({LazySymbol(base_inherited)});

  // IntermediateDerived.
  //
  // The vtable pointer will actually be declared as some kind of pointer. But we never need the
  // type so just make an 8-byte value as a placeholder.
  auto uint64_type = MakeUint64Type();
  intermediate_derived = MakeCollectionType(
      DwarfTag::kClassType, "IntermediateDerived",
      {{"_vptr.IntermediateDerived", uint64_type}, {"intermediate_derived_i", int32_type}});
  FXL_CHECK(intermediate_derived->byte_size() ==
            kIntermediateBaseAddress - kIntermediateDerivedAddress);
  // Make room for the base classes.
  intermediate_derived->set_byte_size(intermediate_derived->byte_size() +
                                      intermediate_base->byte_size());

  // Virtual inheritance. This expression is what GCC generated for a simple example. Recall the
  // initial stack state for running this program will have the address of IntermediateDerived.
  std::vector<uint8_t> expression = {
      llvm::dwarf::DW_OP_dup,    // Make 2 copies of the IntermediateDerived address.
      llvm::dwarf::DW_OP_deref,  // Read the vtable_ptr to top of stack.
      llvm::dwarf::DW_OP_lit24,  // Move pointer backwards 24 bytes to point to the offset.
      llvm::dwarf::DW_OP_minus,  //   (cont)
      llvm::dwarf::DW_OP_deref,  // Read the offset from the computed pointer.
      llvm::dwarf::DW_OP_plus    // Add the IntermediateDerived address and the offset.
  };
  intermediate_base_inherited = fxl::MakeRefCounted<InheritedFrom>(intermediate_base, expression);
  intermediate_derived->set_inherited_from({LazySymbol(intermediate_base_inherited)});

  // Derived.
  derived = MakeCollectionType(DwarfTag::kClassType, "Derived", {{"derived_i", int32_type}});
  FXL_CHECK(derived->byte_size() == kIntermediateDerivedAddress - kDerivedAddress);
  intermediate_derived_inherited =
      fxl::MakeRefCounted<InheritedFrom>(intermediate_derived, derived->byte_size());
  derived->set_byte_size(derived->byte_size() + intermediate_derived->byte_size());
  derived->set_inherited_from({LazySymbol(intermediate_derived_inherited)});

  // Object.
  std::vector<uint8_t> derived_data = {
      0x01, 0x00, 0x00, 0x00,                          // derived_i = 1.
      0x1c, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,  // vtable_ptr = kVtablePtr.
      0x02, 0x00, 0x00, 0x00,                          // intermediate_derived_i = 2.
      0x03, 0x00, 0x00, 0x00,                          // intermediate_base_i = 3.
      0x04, 0x00, 0x00, 0x00,                          // base_i = 4.
  };
  FXL_CHECK(derived_data.size() == derived->byte_size());
  derived_value = ExprValue(derived, derived_data, ExprValueSource(kDerivedAddress));

  // Vtable data.
  virtual_data = std::vector<uint8_t>{
      // Offset of base in derived for the virtual inheritance, expressed in 64-bit little-endian.
      static_cast<uint8_t>(kIntermediateBaseAddress - kIntermediateDerivedAddress), 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00,

      // This is extra data to cover the whole vtable range. It shouldn't be technically needed.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

void VirtualInheritanceTestSetup::SaveMockData(MockSymbolDataProvider* mock) const {
  mock->AddMemory(kDerivedAddress, derived_value.data());
  mock->AddMemory(kVirtualDataAddress, virtual_data);
}

}  // namespace zxdb
