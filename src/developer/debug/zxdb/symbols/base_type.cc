// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/base_type.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

// Storage for constants.
const int BaseType::kBaseTypeNone;
const int BaseType::kBaseTypeAddress;
const int BaseType::kBaseTypeBoolean;
const int BaseType::kBaseTypeFloat;
const int BaseType::kBaseTypeSigned;
const int BaseType::kBaseTypeSignedChar;
const int BaseType::kBaseTypeUnsigned;
const int BaseType::kBaseTypeUnsignedChar;
const int BaseType::kBaseTypeUTF;

BaseType::BaseType() : Type(DwarfTag::kBaseType) {}

BaseType::BaseType(int base_type, uint32_t byte_size, const std::string& name)
    : Type(DwarfTag::kBaseType), base_type_(base_type) {
  set_byte_size(byte_size);
  set_assigned_name(name);
}

BaseType::~BaseType() = default;

const BaseType* BaseType::AsBaseType() const { return this; }

// static
std::string BaseType::BaseTypeToString(int base_type, bool include_number) {
  const char* name = nullptr;
  switch (base_type) {
      // clang-format off
    case kBaseTypeNone:         name = "<none>";               break;
    case kBaseTypeAddress:      name = "DW_ATE_address";       break;
    case kBaseTypeBoolean:      name = "DW_ATE_boolean";       break;
    case kBaseTypeFloat:        name = "DW_ATE_float";         break;
    case kBaseTypeSigned:       name = "DW_ATE_signed";        break;
    case kBaseTypeSignedChar:   name = "DW_ATE_signed_char";   break;
    case kBaseTypeUnsigned:     name = "DW_ATE_unsigned";      break;
    case kBaseTypeUnsignedChar: name = "DW_ATE_unsigned_char"; break;
    case kBaseTypeUTF:          name = "DW_ATE_UTF";           break;
    // clang-format on
    default:
      // Always print the number for unknown names.
      return fxl::StringPrintf("<undefined (0x%02x)>", static_cast<unsigned>(base_type));
  }
  if (!include_number)
    return name;
  return fxl::StringPrintf("%s (0x%02x)", name, static_cast<unsigned>(base_type));
}

// static
bool BaseType::IsSigned(int base_type) {
  return base_type == BaseType::kBaseTypeSigned || base_type == kBaseTypeSignedChar;
}

const std::string& BaseType::GetAssignedName() const {
  const std::string& assigned_name = Type::GetAssignedName();
  if (!assigned_name.empty())
    return assigned_name;

  // Special-case void types with no assigned names.
  if (base_type_ == kBaseTypeNone) {
    static std::string void_str("void");
    return void_str;
  }

  return assigned_name;
}

}  // namespace zxdb
