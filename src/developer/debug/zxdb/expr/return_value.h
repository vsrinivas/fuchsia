// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RETURN_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RETURN_VALUE_H_

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"

namespace zxdb {

class Function;

// Use immediately following the return instruction of the given non-inline function. This computes
// the return value of the function if possible, and issues the callback with it.
//
// The callback will be issued reentrantly if the value is known synchronously. The callback
// ExprValue will be valid but empty if the function return type is void.
void GetReturnValue(const fxl::RefPtr<EvalContext>& context, const Function* func, EvalCallback cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RETURN_VALUE_H_
