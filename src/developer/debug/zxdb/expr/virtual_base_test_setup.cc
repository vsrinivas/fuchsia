// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/virtual_base_test_setup.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

constexpr uint32_t VirtualBaseTestSetup::kBaseOffset;
constexpr TargetPointer VirtualBaseTestSetup::kVtableRelativeAddress;
constexpr TargetPointer VirtualBaseTestSetup::kVtableAbsoluteAddress;
constexpr TargetPointer VirtualBaseTestSetup::kDerivedAddress;
constexpr TargetPointer VirtualBaseTestSetup::kBaseAddress;
const char* VirtualBaseTestSetup::kBaseIName = "base_i";
constexpr uint32_t VirtualBaseTestSetup::kBaseI;
const char* VirtualBaseTestSetup::kDerivedIName = "derived_i";
constexpr uint32_t VirtualBaseTestSetup::kDerivedI;

VirtualBaseTestSetup::VirtualBaseTestSetup(MockSymbolDataProvider* data_provider,
                                           MockModuleSymbols* mock_module_symbols) {
  vtbl_entry_type = MakeUint64Type();  // Pointer to function "int()" in real life.
  vtbl_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_entry_type);
  vtbl_ptr_type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, vtbl_ptr_type);

  auto& index_root = mock_module_symbols->index().root();

  // Base class.
  auto int32_type = MakeInt32Type();
  const char kBaseClassName[] = "BaseClass";
  base_class =
      MakeCollectionType(DwarfTag::kStructureType, kBaseClassName,
                         {{"_vptr$BaseClass", vtbl_ptr_type_ptr}, {kBaseIName, int32_type}});
  FX_DCHECK(base_class->byte_size() == 12);  // point = 8 bytes, int32 = 4.
  // The artificial flag must be set on the vtable pointer.
  const_cast<DataMember*>(base_class->data_members()[0].Get()->As<DataMember>())
      ->set_artificial(true);
  base_class_indexed = std::make_unique<TestIndexedSymbol>(mock_module_symbols, &index_root,
                                                           kBaseClassName, base_class);

  base_class_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, base_class);
  base_class_ref = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, base_class);

  // Derived class.
  //
  // Leave room at the beginning of the structure for the base class' data. Note that the
  // DerivedClass doesn't need a _vptr because it has no vtable of its own (its only virtual
  // functions are on the BaseClass).
  const char kDerivedClassName[] = "DerivedClass";
  derived_class = MakeCollectionTypeWithOffset(DwarfTag::kStructureType, kDerivedClassName,
                                               kBaseOffset + base_class->byte_size(),
                                               {{kDerivedIName, int32_type}});
  FX_DCHECK(derived_class->byte_size() == kBaseOffset + base_class->byte_size() + 4);

  auto inherited_from = fxl::MakeRefCounted<InheritedFrom>(base_class, kBaseOffset);
  derived_class->set_inherited_from({LazySymbol(inherited_from)});
  derived_class_indexed = std::make_unique<TestIndexedSymbol>(mock_module_symbols, &index_root,
                                                              kDerivedClassName, derived_class);

  auto const_base_class = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, base_class);
  auto ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, const_base_class);
  auto const_ptr_const_base_class =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, ptr_const_base_class);

  derived_data = std::vector<uint8_t>{
      // 4-bytes initial padding to make sure we handle offsets correctly.
      0, 0, 0, 0,

      // Base class' data to start with.
      0, 0, 0, 0, 0, 0, 0, 0,                 // _vptr$BaseClass = kVtableAbsoluteAddress
                                              // (Filled in below from the constant.)
      static_cast<uint8_t>(kBaseI), 0, 0, 0,  // base_i = kBaseI.

      // Derived data follows.
      static_cast<uint8_t>(kDerivedI), 0, 0, 0  // derived_i = kDerivedI.
  };
  // Write the absolute vtable address from the constant. Assumes little-endian.
  memcpy(&derived_data[4], &kVtableAbsoluteAddress, sizeof(uint64_t));

  FX_DCHECK(derived_class->byte_size() == derived_data.size());
  data_provider->AddMemory(kDerivedAddress, derived_data);

  base_vtable = fxl::MakeRefCounted<ElfSymbol>(
      nullptr,
      ElfSymbolRecord(ElfSymbolType::kNormal, kVtableRelativeAddress, 0, "_ZTV9BaseClass"));
  derived_vtable = fxl::MakeRefCounted<ElfSymbol>(
      nullptr,
      ElfSymbolRecord(ElfSymbolType::kNormal, kVtableRelativeAddress, 0, "_ZTV12DerivedClass"));

  // Hook up the vtable pointer.
  mock_module_symbols->AddSymbolLocations(
      kVtableAbsoluteAddress, {Location(kVtableAbsoluteAddress, FileLine(), 0,
                                        SymbolContext::ForRelativeAddresses(), derived_vtable)});
}

}  // namespace zxdb
