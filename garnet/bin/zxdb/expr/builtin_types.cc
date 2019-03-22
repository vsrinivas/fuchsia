// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/builtin_types.h"

#include <map>

namespace zxdb {

namespace {

struct BuiltinTypeInfo {
  const char* name;
  uint32_t base_type;
  uint32_t byte_size;
};

// TODO(brettw) this needs to handle compound types like "unsigned short"
// and "signed int". Note that the modifiers can appear in different orders
// like "signed short int" vs. "short signed int", and can also have
// intersperced CV-modifiers like "short volatile signed const int".
const BuiltinTypeInfo kBuiltinInfo[] = {
    // clang-format off

    { "void",     BaseType::kBaseTypeNone,         0 },
    { "bool",     BaseType::kBaseTypeBoolean,      1 },

    // Integer types.
    { "short",    BaseType::kBaseTypeSigned,       2 },  // TODO: [un]signed
    { "int",      BaseType::kBaseTypeSigned,       4 },  // TODO: [un]signed, long/short/"long long"
    { "unsigned", BaseType::kBaseTypeUnsigned,     4 },
    { "long",     BaseType::kBaseTypeSigned,       8 },  // TODO: [un]signed, "long long"

    // Floating-point types.
    { "float",    BaseType::kBaseTypeFloat,        4 },
    { "double",   BaseType::kBaseTypeFloat,        8 },  // TODO: "long double"

    // Character types.
    { "char",     BaseType::kBaseTypeSignedChar,   1 },  // TODO: [un]signed
    { "wchar_t",  BaseType::kBaseTypeSigned,       4 },  // TODO: [un]signed
    { "char8_t",  BaseType::kBaseTypeUTF,          1 },
    { "char16_t", BaseType::kBaseTypeUTF,          2 },
    { "char32_t", BaseType::kBaseTypeUTF,          4 },

    // Main stdint types (not technically built-in, but commonly needed).
    { "int8_t",   BaseType::kBaseTypeSignedChar,   1 },
    { "uint8_t",  BaseType::kBaseTypeUnsignedChar, 1 },
    { "int16_t",  BaseType::kBaseTypeSigned,       2 },
    { "uint16_t", BaseType::kBaseTypeUnsigned,     2 },
    { "int32_t",  BaseType::kBaseTypeSigned,       4 },
    { "uint32_t", BaseType::kBaseTypeUnsigned,     4 },
    { "int64_t",  BaseType::kBaseTypeSigned,       8 },
    { "uint64_t", BaseType::kBaseTypeUnsigned,     8 },

    // clang-format on
};

using BuiltinTypeInfoMap = std::map<std::string_view, const BuiltinTypeInfo*>;

const BuiltinTypeInfoMap& GetBuiltinTypeMap() {
  static BuiltinTypeInfoMap map;
  if (map.empty()) {
    for (const auto& cur : kBuiltinInfo)
      map[cur.name] = &cur;
  }
  return map;
}

}  // namespace

fxl::RefPtr<BaseType> GetBuiltinType(std::string_view name) {
  const auto& map = GetBuiltinTypeMap();

  auto found = map.find(name);
  if (found == map.end())
    return nullptr;

  const BuiltinTypeInfo& info = *found->second;
  return fxl::MakeRefCounted<BaseType>(info.base_type, info.byte_size, info.name);
}

}  // namespace zxdb
