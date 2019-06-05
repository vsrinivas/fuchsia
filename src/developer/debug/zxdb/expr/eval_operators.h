// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_

#include <functional>
#include <utility>

#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class ExprNode;
class ExprToken;
class ExprValue;
class ExprValueSource;
class Type;

void EvalBinaryOperator(
    fxl::RefPtr<EvalContext> context, const fxl::RefPtr<ExprNode>& left,
    const ExprToken& op, const fxl::RefPtr<ExprNode>& right,
    std::function<void(const Err& err, ExprValue value)> cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_OPERATORS_H_
