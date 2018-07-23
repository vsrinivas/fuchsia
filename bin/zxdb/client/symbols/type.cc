// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/type.h"

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"
#include "lib/fxl/logging.h"

namespace zxdb {

Type::Type(int kind) : Symbol(kind) {}
Type::~Type() = default;

const Type* Type::AsType() const { return this; }

const std::string& Type::GetTypeName() const {
  if (!computed_type_name_) {
    computed_type_name_ = true;
    type_name_ = ComputeTypeName();
  }
  return type_name_;
}

std::string Type::ComputeTypeName() const {
  // This base type class just uses the name for the type name. Derived classes
  // will override this function to apply modifiers.
  return GetSymbolScopePrefix(this) + GetAssignedName();
}

}  // namespace zxdb
