// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;
class ExprValue;
class SymbolDataProvider;
class SymbolVariableResolver;
class Variable;

// Interface used by expression evaluation to communicate with the outside
// world. This provides access to the variables currently in scope.
class ExprEvalContext : public fxl::RefCountedThreadSafe<ExprEvalContext> {
 public:
  virtual ~ExprEvalContext() = default;

  // Searches the current context for a variable with the given name using
  // language scoping rules (innermost blocks first, going outward, then
  // function parameters).
  //
  // Works specifically for variables (local and function params), not members
  // of |this|.
  //
  // If found, returns it, otherwise returns nullptr.
  virtual const Variable* GetVariableSymbol(const std::string& name) = 0;

  // Issues the callback with the value of the given named value in the context
  // of the current expression evaluation. This will handle things like
  // implicit |this| members in addition to normal local variables.
  //
  // The callback may be issued asynchronously in the future if communication
  // with the remote debugged application is required. The callback may be
  // issued reentrantly for synchronously available data.
  virtual void GetNamedValue(
      const std::string& name,
      std::function<void(const Err& err, ExprValue value)>) = 0;

  // Returns the SymbolVariableResolver used to create variables from
  // memory for this context.
  virtual SymbolVariableResolver& GetVariableResolver() = 0;

  virtual fxl::RefPtr<SymbolDataProvider> GetDataProvider() = 0;
};

}  // namespace zxdb
