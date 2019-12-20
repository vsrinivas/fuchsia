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
fxl::RefPtr<BaseType> GetBuiltinType(ExprLanguage lang, std::string_view name);

fxl::RefPtr<BaseType> GetBuiltinFloatType(ExprLanguage lang);
fxl::RefPtr<BaseType> GetBuiltinDoubleType(ExprLanguage lang);
fxl::RefPtr<BaseType> GetBuiltinLongDoubleType(ExprLanguage lang);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BUILTIN_TYPES_H_
