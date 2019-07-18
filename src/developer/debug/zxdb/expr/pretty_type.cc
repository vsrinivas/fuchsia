// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"

namespace zxdb {

// static
Err PrettyType::ExtractMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                              std::initializer_list<std::string> names, ExprValue* extracted) {
  ExprValue cur = value;
  for (const std::string& name : names) {
    ParsedIdentifier id(IdentifierQualification::kRelative, ParsedIdentifierComponent(name));
    ExprValue expr_value;
    if (Err err = ResolveMember(context, cur, id, &expr_value); err.has_error())
      return err;

    cur = std::move(expr_value);
  }
  *extracted = std::move(cur);
  return Err();
}

// static
Err PrettyType::Extract64BitMember(fxl::RefPtr<EvalContext> context, const ExprValue& value,
                                   std::initializer_list<std::string> names, uint64_t* extracted) {
  ExprValue member;
  if (Err err = ExtractMember(context, value, names, &member); err.has_error())
    return err;
  return member.PromoteTo64(extracted);
}

}  // namespace zxdb
