// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/function.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

Function::Function(DwarfTag tag) : CodeBlock(tag) {
  FX_DCHECK(tag == DwarfTag::kSubprogram || tag == DwarfTag::kInlinedSubroutine);
}

Function::~Function() = default;

fxl::RefPtr<CodeBlock> Function::GetContainingBlock() const {
  fxl::RefPtr<Symbol> containing;
  if (containing_block()) {
    // Use the manually set containing block if specified.
    containing = containing_block().Get();
  } else {
    // Fall back on the parent if no containing block is explicitly set.
    containing = parent().Get();
  }
  return RefPtrTo(containing->As<CodeBlock>());
}

const Function* Function::AsFunction() const { return this; }

const Variable* Function::GetObjectPointerVariable() const {
  // See the header comment for background. This function finds the name of the
  // object pointer variable, and then looks it up by name in the parameter
  // list. This avoids the problem of abstract bases for inlined functions.
  if (!object_pointer())
    return nullptr;  // No object pointer on this function.

  const Variable* var = object_pointer().Get()->As<Variable>();
  if (!var)
    return nullptr;  // Symbols corrupt.
  const std::string& name = var->GetAssignedName();

  // Check the parameters for a var with the same name.
  for (const auto& lazy_param : parameters()) {
    const Variable* param = lazy_param.Get()->As<Variable>();
    if (!param)
      continue;  // This symbol is corrupt.
    if (param->GetAssignedName() == name)
      return param;  // Found match.
  }

  // No match in the parameters list found, assume the original referenced
  // variable is correct.
  return var;
}

Identifier Function::ComputeIdentifier() const {
  Identifier result = GetSymbolScopePrefix(this);
  result.AppendComponent(IdentifierComponent(GetAssignedName()));
  return result;
}

}  // namespace zxdb
