// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"
#include "src/developer/debug/zxdb/expr/resolve_type.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Abi;
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
  // Callback type used to implement builtin functions.
  using BuiltinFuncCallback =
      fit::function<void(const fxl::RefPtr<EvalContext>& eval_context,
                         const std::vector<ExprValue>& params, EvalCallback cb)>;

  // Returns the language associated with the expression.
  virtual ExprLanguage GetLanguage() const = 0;

  // The ABI defines the calling conventions on the current platform.
  virtual const std::shared_ptr<Abi>& GetAbi() const = 0;

  // Looks up the given name in the current context.
  //
  // These use the global FindName backend with our context from GetFindNameContext() below. The
  // main difference is that this call additionally allows tests to inject names without setting up
  // the very complex symbol system indexing.
  //
  // The version that returns a FoundName is the same as the one that takes an output vector but
  // limits the result to 1 max.
  FoundName FindName(const FindNameOptions& options, const ParsedIdentifier& identifier) const;
  virtual void FindName(const FindNameOptions& options, const ParsedIdentifier& looking_for,
                        std::vector<FoundName>* results) const = 0;

  // Returns a context for looking up names via FindName. Prefer not to use this and instead
  // call EvalContext.FindName (which uses this context implicitly) because it additionally allows
  // mocking.
  virtual FindNameContext GetFindNameContext() const = 0;

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

  // Checks for a builtin function with the given name. If one exists, returns a pointer to the
  // callback. Returns null if it doesn't exist.
  virtual const BuiltinFuncCallback* GetBuiltinFunction(const ParsedIdentifier& name) const = 0;

  // Convenience wrappers around the toplevel GetConcreteType() that uses the FindNameContext()
  // from this class.
  fxl::RefPtr<Type> GetConcreteType(const Type* type) const {
    return zxdb::GetConcreteType(GetFindNameContext(), type);
  }
  fxl::RefPtr<Type> GetConcreteType(const LazySymbol& symbol) const {
    return zxdb::GetConcreteType(GetFindNameContext(), symbol);
  }
  template <typename DerivedType>
  fxl::RefPtr<DerivedType> GetConcreteTypeAs(const Type* type) {
    return zxdb::GetConcreteTypeAs<DerivedType>(GetFindNameContext(), type);
  }
  template <typename DerivedType>
  fxl::RefPtr<DerivedType> GetConcreteTypeAs(const LazySymbol& symbol) {
    return zxdb::GetConcreteTypeAs<DerivedType>(GetFindNameContext(), symbol);
  }

  // May return null (ProcessSymbols are destroyed with the process, and the EvalContext is
  // refcounted and can outlive it).
  virtual const ProcessSymbols* GetProcessSymbols() const = 0;

  virtual fxl::RefPtr<SymbolDataProvider> GetDataProvider() = 0;

  // Returns a symbolized (if possible) location for the given address.
  virtual Location GetLocationForAddress(uint64_t address) const = 0;

  virtual const PrettyTypeManager& GetPrettyTypeManager() const = 0;

  // Returns the format to be used for converting vector registers to values.
  virtual VectorRegisterFormat GetVectorRegisterFormat() const = 0;

  // Returns true if base classes should automatically be promoted to derived classes (when the
  // derived class is known) when pointer and references are dereferences.
  virtual bool ShouldPromoteToDerived() const = 0;

 protected:
  // Only RefPtr should be destructing this class.
  FRIEND_REF_COUNTED_THREAD_SAFE(EvalContext);
  virtual ~EvalContext() = default;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_H_
