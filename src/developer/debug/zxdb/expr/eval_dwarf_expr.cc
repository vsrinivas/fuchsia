// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_dwarf_expr.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

void DwarfExprToValue(const fxl::RefPtr<EvalContext>& eval_context,
                      const SymbolContext& symbol_context, DwarfExpr expr, fxl::RefPtr<Type> type,
                      EvalCallback cb) {
  auto evaluator =
      fxl::MakeRefCounted<AsyncDwarfExprEvalValue>(eval_context, std::move(type), std::move(cb));
  evaluator->Eval(eval_context->GetDataProvider(), symbol_context, std::move(expr));
}

void DwarfExprEvalToValue(const fxl::RefPtr<EvalContext>& context, DwarfExprEval& eval,
                          fxl::RefPtr<Type> type, EvalCallback cb) {
  if (eval.GetResultType() == DwarfExprEval::ResultType::kValue) {
    // Get the concrete type since we need the byte size. But don't use this to actually construct
    // the variable since it will strip "const" and stuff that the user will expect to see.
    fxl::RefPtr<Type> concrete_type = context->GetConcreteType(type.get());

    // The DWARF expression produced the exact value (it's not in memory).
    uint32_t type_size = concrete_type->byte_size();
    if (type_size > sizeof(DwarfExprEval::StackEntry)) {
      return cb(
          Err(fxl::StringPrintf("Result size insufficient for type of size %u. "
                                "Please file a bug with a repro case.",
                                type_size)));
    }

    // When the result was read directly from a register or is known to be constant, preserve that
    // so the user can potentially write to it (or give a good error message about writing to it).
    ExprValueSource source(ExprValueSource::Type::kTemporary);
    if (eval.current_register_id() != debug_ipc::RegisterID::kUnknown)
      source = ExprValueSource(eval.current_register_id());
    else if (eval.result_is_constant())
      source = ExprValueSource(ExprValueSource::Type::kConstant);

    uint64_t result_int = eval.GetResult();

    std::vector<uint8_t> data;
    data.resize(type_size);
    memcpy(data.data(), &result_int, type_size);
    cb(ExprValue(type, std::move(data), source));
  } else if (eval.GetResultType() == DwarfExprEval::ResultType::kData) {
    // The DWARF result is a block of data.
    //
    // Here we assume the data size is correct. If it doesn't match the type, that should be caught
    // later when it's interpreted.
    //
    // TODO(bug 39630) we have no source locations for this case.
    cb(ExprValue(type, eval.TakeResultData(), ExprValueSource(ExprValueSource::Type::kComposite)));
  } else {
    // The DWARF result is a pointer to the value.
    uint64_t result_int = eval.GetResult();
    ResolvePointer(context, result_int, type,
                   [cb = std::move(cb)](ErrOrValue value) mutable { cb(std::move(value)); });
  }
}

void AsyncDwarfExprEval::Eval(fxl::RefPtr<SymbolDataProvider> data_provider,
                              const SymbolContext& expr_symbol_context, DwarfExpr expr) {
  dwarf_eval_.Eval(std::move(data_provider), expr_symbol_context, std::move(expr),
                   [this_ref = RefPtrTo(this)](DwarfExprEval*, const Err& err) {
                     this_ref->dwarf_callback_(this_ref->dwarf_eval_, err);

                     // Prevent the DwarfExprEval from getting reentrantly deleted from within its
                     // own callback by posting a reference back to the message loop.
                     debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                                             [this_ref = std::move(this_ref)]() {});
                   });
}

// The callback passed to the base class can capture |this| because we're the same object.
AsyncDwarfExprEvalValue::AsyncDwarfExprEvalValue(const fxl::RefPtr<EvalContext>& context,
                                                 fxl::RefPtr<Type> type, EvalCallback cb)
    : AsyncDwarfExprEval([this](DwarfExprEval&, const Err& err) { OnEvalComplete(err); }),
      context_(context),
      type_(std::move(type)),
      value_callback_(std::move(cb)) {}

void AsyncDwarfExprEvalValue::OnEvalComplete(const Err& err) {
  if (err.has_error())
    return value_callback_(err);
  DwarfExprEvalToValue(context_, dwarf_eval(), type_, std::move(value_callback_));
}

}  // namespace zxdb
