// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/function.h"

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"

namespace zxdb {

Function::Function() : CodeBlock(Symbol::kTagSubprogram) {}
Function::~Function() = default;

const Function* Function::AsFunction() const { return this; }

std::string Function::ComputeFullName() const {
  // TODO(brettw) add parameter types, at least for C++, since this is part of
  // the function type signature.
  //
  // This doesn't show the return types because C++ can't overload based on
  // return types so they're not ambiguous (and add noise). Neither GDB nor
  // LLDB shows the function return types normally.
  return GetSymbolScopePrefix(this) + GetAssignedName() + "()";
}

}  // namespace zxdb
