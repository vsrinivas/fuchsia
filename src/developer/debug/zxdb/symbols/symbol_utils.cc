// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_utils.h"

#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

Identifier GetSymbolScopePrefix(const Symbol* symbol) {
  if (!symbol->parent().is_valid())
    return Identifier(IdentifierQualification::kGlobal);  // No prefix

  fxl::RefPtr<Symbol> parent = symbol->parent().Get();
  if (parent->tag() == DwarfTag::kCompileUnit)
    return Identifier(IdentifierQualification::kGlobal);  // Don't go above compilation units.

  if (parent->AsNamespace() || parent->AsCollection() || parent->AsFunction()) {
    // These are the types that get qualified.
    return parent->GetIdentifier();
  }
  // Anything else is skipped and we just return the parent's prefix. This
  // will include things like lexical blocks.
  return GetSymbolScopePrefix(parent.get());
}

}  // namespace zxdb
