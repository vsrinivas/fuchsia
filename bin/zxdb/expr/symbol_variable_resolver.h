// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/dwarf_expr_eval.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class SymbolContext;
class SymbolDataProvider;
class Type;
class Variable;

// Manages the conversion from a DWARF symbol to a ExprValue. This can be
// asynchronous because reading the values from the debugged program may
// require IPC.
//
// This can resolve different things, but only one request can be in progress
// at a time.
class SymbolVariableResolver {
 public:
  using Callback = std::function<void(const Err&, ExprValue)>;

  // The lifetime of this object will scope the operation. If this object is
  // destroyed before a callback is issued, the operation will be canceled and
  // the callback will not be issued.
  explicit SymbolVariableResolver(
      fxl::RefPtr<SymbolDataProvider> data_provider);
  ~SymbolVariableResolver();

  // Does the resolution. If the operation completes synchronously, the
  // callback will be issued reentrantly (from within the call stack of this
  // function).
  //
  // If this object is destroyed, the callback will be canceled.
  void ResolveVariable(const SymbolContext& symbol_context, const Variable* var,
                       Callback cb);

  // Does the resolution of a variable from a known address in memory. The
  // callback will be issued reentrantly (from within the call stack of this
  // function).
  //
  // If this object is destroyed, the callback will be canceled.
  void ResolveFromAddress(uint64_t address, fxl::RefPtr<Type> type,
                          Callback cb);

 private:
  // Callback for when the dwarf_eval_ has completed evaluation.
  void OnDwarfEvalComplete(const Err& err, fxl::RefPtr<Type> type);

  // Implements ResolveFromCallback after a callback has already been installed
  // in this class.
  void DoResolveFromAddress(uint64_t address, fxl::RefPtr<Type> type);

  // Issuse the callback. The callback could possibly delete |this| so don't
  // do anything after calling.
  void OnComplete(const Err& err, ExprValue value);

  fxl::RefPtr<SymbolDataProvider> data_provider_;

  DwarfExprEval dwarf_eval_;

  // Non-null when an operation is in progress.
  Callback current_callback_;

  fxl::WeakPtrFactory<SymbolVariableResolver> weak_factory_;
};

}  // namespace zxdb
