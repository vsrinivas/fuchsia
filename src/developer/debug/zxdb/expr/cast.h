// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_CAST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_CAST_H_

#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class ExprValue;
class Type;

// Our casting rules are somewhat different than C++. In a debugger, we want to be as permissive as
// reasonable given the rules of the requested cast. When the user is interactively assigning or
// converting values, they usually don't want the warnings and errors that come with C++.
enum class CastType {
  // Implicit casts are for expressions like "double d = (float)f";
  //
  // Any number can be converted to any other number, even if the sign is different, it is
  // truncated, or there is a float/integer mismatch. Pointers can be converted back-and-forth to
  // integers as long as the sizes match. Composite types can be copied if the type names and sizes
  // match (the type objects don't necessarily need to point to the same thing because
  // we can easily get different definitions of the same type).
  kImplicit,

  // A C-style cast: "(int)foo;".
  //
  // This attempts a static_cast and falls back to reinterpret_cast.
  kC,

  // Converts pointer types.
  //
  // Our rules are more lax than C++, allowing any conversion that can be reasonable executed. C++
  // will, for example, prohibit conversion of a 32-bit integer to a 64-bit pointer, but if the user
  // types "reinterpret_cast<char*>(0x12343567)" we want the debugger to be able to execute.
  kReinterpret,

  // A Rust-style cast: "foo as bar;".
  //
  // The expected behaviors of this type of cast are documented here:
  //
  // https://doc.rust-lang.org/nomicon/casts.html
  kRust,

  // Compared to C++, the debugger's implicit cast is so powerful that the only thing that
  // static_cast adds is conversions to derived classes for pointers and references.
  kStatic

  // We don't bother implementing const_cast and dynamic_cast yet because they're less useful in a
  // debugger.
};

const char* CastTypeToString(CastType);

// Casts to a given type using a specific set of casting rules.
//
// The dest_source is an optional specification of what "source location" the returned value should
// have. For the default behavior, use an empty ExprValueSource().
void CastExprValue(const fxl::RefPtr<EvalContext>& eval_context, CastType cast_type,
                   const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                   const ExprValueSource& dest_source, EvalCallback cb);

// A numeric cast handles implicit casts of numeric types. This subset of casting can be synchronous
// because it does not need to follow references or virtual inheritance.
ErrOrValue CastNumericExprValue(const fxl::RefPtr<EvalContext>& eval_context,
                                const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                                const ExprValueSource& dest_source = ExprValueSource());

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_CAST_H_
