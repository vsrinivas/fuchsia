// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_utils.h"

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

// Some things in this file currently hardcode C/C++ rules. To support other
// languages, we will need to extract the language from the compilation unit
// and dispatch to a different implementation.

std::string GetSymbolScopePrefix(const Symbol* symbol) {
  static const char kCppSeparator[] = "::";

  if (!symbol->parent().is_valid())
    return std::string();  // No prefix

  const Symbol* parent = symbol->parent().Get();
  if (parent->tag() == DwarfTag::kCompileUnit)
    return std::string();  // Don't go above compilation units.

  if (parent->AsNamespace() || parent->AsCollection() || parent->AsFunction()) {
    // These are the types that get qualified.
    return parent->GetFullName() + kCppSeparator;
  }
  // Anything else is skipped and we just return the parent's prefix. This
  // will include things like lexical blocks.
  return GetSymbolScopePrefix(parent);
}

}  // namespace zxdb
