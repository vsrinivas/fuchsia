// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_

#include <utility>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class ExprNode;
class ExprToken;
class ExprValue;
class ExprValueSource;
class Type;

// This version takes evaluated values and so can not support short-circuiting for || and &&.
// Shor-circuiting is implemented in the bytecode emitted by the BinaryOpNode.
void EvalBinaryOperator(const fxl::RefPtr<EvalContext>& context, const ExprValue& left_value,
                        const ExprToken& op, const ExprValue& right_value, EvalCallback cb);

void EvalUnaryOperator(const fxl::RefPtr<EvalContext>& context, const ExprToken& op_token,
                       const ExprValue& value, EvalCallback cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_
