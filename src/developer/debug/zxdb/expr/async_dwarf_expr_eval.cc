// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/async_dwarf_expr_eval.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

void AsyncDwarfExprEval::Eval(const fxl::RefPtr<EvalContext>& context,
                              const SymbolContext& expr_symbol_context,
                              const std::vector<uint8_t>& expr) {
  dwarf_eval_.Eval(context->GetDataProvider(), expr_symbol_context, expr,
                   [this_ref = RefPtrTo(this), context](DwarfExprEval*, const Err& err) {
                     this_ref->OnEvalComplete(err, context);

                     // Prevent the DwarfExprEval from getting reentrantly deleted from within its
                     // own callback by posting a reference back to the message loop.
                     debug_ipc::MessageLoop::Current()->PostTask(
                         FROM_HERE, [this_ref = std::move(this_ref)]() {});
                   });
}

void AsyncDwarfExprEval::OnEvalComplete(const Err& err, const fxl::RefPtr<EvalContext>& context) {
  if (err.has_error())
    return callback_(err);

  // The DWARF expression can produce different forms we need to handle.
  if (dwarf_eval_.GetResultType() == DwarfExprEval::ResultType::kValue) {
    // Get the concrete type since we need the byte size. But don't use this to actually construct
    // the variable since it will strip "const" and stuff that the user will expect to see.
    fxl::RefPtr<Type> concrete_type = context->GetConcreteType(type_.get());

    // The DWARF expression produced the exact value (it's not in memory).
    uint32_t type_size = concrete_type->byte_size();
    if (type_size > sizeof(DwarfExprEval::StackEntry)) {
      callback_(
          Err(fxl::StringPrintf("Result size insufficient for type of size %u. "
                                "Please file a bug with a repro case.",
                                type_size)));
      return;
    }

    // When the result was read directly from a register or is known to be constant, preserve that
    // so the user can potentially write to it (or give a good error message about writing to it).
    ExprValueSource source(ExprValueSource::Type::kTemporary);
    if (dwarf_eval_.current_register_id() != debug_ipc::RegisterID::kUnknown)
      source = ExprValueSource(dwarf_eval_.current_register_id());
    else if (dwarf_eval_.result_is_constant())
      source = ExprValueSource(ExprValueSource::Type::kConstant);

    uint64_t result_int = dwarf_eval_.GetResult();

    std::vector<uint8_t> data;
    data.resize(type_size);
    memcpy(&data[0], &result_int, type_size);
    callback_(ExprValue(type_, std::move(data), source));
  } else if (dwarf_eval_.GetResultType() == DwarfExprEval::ResultType::kData) {
    // The DWARF result is a block of data.
    //
    // Here we assume the data size is correct. If it doesn't match the type, that should be caught
    // later when it's interpreted.
    //
    // TODO(bug 39630) we have no source locations for this case.
    callback_(ExprValue(type_, dwarf_eval_.result_data(),
                        ExprValueSource(ExprValueSource::Type::kComposite)));
  } else {
    // The DWARF result is a pointer to the value.
    uint64_t result_int = dwarf_eval_.GetResult();
    ResolvePointer(context, result_int, type_, [this_ref = RefPtrTo(this)](ErrOrValue value) {
      this_ref->callback_(std::move(value));
    });
  }
}

}  // namespace zxdb
