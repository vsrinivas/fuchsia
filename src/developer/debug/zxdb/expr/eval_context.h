// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/name_lookup.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;
class PrettyTypeManager;
class Symbol;
class SymbolDataProvider;
class Value;
class Variable;

// Interface used by expression evaluation to communicate with the outside world. This provides
// access to the variables currently in scope.
//
// PASSING CONVENTION
//
// Prefer to pass EvalContext function parameters as:
//   const fxl::RefPtr<EvalContext>& context
// The advantage is that this will avoid an atomic refcount in most cases, but still is
// automatically ref-ed when bound in a lambda.
class EvalContext : public fxl::RefCountedThreadSafe<EvalContext> {
 public:
  virtual ~EvalContext() = default;

  // Returns the language associated with the expression.
  virtual ExprLanguage GetLanguage() const = 0;

  // Issues the callback with the value of the given named value in the context of the current
  // expression evaluation. This will handle things like implicit |this| members in addition to
  // normal local variables.
  //
  // The callback also returns the Symbol associated with the variable it found. This can be used
  // for diagnostics. It is possible for the symbol to be valid but the err to be set if the symbol
  // was found but it could not be evaluated.
  //
  // The callback may be issued asynchronously in the future if communication with the remote
  // debugged application is required. The callback may be issued reentrantly for synchronously
  // available data.
  //
  // If the EvalContext is destroyed before the data is ready, the callback will not be issued.
  virtual void GetNamedValue(const ParsedIdentifier& identifier, EvalCallback cb) const = 0;

  // Like GetNamedValue() but takes an already-identified Variable.
  //
  // This will handle extern variables and will resolve them. In this case the EvalCallback's
  // variable will be the resolved extern one. Otherwise it will be the input Value.
  //
  // The value is normally a Variable but it can also be an extern DataMember (which will transform
  // into a Variable when the extern is resolved).
  virtual void GetVariableValue(fxl::RefPtr<Value> variable, EvalCallback cb) const = 0;

  // Attempts to resolve a type that is a declaration (is_declaration() is set on the type) by
  // looking up a non-declaration type with the same name.
  //
  // Most callers will want GetConcreteType() instead, of which this is one component.
  //
  // Some variables will be specified by DWARF as having a type that's only a declaration.
  // Declarations don't have full definitions which makes it impossible to interpret the data.
  //
  // Since the lookup is by type name, it may fail. It could also refer to a different type, but if
  // the user has more than one type with the same name bad things will happen anyway. On failure,
  // the Type* version will return the input type, and the ParsedIdentifier version will return
  // null.
  virtual fxl::RefPtr<Type> ResolveForwardDefinition(const Type* type) const = 0;
  virtual fxl::RefPtr<Type> ResolveForwardDefinition(ParsedIdentifier type_name) const = 0;

  // Strips C-V qualifications and resolves forward declarations.
  //
  // This is the function to use to properly resolve the type to something there the data of the
  // ExprValue can be interpreted.
  //
  // It will return null only if the input type is null. Sometimes forward declarations can't be
  // resolved or the "const" refers to nothing, in which case this function will return the original
  // type.
  virtual fxl::RefPtr<Type> GetConcreteType(const Type* type) const = 0;

  virtual fxl::RefPtr<SymbolDataProvider> GetDataProvider() = 0;

  // Returns a callback the parser can use to lookup names.
  //
  // It is assumed this callback is used for parsing and discarded rather than stored since it may
  // have references back the eval context.
  virtual NameLookupCallback GetSymbolNameLookupCallback() = 0;

  // Returns a symbolized (if possible) location for the given address.
  virtual Location GetLocationForAddress(uint64_t address) const = 0;

  virtual const PrettyTypeManager& GetPrettyTypeManager() const = 0;

  // Returns the format to be used for converting vector registers to values.
  virtual VectorRegisterFormat GetVectorRegisterFormat() const = 0;

  // Returns true if base classes should automatically be promoted to derived classes when pointer
  // and references are dereferences.
  virtual bool ShouldPromoteToDerived() const = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_
