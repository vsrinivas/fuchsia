// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_EVAL_CONTEXT_H_

#include <functional>

#include "src/developer/debug/zxdb/expr/name_lookup.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;
class ExprValue;
class Symbol;
class SymbolDataProvider;
class SymbolVariableResolver;
class Variable;

// Interface used by expression evaluation to communicate with the outside
// world. This provides access to the variables currently in scope.
class ExprEvalContext : public fxl::RefCountedThreadSafe<ExprEvalContext> {
 public:
  using ValueCallback = std::function<void(
      const Err& err, fxl::RefPtr<Symbol> symbol, ExprValue value)>;

  virtual ~ExprEvalContext() = default;

  // Issues the callback with the value of the given named value in the context
  // of the current expression evaluation. This will handle things like
  // implicit |this| members in addition to normal local variables.
  //
  // The callback also returns the Symbol associated with the variable it
  // found. This can be used for diagnostics. It is possible for the symbol
  // to be valid but the err to be set if the symbol was found but it could not
  // be evaluated.
  //
  // The callback may be issued asynchronously in the future if communication
  // with the remote debugged application is required. The callback may be
  // issued reentrantly for synchronously available data.
  virtual void GetNamedValue(const ParsedIdentifier& identifier,
                             ValueCallback cb) = 0;

  // Attempts to resolve a type that is a declaration (is_declaration() is set
  // on the type) by looking up a non-declaration type with the same name.
  //
  // Most callers will want GetConcreteType() instead, of which this is one
  // component.
  //
  // Some variables will be specified by DWARF as having a type that's only a
  // declaration. Declarations don't have full definitions which makes it
  // impossible to interpret the data.
  //
  // Since the lookup is by type name, it may fail. It could also refer to
  // a different type, but if the user has more than one type with the same
  // name bad things will happen anyway. On failure, the input type will be
  // returned.
  virtual fxl::RefPtr<Type> ResolveForwardDefinition(const Type* type) = 0;

  // Strips C-V qualifications and resolves forward declarations.
  //
  // This is the function to use to properly resolve the type to something
  // there the data of the ExprValue can be interpreted.
  //
  // It will return null only if the input type is null. Sometimes forward
  // declarations can't be resolved or the "const" refers to nothing, in which
  // case this function will return the original type.
  virtual fxl::RefPtr<Type> GetConcreteType(const Type* type) = 0;

  // Returns the SymbolVariableResolver used to create variables from
  // memory for this context.
  virtual SymbolVariableResolver& GetVariableResolver() = 0;

  virtual fxl::RefPtr<SymbolDataProvider> GetDataProvider() = 0;

  // Returns a callback the parser can use to lookup names.
  //
  // It is assumed this callback is used for parsing and discarded rather than
  // stored since it may have references back the eval context.
  virtual NameLookupCallback GetSymbolNameLookupCallback() = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_EVAL_CONTEXT_H_
