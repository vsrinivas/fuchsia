// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

DerivedClassTestSetup::DerivedClassTestSetup() {
  // Main types.
  auto int16_type = MakeInt16Type();
  auto int32_type = MakeInt32Type();
  base1_type = MakeCollectionType(DwarfTag::kStructureType, "Base1",
                                  {{"b", int16_type}});
  base2_type = MakeCollectionType(DwarfTag::kStructureType, "Base2",
                                  {{"b", int32_type}});
  derived_type = MakeCollectionTypeWithOffset(
      DwarfTag::kStructureType, "Derived", 6, {{"d", int32_type}});
  derived_type->set_inherited_from(
      {LazySymbol(fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base1_type),
                                                     kBase1Offset)),
       LazySymbol(fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base2_type),
                                                     kBase2Offset))});

  // Pointer variants.
  base1_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                     LazySymbol(base1_type));
  base2_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                     LazySymbol(base2_type));
  derived_ptr_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(derived_type));

  // Reference types.
  base1_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType,
                                                     LazySymbol(base1_type));
  base2_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType,
                                                     LazySymbol(base2_type));
  derived_ref_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kReferenceType, LazySymbol(derived_type));

  // Values.
  constexpr uint8_t kB1Value = 32;
  constexpr uint8_t kB2Value = 78;
  constexpr uint8_t kDValue = 12;
  std::vector<uint8_t> base1_storage = {kB1Value, 0};
  std::vector<uint8_t> base2_storage = {kB2Value, 0, 0, 0};
  std::vector<uint8_t> derived_storage = {
      kB1Value, 0,         // Base #1 class' int16 value.
      kB2Value, 0, 0, 0,   // Base class' int32 value.
      kDValue,  0, 0, 0};  // Derived class' int32 value.
  derived_value =
      ExprValue(derived_type, derived_storage, ExprValueSource(kDerivedAddr));
  base1_value =
      ExprValue(base1_type, base1_storage, ExprValueSource(kBase1Addr));
  base2_value =
      ExprValue(base2_type, base2_storage, ExprValueSource(kBase2Addr));

  // Pointer values.
  std::vector<uint8_t> derived_ptr_storage = {0, 0x30, 0, 0, 0, 0, 0, 0};
  derived_ptr_value =
      ExprValue(derived_ptr_type, derived_ptr_storage, ExprValueSource());
  std::vector<uint8_t> base1_ptr_storage = {0, 0x30, 0, 0, 0, 0, 0, 0};
  base1_ptr_value =
      ExprValue(base1_ptr_type, base1_ptr_storage, ExprValueSource());
  std::vector<uint8_t> base2_ptr_storage = {0x02, 0x30, 0, 0, 0, 0, 0, 0};
  base2_ptr_value =
      ExprValue(base2_ptr_type, base2_ptr_storage, ExprValueSource());

  // Reference values (use the same addresse as the pointer variants).
  derived_ref_value =
      ExprValue(derived_ref_type, derived_ptr_storage, ExprValueSource());
  base1_ref_value =
      ExprValue(base1_ref_type, base1_ptr_storage, ExprValueSource());
  base2_ref_value =
      ExprValue(base2_ref_type, base2_ptr_storage, ExprValueSource());
}

}  // namespace zxdb
