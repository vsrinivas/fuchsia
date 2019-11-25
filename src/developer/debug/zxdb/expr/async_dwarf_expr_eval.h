// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ASYNC_DWARF_EXPR_EVAL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ASYNC_DWARF_EXPR_EVAL_H_

#include <vector>

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class EvalContext;
class SymbolContext;
class Type;

// Manages evaluation of a DWARF expression (which might be asynchronous and need some tricky memory
// management), and constructs the proper type of ExprValue with the result.
//
// This keeps itself and the expression evaluator alive during the computation.
class AsyncDwarfExprEval : public fxl::RefCountedThreadSafe<AsyncDwarfExprEval> {
 public:
  // Alows the expression evaluator to be set up before Eval() is called for cases where it needs
  // initial state.
  DwarfExprEval& dwarf_eval() { return dwarf_eval_; }

  // Starts evaluation. The callback passed into the constructor will be issued on completion.
  // This can only be called once.
  //
  // The symbol context should be the one for the module the expression came from so that addresses
  // within the expression can be interpreted correctly.
  void Eval(const fxl::RefPtr<EvalContext>& context, const SymbolContext& expr_symbol_context,
            const std::vector<uint8_t>& expr);

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(AsyncDwarfExprEval);
  FRIEND_MAKE_REF_COUNTED(AsyncDwarfExprEval);

  // The passed-in callback will be executed if the DwarfExprEval returns success. It will have
  // the given type.
  //
  // Note this class is derived from in a test so these need to be protected and virtual.
  explicit AsyncDwarfExprEval(EvalCallback cb, fxl::RefPtr<Type> type)
      : callback_(std::move(cb)), type_(std::move(type)) {}
  virtual ~AsyncDwarfExprEval() = default;

  void OnEvalComplete(const Err& err, const fxl::RefPtr<EvalContext>& context);

  DwarfExprEval dwarf_eval_;
  EvalCallback callback_;

  // Not necessarily a concrete type, this is the type of the result the user will see.
  fxl::RefPtr<Type> type_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_ASYNC_DWARF_EXPR_EVAL_H_
