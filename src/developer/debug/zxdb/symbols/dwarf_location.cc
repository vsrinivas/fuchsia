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

namespace {

// Reads a DWARF 5 counted location description from the given data extractor. Returns nullopt on
// failure.
std::optional<DwarfExpr> ExtractCountedLocationDescription(DataExtractor& ext,
                                                           const UncachedLazySymbol& source) {
  // A counted location description consists of a ULEB128 integer giving the byte length of the
  // following location description.
  auto length_or = ext.ReadUleb128();
  if (!length_or)
    return std::nullopt;

  // Empty expressions are valid and just mean to skip this entry.
  if (*length_or == 0)
    return DwarfExpr({}, source);

  // Expression data.
  std::vector<uint8_t> expression;
  expression.resize(*length_or);
  if (!ext.ReadBytes(*length_or, expression.data()))
    return std::nullopt;

  return DwarfExpr(std::move(expression), source);
}

// Reads a DWARF 5 counted location description from the given data extractor, and appends it
// for the given range to the given output vector. Returns true on success.
bool AppendCountedLocationDescriptionEntry(TargetPointer begin, TargetPointer end,
                                           DataExtractor& ext,
                                           std::vector<VariableLocation::Entry>& output_list,
                                           const UncachedLazySymbol& source) {
  std::optional<DwarfExpr> expr = ExtractCountedLocationDescription(ext, source);
  if (!expr)
    return false;

  // Empty expressions are valid and just mean to skip this entry, so we don't bother adding it.
  if (expr->data().empty())
    return true;

  // Skip invalid ranges and don't bother adding empty ranges. Check this after reading the
  // expression to advance the data extractor.
  if (begin >= end)
    return true;

  VariableLocation::Entry& dest = output_list.emplace_back();
  dest.range = AddressRange(begin, end);
  dest.expression = std::move(*expr);
  return true;
}

// Decodes a reference to a DWARF 4 location list. This is a DW_FORM_sec_offset type into the
// .debug_loc section that contains the location list.
VariableLocation DecodeDwarf4LocationReference(const llvm::DWARFUnit* unit,
                                               const llvm::DWARFFormValue& form,
                                               const UncachedLazySymbol& source) {
  llvm::DWARFContext& context = unit->getContext();
  const llvm::DWARFObject& object = context.getDWARFObj();
  const llvm::DWARFSection& debug_loc_section = object.getLocSection();
  if (debug_loc_section.Data.empty()) {
    // LLVM dumpLocation() falls back on the DWARFObject::getLocDWOSection() call in this case. We
    // don't support DWOs yet so just fail.
    return VariableLocation();
  }

  auto offset_or = form.getAsSectionOffset();
  if (!offset_or)
    return VariableLocation();

  if (*offset_or >= debug_loc_section.Data.size())
    return VariableLocation();  // Off the end.

  containers::array_view<uint8_t> section_data(
      reinterpret_cast<const uint8_t*>(debug_loc_section.Data.data()),
      debug_loc_section.Data.size());

  // Interpret the resulting list.
  auto base_address = const_cast<llvm::DWARFUnit*>(unit)->getBaseAddress();
  return DecodeDwarf4LocationList(base_address ? base_address->Address : 0,
                                  section_data.subview(*offset_or), source);
}

// Decodes a DWARF 5 location which is a DW_FORM_loclistx-type offset into the .debug_loclist
// section that contains the location list.
VariableLocation DecodeDwarf5LocationReference(llvm::DWARFUnit* unit,
                                               const llvm::DWARFFormValue& form,
                                               const UncachedLazySymbol& source) {
  llvm::DWARFContext& context = unit->getContext();
  const llvm::DWARFObject& object = context.getDWARFObj();
  const llvm::DWARFSection& debug_loclists_section = object.getLoclistsSection();
  if (debug_loclists_section.Data.empty()) {
    // LLVM dumpLocation() falls back on the DWARFObject::getLocDWOSection() call in this case. We
    // don't support DWOs yet so just fail.
    return VariableLocation();
  }

  // Compute the byte offset into the .debug_loclists section of the location list.
  uint64_t list_offset = 0;
  switch (form.getForm()) {
    case llvm::dwarf::DW_FORM_sec_offset: {
      // A byte offset into the .debug_loclists section.
      list_offset = *form.getAsSectionOffset();
      break;
    }

    case llvm::dwarf::DW_FORM_loclistx: {
      // The form value is an index into the unit's location list table.
      uint64_t list_index = *form.getAsSectionOffset();

      // The unit's DW_AT_loclists_base attribute specifies the byte offset within the
      // .debug_loclists section of the unit's location list table. The getLoclistOffset() call
      // combines the list_index and the unit's table offset, and reads the resulting offset to get
      // the the byte offset of the location list we want relative to the .debug_loclists section.
      auto list_offset_or = unit->getLoclistOffset(list_index);
      if (!list_offset_or)
        return VariableLocation();

      list_offset = *list_offset_or;
      break;
    }

    default: {
      // Unsupported.
      return VariableLocation();
    }
  }

  containers::array_view<uint8_t> section_data(
      reinterpret_cast<const uint8_t*>(debug_loclists_section.Data.data()),
      debug_loclists_section.Data.size());

  // Callback that converts an index into the .debug_addr table into the corresponding address.
  fit::function<std::optional<TargetPointer>(uint64_t)> index_to_addr =
      [unit](uint64_t index) -> std::optional<TargetPointer> {
    auto sectioned_addr_or = unit->getAddrOffsetSectionItem(index);
    if (!sectioned_addr_or)
      return std::nullopt;

    // We do not support any platforms with segmented addresses so only return the regular address.
    return sectioned_addr_or->Address;
  };

  auto base_address = const_cast<llvm::DWARFUnit*>(unit)->getBaseAddress();
  return DecodeDwarf5LocationList(base_address ? base_address->Address : 0,
                                  section_data.subview(list_offset), index_to_addr, source);
}

}  // namespace

