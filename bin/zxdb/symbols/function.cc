// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/function.h"

#include "garnet/bin/zxdb/symbols/symbol_utils.h"

namespace zxdb {

Function::Function() : CodeBlock(Symbol::kTagSubprogram) {}
Function::~Function() = default;

const Function* Function::AsFunction() const { return this; }

std::string Function::ComputeFullName() const {
  return GetSymbolScopePrefix(this) + GetAssignedName();
}

}  // namespace zxdb
