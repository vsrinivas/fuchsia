// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_variant.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

Err ResolveVariant(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                   const Collection* collection, const VariantPart* variant_part,
                   fxl::RefPtr<Variant>* result) {
  // Resolve the discriminant value. It is effectively a member of the enclosing structure.
  const DataMember* discr_member = variant_part->discriminant().Get()->AsDataMember();
  if (!discr_member)
    return Err("Missing discriminant for variant.");

  // Variants don't have static variant members or virtual inheritance.
  ErrOrValue discr_value =
      ResolveNonstaticMember(context, value, FoundMember(collection, discr_member));
  if (discr_value.has_error())
    return discr_value.err();

  // Expect the discriminant value to resolve to a <= 64-bit number.
  //
  // NOTE: there is some trickery with signed/unsigned values as described in the
  // Variant.discr_value() getter. If we need to support signed discriminants this block will have
  // to be updated.
  uint64_t discr = 0;
  if (Err err = discr_value.value().PromoteTo64(&discr); err.has_error())
    return err;

  // Check against all variants and also look for the default variant.
  const Variant* default_var = nullptr;
  for (const auto& lazy_var : variant_part->variants()) {
    const Variant* var = lazy_var.Get()->AsVariant();
    if (!var)
      continue;

    if (var->discr_value()) {
      if (*var->discr_value() == discr) {
        // Found match.
        *result = RefPtrTo(var);
        return Err();
      }
    } else {
      // No discriminant value set on the variant means it's the default one.
      default_var = var;
    }
  }

  // No match means use the default value if there is one.
  if (default_var) {
    *result = RefPtrTo(default_var);
    return Err();
  }

  return Err("Discriminant value of 0x%" PRIx64 " does not match any of the Variants.", discr);
}

}  // namespace zxdb
