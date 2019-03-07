// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/type_parser.h"

#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"

namespace zxdb {

// TODO(brettw) this is a placeholder with a few hardcoded types for testing.
// It needs a real implementation.
Err StringToType(const std::string& input, fxl::RefPtr<Type>* type) {
  if (input == "int") {
    *type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int");
    return Err();
  } else if (input == "char*") {
    auto char_type =
        fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
    *type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                              LazySymbol(char_type));
    return Err();
  } else if (input == "void*") {
    // A "void*" is a pointer modification of nothing.
    *type = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                              LazySymbol());
    return Err();
  }
  return Err("Unknown type (type parsing is a work in progress).");
}

}  // namespace zxdb
