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
  SymbolVariableResolver(fxl::RefPtr<SymbolDataProvider> data_provider,
                         uint64_t ip);
  ~SymbolVariableResolver();

  // Does the resolution. If the operation completes synchronously, the
  // callback will be issued reentrantly (from within the call stack of this
  // function).
  void ResolveVariable(const Variable* var, Callback cb);

 private:
  // Callback for when the dwarf_eval_ has completed evaluation.
  void OnDwarfEvalComplete(const Err& err, fxl::RefPtr<Type> type, Callback cb);

  fxl::RefPtr<SymbolDataProvider> data_provider_;
  uint64_t ip_;

  DwarfExprEval dwarf_eval_;

  fxl::WeakPtrFactory<SymbolVariableResolver> weak_factory_;
};

}  // namespace zxdb