VariableLocation DecodeVariableLocation(llvm::DWARFUnit* unit, const llvm::DWARFFormValue& form,
                                        const UncachedLazySymbol& source) {
  if (form.isFormClass(llvm::DWARFFormValue::FC_Block) ||
      form.isFormClass(llvm::DWARFFormValue::FC_Exprloc)) {
    // These forms are both a block of data which is interpreted as a DWARF expression. There is no
    // validity range for this so assume the expression is valid as long as the variable is in
    // scope.
    llvm::ArrayRef<uint8_t> block = *form.getAsBlock();
    return VariableLocation(DwarfExpr(std::vector<uint8_t>(block.begin(), block.end()), source));
  }

  if (unit->getVersion() < 5) {
    // DWARF 4 location list.
    return DecodeDwarf4LocationReference(unit, form, source);
  }

  // Assume everything newer is a DWARF 5 location list.
  return DecodeDwarf5LocationReference(unit, form, source);
}

VariableLocation DecodeDwarf4LocationList(TargetPointer unit_base_addr,
                                          containers::array_view<uint8_t> data,
                                          const UncachedLazySymbol& source) {
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
    if (!ext.ReadBytes(*expression_len, expression.data()))
      return VariableLocation();

    if (*begin >= *end)
      continue;  // Invalid or empty range, don't add/

    VariableLocation::Entry& dest = entries.emplace_back();
    dest.range = AddressRange(base_address + *begin, base_address + *end);
    dest.expression = DwarfExpr(std::move(expression), source);
  }

  return VariableLocation(std::move(entries));
}

