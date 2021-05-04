// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Undefined values will be null.
const char* kDwarfTagNames[] = {
    "<none>",                           // 0x00
    "DW_TAG_array_type",                // 0x01
    "DW_TAG_class_type",                // 0x02
    "DW_TAG_entry_point",               // 0x03
    "DW_TAG_enumeration_type",          // 0x04
    "DW_TAG_formal_parameter",          // 0x05
    nullptr,                            // 0x06
    nullptr,                            // 0x07
    "DW_TAG_imported_declaration",      // 0x08
    nullptr,                            // 0x09
    "DW_TAG_label",                     // 0x0a
    "DW_TAG_lexical_block",             // 0x0b
    nullptr,                            // 0x0c
    "DW_TAG_member",                    // 0x0d
    nullptr,                            // 0x0e
    "DW_TAG_pointer_type",              // 0x0f
    "DW_TAG_reference_type",            // 0x10
    "DW_TAG_compile_unit",              // 0x11
    "DW_TAG_string_type",               // 0x12
    "DW_TAG_structure_type",            // 0x13
    nullptr,                            // 0x14
    "DW_TAG_subroutine_type",           // 0x15
    "DW_TAG_typedef",                   // 0x16
    "DW_TAG_union_type",                // 0x17
    "DW_TAG_unspecified_parameters",    // 0x18
    "DW_TAG_variant",                   // 0x19
    "DW_TAG_common_block",              // 0x1a
    "DW_TAG_common_inclusion",          // 0x1b
    "DW_TAG_inheritance",               // 0x1c
    "DW_TAG_inlined_subroutine",        // 0x1d
    "DW_TAG_module",                    // 0x1e
    "DW_TAG_ptr_to_member_type",        // 0x1f
    "DW_TAG_set_type",                  // 0x20
    "DW_TAG_subrange_type",             // 0x21
    "DW_TAG_with_stmt",                 // 0x22
    "DW_TAG_access_declaration",        // 0x23
    "DW_TAG_base_type",                 // 0x24
    "DW_TAG_catch_block",               // 0x25
    "DW_TAG_const_type",                // 0x26
    "DW_TAG_constant",                  // 0x27
    "DW_TAG_enumerator",                // 0x28
    "DW_TAG_file_type",                 // 0x29
    "DW_TAG_friend",                    // 0x2a
    "DW_TAG_namelist",                  // 0x2b
    "DW_TAG_namelist_item",             // 0x2c
    "DW_TAG_packed_type",               // 0x2d
    "DW_TAG_subprogram",                // 0x2e
    "DW_TAG_template_type_parameter",   // 0x2f
    "DW_TAG_template_value_parameter",  // 0x30
    "DW_TAG_thrown_type",               // 0x31
    "DW_TAG_try_block",                 // 0x32
    "DW_TAG_variant_part",              // 0x33
    "DW_TAG_variable",                  // 0x34
    "DW_TAG_volatile_type",             // 0x35
    "DW_TAG_dwarf_procedure",           // 0x36
    "DW_TAG_restrict_type",             // 0x37
    "DW_TAG_interface_type",            // 0x38
    "DW_TAG_namespace",                 // 0x39
    "DW_TAG_imported_module",           // 0x3a
    "DW_TAG_unspecified_type",          // 0x3b
    "DW_TAG_partial_unit",              // 0x3c
    "DW_TAG_imported_unit",             // 0x3d
    nullptr,                            // 0x3e
    "DW_TAG_condition",                 // 0x3f
    "DW_TAG_shared_type",               // 0x40
    "DW_TAG_type_unit",                 // 0x41
    "DW_TAG_rvalue_reference_type",     // 0x42
    "DW_TAG_template_alias",            // 0x43
    "DW_TAG_coarray_type",              // 0x44
    "DW_TAG_generic_subrange",          // 0x45
    "DW_TAG_dynamic_type",              // 0x46
    "DW_TAG_atomic_type",               // 0x47
    "DW_TAG_call_site",                 // 0x48
    "DW_TAG_call_site_parameter",       // 0x49
    "DW_TAG_skeleton_unit",             // 0x4a
    "DW_TAG_immutable_type",            // 0x4b
};

constexpr size_t kDwarfTagNameCount = static_cast<size_t>(DwarfTag::kLastDefined);
static_assert(std::size(kDwarfTagNames) == kDwarfTagNameCount,
              "Update DWARF tag name array for changes to enum.");

}  // namespace

bool DwarfTagIsType(DwarfTag tag) {
  return DwarfTagIsTypeModifier(tag) || tag == DwarfTag::kArrayType || tag == DwarfTag::kBaseType ||
         tag == DwarfTag::kClassType || tag == DwarfTag::kEnumerationType ||
         tag == DwarfTag::kPtrToMemberType || tag == DwarfTag::kStringType ||
         tag == DwarfTag::kStructureType || tag == DwarfTag::kSubroutineType ||
         tag == DwarfTag::kUnionType || tag == DwarfTag::kUnspecifiedType;

  // Types in languages we ignore for now:
  //   kInterfaceType
  //   kSetType
  //   kSharedType
  //   kPackedType
  //   kFileType
}

bool DwarfTagIsTypeModifier(DwarfTag tag) {
  return tag == DwarfTag::kConstType || tag == DwarfTag::kPointerType ||
         tag == DwarfTag::kReferenceType || tag == DwarfTag::kRestrictType ||
         tag == DwarfTag::kRvalueReferenceType || tag == DwarfTag::kTypedef ||
         tag == DwarfTag::kVolatileType || tag == DwarfTag::kImportedDeclaration ||
         tag == DwarfTag::kAtomicType;
}

bool DwarfTagIsCVQualifier(DwarfTag tag) {
  return tag == DwarfTag::kConstType || tag == DwarfTag::kVolatileType ||
         tag == DwarfTag::kRestrictType || tag == DwarfTag::kAtomicType;
}

bool DwarfTagIsEitherReference(DwarfTag tag) {
  return tag == DwarfTag::kReferenceType || tag == DwarfTag::kRvalueReferenceType;
}

bool DwarfTagIsPointerOrReference(DwarfTag tag) {
  return DwarfTagIsEitherReference(tag) || tag == DwarfTag::kPointerType;
}

std::string DwarfTagToString(DwarfTag tag, bool include_number) {
  unsigned int_tag = static_cast<unsigned>(tag);
  if (int_tag < kDwarfTagNameCount && kDwarfTagNames[int_tag]) {
    if (include_number)
      return fxl::StringPrintf("%s (0x%02x)", kDwarfTagNames[int_tag], int_tag);
    return kDwarfTagNames[int_tag];
  }
  return fxl::StringPrintf("<undefined (0x%x)>", static_cast<unsigned>(int_tag));
}

}  // namespace zxdb
