// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/builtin_types.h"

#include <lib/syslog/cpp/macros.h>

#include <map>

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

    // This void type is a bit weird because the way that "void" is represented in DWARF is just by
    // an absence of a type. But we can't really return that here. So we return it as a base type of
    // "no base type".
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
    // Not technically defined in C but we need a name for 128-bit values.
    { "int128_t",  BaseType::kBaseTypeSigned,      16 },
    { "uint128_t", BaseType::kBaseTypeUnsigned,    16 },

    { "size_t",   BaseType::kBaseTypeUnsigned,     8 },
    { "ssize_t",  BaseType::kBaseTypeSigned,       8 },
    { "intptr_t", BaseType::kBaseTypeSigned,       8 },
    { "uintptr_t", BaseType::kBaseTypeUnsigned,    8 },

    // Special Zircon types (see note below).
    { "zx_status_t", BaseType::kBaseTypeSigned,    4 },

    // In C++, "auto" is not a type but rather a "placeholder type specifier" that the compiler
    // fills in for you in certain contexts. Our expression language is not statically typed so
    // it isn't possible to fill in at parse-time, which means we need a placeholder for these
    // auto types until they can be handled.
    //
    // So this is modeled as a "void". The code that can handle "auto" for variable declarations
    // will check for this name and fill it in. Having this as a type allows you to specify "auto"
    // as a type like "sizeof(auto)" which does not make sense in C++, but it's not too misleading.
    { "auto",      BaseType::kBaseTypeNone,        0 },

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
// define it as a typedef for simplicity, that could be added in the future if desired.

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
      FX_NOTREACHED();
      return nullptr;
  }

  auto found = map->find(name);
  if (found == map->end())
    return nullptr;

  const BuiltinTypeInfo& info = *found->second;
  return fxl::MakeRefCounted<BaseType>(info.base_type, info.byte_size, info.name);
}

fxl::RefPtr<BaseType> GetBuiltinUnsigned64Type(ExprLanguage lang) {
  switch (lang) {
    case ExprLanguage::kC:
      return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t");
    case ExprLanguage::kRust:
      return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "u64");
  }
  FX_NOTREACHED();
  return fxl::RefPtr<BaseType>();
}

fxl::RefPtr<BaseType> GetBuiltinUnsignedType(ExprLanguage lang, size_t byte_size) {
  switch (lang) {
    case ExprLanguage::kC:
      switch (byte_size) {
        case 1:
          return GetBuiltinType(lang, "uint8_t");
        case 2:
          return GetBuiltinType(lang, "uint16_t");
        case 4:
          return GetBuiltinType(lang, "uint32_t");
        case 8:
          return GetBuiltinType(lang, "uint64_t");
        case 16:
          return GetBuiltinType(lang, "uint128_t");
      }
      break;
    case ExprLanguage::kRust:
      switch (byte_size) {
        case 1:
          return GetBuiltinType(lang, "u8");
        case 2:
          return GetBuiltinType(lang, "u16");
        case 4:
          return GetBuiltinType(lang, "u32");
        case 8:
          return GetBuiltinType(lang, "u64");
        case 16:
          return GetBuiltinType(lang, "u128");
      }
      break;
  }

  // No builtin, in this case just make up a type.
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, byte_size,
                                       "nonstandard_unsigned");
}

fxl::RefPtr<BaseType> GetBuiltinFloatType(ExprLanguage lang, size_t byte_size) {
  switch (lang) {
    case ExprLanguage::kC:
      switch (byte_size) {
        case 4:
          return GetBuiltinType(lang, "float");
        case 8:
          return GetBuiltinType(lang, "double");
        case 10:
          return GetBuiltinType(lang, "long double");
      }
      break;
    case ExprLanguage::kRust:
      switch (byte_size) {
        case 4:
          return GetBuiltinType(lang, "f32");
        case 8:
          return GetBuiltinType(lang, "f64");
      }
      break;
  }

  // No builtin, in this case just make up a type.
  return fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, byte_size, "nonstandard_float");
}

}  // namespace zxdb
