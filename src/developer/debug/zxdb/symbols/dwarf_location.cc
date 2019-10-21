// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_location.h"

#include <limits>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/common/data_extractor.h"

namespace zxdb {

VariableLocation DecodeVariableLocation(const llvm::DWARFUnit* unit,
                                        const llvm::DWARFFormValue& form) {
  if (form.isFormClass(llvm::DWARFFormValue::FC_Block) ||
      form.isFormClass(llvm::DWARFFormValue::FC_Exprloc)) {
    // These forms are both a block of data which is interpreted as a DWARF expression. There is no
    // validity range for this so assume the expression is valid as long as the variable is in
    // scope.
    llvm::ArrayRef<uint8_t> block = *form.getAsBlock();
    return VariableLocation(block.data(), block.size());
  }

  if (!form.isFormClass(llvm::DWARFFormValue::FC_SectionOffset))
    return VariableLocation();  // Unknown type.

  // This form is a "section offset" reference to a block in the .debug_loc table that contains a
  // list of valid ranges + associated expressions.
  llvm::DWARFContext& context = unit->getContext();
  const llvm::DWARFObject& object = context.getDWARFObj();
  const llvm::DWARFSection& debug_loc_section = object.getLocSection();
  if (debug_loc_section.Data.empty()) {
    // LLVM dumpLocation() falls back on the DWARFObject::getLocDWOSection() call in this case. We
    // don't support DWOs yet so just fail.
    return VariableLocation();
  }

  uint32_t offset = *form.getAsSectionOffset();
  if (offset >= debug_loc_section.Data.size())
    return VariableLocation();  // Off the end.

  containers::array_view<uint8_t> section_data(
      reinterpret_cast<const uint8_t*>(debug_loc_section.Data.data()),
      debug_loc_section.Data.size());

  // Interpret the resulting list.
  auto base_address = const_cast<llvm::DWARFUnit*>(unit)->getBaseAddress();
  return DecodeLocationList(base_address ? base_address->Address : 0, section_data.subview(offset));
}

VariableLocation DecodeLocationList(TargetPointer unit_base_addr,
                                    containers::array_view<uint8_t> data) {
  DataExtractor ext(data);
  std::vector<VariableLocation::Entry> entries;

  // These location list begin and end values are "relative to the applicable base address of the
  // compilation unit referencing this location list."
  //
  // The "applicable base address" is either the unit's base address, or, if there was a "base
  // address selection entry", the nearest preceeding one.
  //
  // This value tracks the current applicable base address.
  TargetPointer base_address = unit_base_addr;

  // Base address selection entries are identifier by a start address with the max value.
  constexpr TargetPointer kBaseAddressSelectionTag = std::numeric_limits<TargetPointer>::max();

  while (!ext.done()) {
    auto begin = ext.Read<TargetPointer>();
    if (!begin)
      return VariableLocation();

    auto end = ext.Read<TargetPointer>();
    if (!end)
      return VariableLocation();

    if (*begin == kBaseAddressSelectionTag) {
      // New base address, read it and we're done with this entry.
      base_address = *end;
      continue;
    }
    if (*begin == 0 && *end == 0)
      break;  // End-of-list entry.

    // Non-"base address selection entries" are followed by a 2-byte length, followed by the
    // DWARF expression of that length.
    auto expression_len = ext.Read<uint16_t>();
    if (!expression_len)
      return VariableLocation();

    // Empty expressions are valid and just mean to skip this entry, so we don't bother adding it.
    if (*expression_len == 0)
      continue;

    // Expression data.
    std::vector<uint8_t> expression;
    expression.resize(*expression_len);
    if (!ext.ReadBytes(*expression_len, &expression[0]))
      return VariableLocation();

    if (*begin == *end)
      continue;  // Empty range, don't bother adding.

    VariableLocation::Entry& dest = entries.emplace_back();
    dest.begin = base_address + *begin;
    dest.end = base_address + *end;
    dest.expression = std::move(expression);
  }

  return VariableLocation(std::move(entries));
}

}  // namespace zxdb
