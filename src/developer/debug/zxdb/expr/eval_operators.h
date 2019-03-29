// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <utility>

#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprEvalContext;
class ExprNode;
class ExprToken;
class ExprValue;
class ExprValueSource;
class Type;

void EvalBinaryOperator(
    fxl::RefPtr<ExprEvalContext> context, const fxl::RefPtr<ExprNode>& left,
    const ExprToken& op, const fxl::RefPtr<ExprNode>& right,
    std::function<void(const Err& err, ExprValue value)> cb);

}  // namespace zxdb
