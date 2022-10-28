// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_const_value.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/value.h"

namespace zxdb {

// TESTING NOTE: This code is tested by the collection and value resolution tests (the places
// where const values are converted to ExprValues).
ErrOrValue ResolveConstValue(const fxl::RefPtr<EvalContext>& context, const Value* value) {
  FX_DCHECK(value->const_value().has_value());

  // Need to keep the original (possibly non-concrete) type to assign as the type of the result.
  const Type* type = value->type().Get()->As<Type>();
  if (!type)
    return Err("Invalid type for '%s'.", value->GetFullName().c_str());
  auto concrete = context->GetConcreteType(type);

  return ExprValue(RefPtrTo(type), value->const_value().GetConstValue(concrete->byte_size()),
                   ExprValueSource(ExprValueSource::Type::kConstant));
}

}  // namespace zxdb
