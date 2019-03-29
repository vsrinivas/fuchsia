// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"

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
// Multiple requests can be pending at a time. This can happen if another
// resolve request happens while a previous one is pending on an asynchronous
// memory ot register read.
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
                       Callback cb) const;

 private:
  // The data associated with one in-progress variable resolution. This must be
  // heap allocated for each resolution operation since multiple operations can
  // be pending.
  struct ResolutionState : public fxl::RefCountedThreadSafe<ResolutionState> {
    DwarfExprEval dwarf_eval;
    Callback callback;

    // This private stuff prevents refcounted mistakes.
   private:
    FRIEND_REF_COUNTED_THREAD_SAFE(ResolutionState);
    FRIEND_MAKE_REF_COUNTED(ResolutionState);

    explicit ResolutionState(Callback cb) : callback(std::move(cb)) {}
    ~ResolutionState() = default;
  };

  // The functions below represent a flow that is usually executed in order.

  // Callback for when the dwarf_eval_ has completed evaluation.
  void OnDwarfEvalComplete(fxl::RefPtr<ResolutionState> state, const Err& err,
                           fxl::RefPtr<Type> type) const;

  // Issue the callback. The callback could possibly delete |this| so don't
  // do anything after calling.
  void OnComplete(fxl::RefPtr<ResolutionState> state, const Err& err,
                  ExprValue value) const;

  fxl::RefPtr<SymbolDataProvider> data_provider_;

  // Mutable because const functions want to take weak references to this class
  // that are logically const, but there's no such thing as a weak ref to a
  // const class.
  mutable fxl::WeakPtrFactory<SymbolVariableResolver> weak_factory_;
};

}  // namespace zxdb
