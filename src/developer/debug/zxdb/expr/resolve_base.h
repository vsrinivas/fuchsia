// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_BASE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_BASE_H_

#include <optional>

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Collection;
class DataMember;
class EvalContext;
class ExprValue;

// Selects whether PromotePtrRefToDerived will convert, references (either rvalue or regular),
// pointers, or both.
enum class PromoteToDerived {
  kPtrOnly,
  kRefOnly,
  kPtrOrRef,
};

// Promotes a pointer/reference type to its derived class if possible. If unknown or there's any
// error, the input value will be given to the callback (the callback will never report an error,
// but is an EvalCallback for consistency).
//
// This will promote pointers (Base* -> Derived*), references (Base& -> Derived&). It will
// NOT promote actual objects (Base -> Derived). From a language perspective, all base classes will
// need to be passed as a pointer or a reference so this operation will pick up all cases. And
// trying to do this on all types will be much slower since it will trigger for everything.
void PromotePtrRefToDerived(const fxl::RefPtr<EvalContext>& context, PromoteToDerived what,
                            ExprValue value, EvalCallback cb);

// Determines if the given collection type has a vtable pointer and returns it. This does not
// look in base classes because the vtable goes with the exact class it's on.
//
// This function can also be used to determine if the collection might possibly have a derived class
// it can be converted to. If this function returns null, PromotePtrRefToDerived is guaranteed to
// be a no-op.
//
// The input type must be concrete.
fxl::RefPtr<DataMember> GetVtableMember(const Collection* coll);

// Given an unmangled symbol name for a vtable symbol, returns the type name of the symbol.
// The input will be something like "vtable for MyClass" and this will return "MyClass".
//
// Returns the empty string on failure.
std::string TypeNameForVtableSymbolName(const std::string& sym_name);

// Computes the derived type given a vtable pointer, if possible. Returns null on failure.
fxl::RefPtr<Type> DerivedTypeForVtable(const fxl::RefPtr<EvalContext>& context, TargetPointer ptr);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_RESOLVE_BASE_H_
