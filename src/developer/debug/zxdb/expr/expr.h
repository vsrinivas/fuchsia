// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_

#include <optional>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class Err;
class EvalContext;

// Main entrypoint to evaluate an expression. This will parse the input, execute the result with the
// given context, and call the callback when complete.
//
// If follow_references is set, expressions that result in a reference will have the value of the
// referenced data computed. This is useful when the caller wants the result value of an expression
// but doesn't care about the exact type.
//
// The callback may get issued asynchronously in the future or it may get called synchronously in a
// reentrant fashion from this function.
void EvalExpression(const std::string& input, const fxl::RefPtr<EvalContext>& context,
                    bool follow_references, EvalCallback cb);

// Same as EvalExpression but uses the bycode execution path.
// TODO remove the other version when the conversion is complete.
void EvalExpressionAsBytecode(const std::string& input, const fxl::RefPtr<EvalContext>& context,
                              bool follow_references, EvalCallback cb);

// Like EvalExpressions but evaluates a sequence of expressions, issuing the callback when they're
// all complete. The size order of the results in the callback vector will correspond to the inputs.
void EvalExpressions(const std::vector<std::string>& inputs,
                     const fxl::RefPtr<EvalContext>& context, bool follow_references,
                     fit::callback<void(std::vector<ErrOrValue>)> cb);

// Determines the memory location that the given value refers to. It is used by the frontend to get
// the address of what the user meant when they typed an expression.
//
// If the result has a type with a known size, that size will be put into *size. Otherwise it will
// be untouched (for example, raw numbers will be converted to pointers that have no intrinsic
// size).
//
// TODO(bug 44074) support non-pointer values and take their address implicitly.
Err ValueToAddressAndSize(const fxl::RefPtr<EvalContext>& context, const ExprValue& value,
                          uint64_t* address, std::optional<uint32_t>* size);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_
