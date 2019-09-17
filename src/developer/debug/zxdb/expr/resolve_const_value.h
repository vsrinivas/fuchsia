// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_CONST_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_CONST_VALUE_H_

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class Value;

// Given a Value that holds a ConstValue (value->const_value().has_value() == true), computes the
// result.
//
// FUTURE ENHANCEMENT: If ConstValue starts to be used for more things like enumeration values, we
// may want to add a "ConstValue" constructor/getter to ExprValue and remove this.
ErrOrValue ResolveConstValue(const fxl::RefPtr<EvalContext>& context, const Value* value);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_CONST_VALUE_H_