VariableLocation DecodeDwarf5LocationList(
    TargetPointer unit_base_addr, containers::array_view<uint8_t> data,
    fit::function<std::optional<TargetPointer>(uint64_t)>& index_to_addr,
    const UncachedLazySymbol& source) {
  DataExtractor ext(data);
  std::vector<VariableLocation::Entry> entries;

  // The offset_pair type uses addresses relative to the closest preceding base address in the same
  // location lis. It defaults to the compilation unit's base address if there is no explicit one.
  //
  // This value tracks the current applicable base address.
  TargetPointer base_address = unit_base_addr;

  // The default location expression, if found.
  std::optional<DwarfExpr> default_expr;

  while (!ext.done()) {
    // The first byte of the location list entry is the entry kind.
    auto type_or = ext.Read<uint8_t>();
    if (!type_or)
      return VariableLocation();

    switch (*type_or) {
      case llvm::dwarf::DW_LLE_end_of_list: {
        return VariableLocation(std::move(entries), std::move(default_expr));
      }

      case llvm::dwarf::DW_LLE_base_address: {
        // One target address operand that indicates the new base address.
        auto new_base_or = ext.Read<TargetPointer>();
        if (!new_base_or)
          return VariableLocation();

        base_address = *new_base_or;
        break;
      }

      case llvm::dwarf::DW_LLE_base_addressx: {
        // Like base_address but the operand us a ULEB128 index into the .debug_addr section
        // that indicates the new base address.
        auto new_base_i_or = ext.ReadUleb128();
        if (!new_base_i_or)
          return VariableLocation();

        // Convert index to address.
        auto new_base_or = index_to_addr(*new_base_i_or);
        if (!new_base_or)
          return VariableLocation();

        base_address = *new_base_or;
        break;
      }

      case llvm::dwarf::DW_LLE_start_end: {
        // Two target address operands of the start and end address of the entry. Followed by a
        // counted location description for that range.
        auto start_or = ext.Read<TargetPointer>();
        auto end_or = ext.Read<TargetPointer>();
        if (!start_or || !end_or ||
            !AppendCountedLocationDescriptionEntry(*start_or, *end_or, ext, entries, source))
          return VariableLocation();
        break;
      }

      case llvm::dwarf::DW_LLE_startx_endx: {
        // Like start_end but the two operands are ULEB128 indices into the .debug_addr section that
        // indicates the start and end addresses of the entry.
        auto start_i_or = ext.ReadUleb128();
        auto end_i_or = ext.ReadUleb128();
        if (!start_i_or || !end_i_or)
          return VariableLocation();

        // Convert indices to addresses.
        auto start_or = index_to_addr(*start_i_or);
        auto end_or = index_to_addr(*end_i_or);

        if (!start_or || !end_or ||
            !AppendCountedLocationDescriptionEntry(*start_or, *end_or, ext, entries, source))
          return VariableLocation();
        break;
      }

      case llvm::dwarf::DW_LLE_start_length: {
        // One target address operand of the start, and a ULEB length that indicates the address
        // range of the entry. Followed by a counted location description.
        auto start_or = ext.Read<TargetPointer>();
        auto length_or = ext.ReadUleb128();
        if (!start_or || !length_or ||
            !AppendCountedLocationDescriptionEntry(*start_or, *start_or + *length_or, ext, entries,
                                                   source))
          return VariableLocation();
        break;
      }

      case llvm::dwarf::DW_LLE_startx_length: {
        // Like start_length but the first operand is a ULEB128 index into the .debug_addr section.
        auto start_i_or = ext.ReadUleb128();
        if (!start_i_or)
          return VariableLocation();

        // Convert index to address.
        auto start_or = index_to_addr(*start_i_or);

        auto length_or = ext.ReadUleb128();
        if (!start_or || !length_or ||
            !AppendCountedLocationDescriptionEntry(*start_or, *start_or + *length_or, ext, entries,
                                                   source))
          return VariableLocation();
        break;
      }

      case llvm::dwarf::DW_LLE_offset_pair: {
        // Two ULEB128 operands indicating offsets from the base_address of the range of the entry.
        // Followed by a counted location description.
        auto start_off_or = ext.ReadUleb128();
        auto end_off_or = ext.ReadUleb128();
        if (!start_off_or || !end_off_or ||
            !AppendCountedLocationDescriptionEntry(
                base_address + *start_off_or, base_address + *end_off_or, ext, entries, source))
          return VariableLocation();
        break;
      }

      case llvm::dwarf::DW_LLE_default_location: {
        // A counted location description that applies when no other ranges do.
        default_expr = ExtractCountedLocationDescription(ext, source);
        if (!default_expr)
          return VariableLocation();  // Default expression was corrupt.
        break;
      }
    }
  }

  // Got to the end of the data without seeing an end-of-list marker, declare corrupt.
  return VariableLocation();
}

}  // namespace zxdb
