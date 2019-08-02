// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CALLBACK_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CALLBACK_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

using EvalCallback = fit::callback<void(ErrOrValue)>;

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CALLBACK_H_
