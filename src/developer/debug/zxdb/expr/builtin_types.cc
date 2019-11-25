// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/builtin_types.h"

#include <map>

#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

struct BuiltinTypeInfo {
  const char* name;
  uint32_t base_type;
  uint32_t byte_size;
};

// TODO(brettw) this needs to handle compound types like "unsigned short" and "signed int". Note
// that the modifiers can appear in different orders like "signed short int" vs. "short signed int",
// and can also have interspersed CV-modifiers like "short volatile signed const int".
const BuiltinTypeInfo kCBuiltinInfo[] = {
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

    // Special Zircon types (see note below).
    { "zx_status_t", BaseType::kBaseTypeSigned,    4 },

    // clang-format on
};

const BuiltinTypeInfo kRustBuiltinInfo[] = {
    // clang-format off

    { "bool",     BaseType::kBaseTypeBoolean,      1 },
    { "char",     BaseType::kBaseTypeUnsignedChar, 4 },

    // Integer types.
    { "i8",       BaseType::kBaseTypeSigned,       1 },
    { "u8",       BaseType::kBaseTypeUnsigned,     1 },
    { "i16",      BaseType::kBaseTypeSigned,       2 },
    { "u16",      BaseType::kBaseTypeUnsigned,     2 },
    { "i32",      BaseType::kBaseTypeSigned,       4 },
    { "u32",      BaseType::kBaseTypeUnsigned,     4 },
    { "i64",      BaseType::kBaseTypeSigned,       8 },
    { "u64",      BaseType::kBaseTypeUnsigned,     8 },
    { "i128",     BaseType::kBaseTypeSigned,       16 },
    { "u128",     BaseType::kBaseTypeUnsigned,     16 },
    { "isize",    BaseType::kBaseTypeSigned,       8 },  // 64-bit system.
    { "usize",    BaseType::kBaseTypeUnsigned,     8 },

    // Floating-point types.
    { "f32",      BaseType::kBaseTypeFloat,        4 },
    { "f64",      BaseType::kBaseTypeFloat,        8 },

    // clang-format on
};

// Note on zx_status_t: Normally this will be declared in the program as a typedef for an int32.
// Adding it here allows casting to it even if the typedef is not currently in scope, which in turn
// will trigger the special-cased pretty-printing to decode status values. This fallback doesn't
// define it as a typedef for simplicit, that could be added in the future if desired.

using BuiltinTypeInfoMap = std::map<std::string_view, const BuiltinTypeInfo*>;

const BuiltinTypeInfoMap* GetCBuiltinTypeMap() {
  static BuiltinTypeInfoMap map;
  if (map.empty()) {
    for (const auto& cur : kCBuiltinInfo)
      map[cur.name] = &cur;
  }
  return &map;
}

const BuiltinTypeInfoMap* GetRustBuiltinTypeMap() {
  static BuiltinTypeInfoMap map;
  if (map.empty()) {
    for (const auto& cur : kRustBuiltinInfo)
      map[cur.name] = &cur;
  }
  return &map;
}

}  // namespace

fxl::RefPtr<BaseType> GetBuiltinType(ExprLanguage lang, std::string_view name) {
  const BuiltinTypeInfoMap* map;
  switch (lang) {
    case ExprLanguage::kC:
      map = GetCBuiltinTypeMap();
      break;
    case ExprLanguage::kRust:
      map = GetRustBuiltinTypeMap();
      break;
    default:
      FXL_NOTREACHED();
      return nullptr;
  }

  auto found = map->find(name);
  if (found == map->end())
    return nullptr;

  const BuiltinTypeInfo& info = *found->second;
  return fxl::MakeRefCounted<BaseType>(info.base_type, info.byte_size, info.name);
}

}  // namespace zxdb
