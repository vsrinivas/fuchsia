// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"

#include "garnet/bin/zxdb/client/symbols/namespace.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/type.h"
#include "lib/fxl/logging.h"

namespace zxdb {

// Some things in this file currently hardcode C/C++ rules. To support other
// languages, we will need to extract the language from the compilation unit
// and dispatch to a different implementation.

std::string GetSymbolScopePrefix(const Symbol* symbol) {
  static const char kCppSeparator[] = "::";

  if (!symbol->parent().is_valid())
    return std::string();  // No prefix

  const Symbol* parent = symbol->parent().Get();
  if (parent->tag() == Symbol::kTagCompileUnit)
    return std::string();  // Don't go above compilation units.

  if (parent->AsNamespace() || parent->AsStructClass() ||
      parent->AsFunction()) {
    // These are the types that get qualified.
    return parent->GetFullName() + kCppSeparator;
  }
  // Anything else is skipped and we just return the parent's prefix. This
  // will include things like lexical blocks.
  return GetSymbolScopePrefix(parent);
}

}  // namespace zxdb
