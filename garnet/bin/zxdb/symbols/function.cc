// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/function.h"

#include "garnet/bin/zxdb/symbols/symbol_utils.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

Function::Function(DwarfTag tag) : CodeBlock(tag) {
  FXL_DCHECK(tag == DwarfTag::kSubprogram ||
             tag == DwarfTag::kInlinedSubroutine);
}

Function::~Function() = default;

const Function* Function::AsFunction() const { return this; }

const Variable* Function::GetObjectPointerVariable() const {
  // See the header comment for background. This function finds the name of the
  // object pointer variable, and then looks it up by name in the parameter
  // list. This avoids the problem of abstract bases for inlined functions.
  if (!object_pointer())
    return nullptr;  // No object pointer on this function.

  const Variable* var = object_pointer().Get()->AsVariable();
  if (!var)
    return nullptr;  // Symbols corrupt.
  const std::string& name = var->GetAssignedName();

  // Check the parameters for a var with the same name.
  for (const auto& lazy_param : parameters()) {
    const Variable* param = lazy_param.Get()->AsVariable();
    if (!param)
      continue;  // This symbol is corrupt.
    if (param->GetAssignedName() == name)
      return param;  // Found match.
  }

  // No match in the parameters list found, assume the original referenced
  // variable is correct.
  return var;
}

std::string Function::ComputeFullName() const {
  return GetSymbolScopePrefix(this) + GetAssignedName();
}

}  // namespace zxdb
