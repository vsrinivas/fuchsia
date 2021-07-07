// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_DWARF_EXPR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_DWARF_EXPR_H_

#include <vector>

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/lib/fxl/memory/ref_counted.h"

// This file bridges the C++/Rust expression system and the symbol system's DwarfExprEval which does
// the low-level DWARF operation evaluation of DwarfExpr objects. In the simplest case you'll want
// to evaluate a DWARF expression and get an ExprValue out. In that case, use:
//
//  - DwarfExprToValue(...)
//
// There are some other uses that need more detailed control. Some code needs direct access to the
// DwarfExprEval. These cases should use one of the helper objects:
//
//  - If you want an ExprValue out but need to set up some initial state on the DwarfExprEval
//    before doing the evaluation, use AsyncDwarfExprEvalValue.
//
//  - If you want raw access to the DwarfExprEval both before and after evaluation, use
//    AsyncDwarfExprEval.

namespace zxdb {

class EvalContext;
class SymbolContext;
class Type;

// Evaluates the given DWARF expression and calls the callback with the result, using the given
// type. See file comment above.
void DwarfExprToValue(const fxl::RefPtr<EvalContext>& eval_context,
                      const SymbolContext& symbol_context, DwarfExpr expr, fxl::RefPtr<Type> type,
                      EvalCallback cb);

// Helper function which, given a completed DwarfExprEval, attempts to convert its result to the
// given type and executes the given callback.
void DwarfExprEvalToValue(const fxl::RefPtr<EvalContext>& context, DwarfExprEval& eval,
                          fxl::RefPtr<Type> type, EvalCallback cb);

// Manages evaluation of a DWARF expression (which might be asynchronous and need some tricky memory
// management). This keeps itself and the expression evaluator alive during the computation.
//
// See the file comment above, most callers will want one of the other variants.
//
// Example:
//
//   auto eval = fxl::MakeRefCounted<AsyncDwarfExprEval>([](DwarfExprEval& eval) {
//     eval->...();
//   });
//   eval->Eval(data_provider, expression);
//
class AsyncDwarfExprEval : public fxl::RefCountedThreadSafe<AsyncDwarfExprEval> {
 public:
  using DwarfEvalCallback = fit::callback<void(DwarfExprEval&, const Err& err)>;

  // Allows the expression evaluator to be set up before Eval() is called for cases where it needs
  // initial state.
  DwarfExprEval& dwarf_eval() { return dwarf_eval_; }

  // Starts evaluation. It will take a reference to itself during execution and the callback passed
  // into the constructor will be issued on completion. This can only be called once.
  //
  // The symbol context should be the one for the module the expression came from so that addresses
  // within the expression can be interpreted correctly.
  void Eval(fxl::RefPtr<SymbolDataProvider> data_provider, const SymbolContext& expr_symbol_context,
            DwarfExpr expr);

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(AsyncDwarfExprEval);
  FRIEND_MAKE_REF_COUNTED(AsyncDwarfExprEval);

  explicit AsyncDwarfExprEval(DwarfEvalCallback cb) : dwarf_callback_(std::move(cb)) {}
  virtual ~AsyncDwarfExprEval() = default;

 private:
  DwarfExprEval dwarf_eval_;
  DwarfEvalCallback dwarf_callback_;
};

// Automatically converts the result of the DwarfExprEval to an EvalCallback (an error or a value).
// See the simpler DwarfExprToValue() function above for cases that don't need low-level access to
// the DwarfExprEval object.
//
// Example:
//
//   auto eval = fxl::MakeRefCounted<AsyncDwarfExprEvalValue>(context, type, std::move(cb));
//   ...any required setup of the dwarf_eval()...
//   eval->Eval(context->GetDataProvider(), expression);
//
class AsyncDwarfExprEvalValue : public AsyncDwarfExprEval {
 public:
  // Call Eval() on the base class to start evaluation.

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(AsyncDwarfExprEvalValue);
  FRIEND_MAKE_REF_COUNTED(AsyncDwarfExprEvalValue);

  // The passed-in callback will be executed if the DwarfExprEval returns success. It will have
  // the given type.
  AsyncDwarfExprEvalValue(const fxl::RefPtr<EvalContext>& context, fxl::RefPtr<Type> type,
                          EvalCallback cb);

 private:
  void OnEvalComplete(const Err& err);

  fxl::RefPtr<EvalContext> context_;

  // Not necessarily a concrete type, this is the type of the result the user will see.
  fxl::RefPtr<Type> type_;

  EvalCallback value_callback_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_DWARF_EXPR_H_
