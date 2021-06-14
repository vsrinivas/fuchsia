// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BUILTIN_TYPES_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BUILTIN_TYPES_H_

#include <string_view>

#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

// Looks up the given type name. If it is a known builtin type name, a symbol defining that type
// will be returned. Otherwise an empty refptr will be returned.
//
// "void" is a special case: it will be represetned as a kBaseTypeNone variant of a base type
// (normally DWARF would represent void as the absence of a type, but that's not possible here.
fxl::RefPtr<BaseType> GetBuiltinType(ExprLanguage lang, std::string_view name);

// These will always return a type of rhte given size. If the language doesn't have a built-in for
// the name, one will be made up.
fxl::RefPtr<BaseType> GetBuiltinUnsignedType(ExprLanguage lang, size_t byte_size);
fxl::RefPtr<BaseType> GetBuiltinFloatType(ExprLanguage lang, size_t byte_size);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BUILTIN_TYPES_H_
