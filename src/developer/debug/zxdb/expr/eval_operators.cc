// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_operators.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"

namespace zxdb {

namespace {

using EvalCallback = std::function<void(const Err& err, ExprValue value)>;

void DoAssignment(fxl::RefPtr<EvalContext> context, const ExprValue& left_value,
                  const ExprValue& right_value, EvalCallback cb) {
  // Note: the calling code will have evaluated the value of the left node.
  // Often this isn't strictly necessary: we only need the "source", but
  // optimizing in that way would complicate things.
  const ExprValueSource& dest = left_value.source();
  if (dest.type() == ExprValueSource::Type::kTemporary) {
    cb(Err("Can't assign to a temporary."), ExprValue());
    return;
  }

  // The coerced value will be the result. It should have the "source" of the
  // left-hand-side since the location being assigned to doesn't change.
  ExprValue coerced;
  Err err = CastExprValue(context.get(), CastType::kImplicit, right_value,
                          left_value.type_ref(), &coerced, dest);
  if (err.has_error()) {
    cb(err, ExprValue());
    return;
  }

  // Make a copy to avoid ambiguity of copying and moving the value below.
  std::vector<uint8_t> data = coerced.data();

  // Update the memory with the new data. The result of the expression is
  // the coerced value.
  context->GetDataProvider()->WriteMemory(
      dest.address(), std::move(data),
      [coerced = std::move(coerced), cb = std::move(cb)](const Err& err) {
        if (err.has_error())
          cb(err, ExprValue());
        else
          cb(Err(), coerced);
      });
}

void DoBinaryOperator(fxl::RefPtr<EvalContext> context,
                      const ExprValue& left_value, const ExprToken& op,
                      const ExprValue& right_value, EvalCallback cb) {
  switch (op.type()) {
    case ExprTokenType::kEquals:
      DoAssignment(std::move(context), left_value, right_value, std::move(cb));
      break;

    case ExprTokenType::kEquality:
    case ExprTokenType::kAmpersand:
    case ExprTokenType::kDoubleAnd:
    case ExprTokenType::kBitwiseOr:
    case ExprTokenType::kLogicalOr:
    default:
      cb(Err("Unsupported binary operator '%s', sorry!", op.value().c_str()),
         ExprValue());
      break;
  }
}

}  // namespace

void EvalBinaryOperator(fxl::RefPtr<EvalContext> context,
                        const fxl::RefPtr<ExprNode>& left, const ExprToken& op,
                        const fxl::RefPtr<ExprNode>& right, EvalCallback cb) {
  left->Eval(context, [context, op, right, cb = std::move(cb)](
                          const Err& err, ExprValue left_value) {
    if (err.has_error()) {
      cb(err, ExprValue());
      return;
    }

    // Note: if we implement ||, need to special-case here so evaluation
    // short-circuits if the "left" is true.
    right->Eval(context,
                [context, left_value = std::move(left_value), op,
                 cb = std::move(cb)](const Err& err, ExprValue right_value) {
                  DoBinaryOperator(std::move(context), left_value, op,
                                   right_value, std::move(cb));
                });
  });
}

}  // namespace zxdb
