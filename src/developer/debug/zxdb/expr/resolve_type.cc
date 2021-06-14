// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_type.h"

#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

fxl::RefPtr<Type> GetConcreteType(const FindNameContext& find_name_context, const Type* type) {
  if (!type)
    return fxl::RefPtr<Type>();

  // Iteratively strip C-V qualifications, follow typedefs, and follow forward
  // declarations.
  fxl::RefPtr<Type> cur = RefPtrTo(type);
  do {
    // Follow forward declarations.
    if (cur->is_declaration()) {
      cur = FindTypeDefinition(find_name_context, cur.get());
      if (cur->is_declaration())
        break;  // Declaration can't be resolved, give up.
    }

    // Strip C-V qualifiers and follow typedefs.
    cur = RefPtrTo(cur->StripCVT());
  } while (cur && cur->is_declaration());
  return cur;
}

fxl::RefPtr<Type> GetConcreteType(const FindNameContext& context, const LazySymbol& symbol) {
  if (!symbol)
    return fxl::RefPtr<Type>();

  const Type* type = symbol.Get()->AsType();
  if (!type)
    return fxl::RefPtr<Type>();

  return GetConcreteType(context, type);
}

fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, const Type* type) {
  Identifier ident = type->GetIdentifier();
  if (ident.empty()) {
    // Some things like modified types don't have real identifier names.
    return RefPtrTo(type);
  }

  fxl::RefPtr<Type> result = FindTypeDefinition(context, ToParsedIdentifier(ident));
  if (result)
    return result;
  return RefPtrTo(type);  // Return the same input on failure.
}

fxl::RefPtr<Type> FindTypeDefinition(const FindNameContext& context, ParsedIdentifier looking_for) {
  // Search for the first match of a type definition. Note that "find_types" is not desirable here
  // since we only want to resolve real definitions. Normally the index contains only definitions
  // but if a module contains only declarations that module's index will list the symbol as a
  // declaration which we don't want.
  FindNameOptions opts(FindNameOptions::kNoKinds);
  opts.find_type_defs = true;
  opts.max_results = 1;

  // The type names will always be fully qualified. Mark the identifier as such and only search the
  // global context by clearing the code location.
  looking_for.set_qualification(IdentifierQualification::kGlobal);

  // The input type name should be fully qualified so explicitly clear out any current code block to
  // bypass relative searching. This should be a no-op since the name is globally qualified, but
  // saves a little work later.
  auto use_context = context;
  use_context.block = nullptr;

  if (FoundName result = FindName(use_context, opts, looking_for)) {
    FX_DCHECK(result.type());
    return result.type();
  }
  return nullptr;
}

}  // namespace zxdb
