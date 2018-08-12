// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/bin/zxdb/expr/expr_value.h"

namespace zxdb {

class Err;
class ExprEvalContext;

// Main entrypoint to evaluate an expression. This will parse the input,
// execute the result with the given contxet, and call the callback when
// complete.
//
// The callback may get issued asynchronously in the future or it may get
// called synchronously in a reentrant fashion from this function.
void EvalExpression(
    const std::string& input,
    fxl::RefPtr<ExprEvalContext> context,
    std::function<void(const Err& err, ExprValue value)> cb);

}  // namespace zxdb
