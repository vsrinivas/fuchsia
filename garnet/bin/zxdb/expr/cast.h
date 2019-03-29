// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/expr/expr_value_source.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class Type;

// Our casting rules are somewhat different than C++. In a debugger, we want to
// be as permissive as reasonable given the rules of the requested cast. When
// the user is interactively assigning or converting values, they usually don't
// want the warnings and errors that come with C++.
enum class CastType {
  // Implicit casts are for expressions like "double d = (float)f";
  //
  // Any number can be converted to any other number, even if the sign is
  // different, it is truncated, or there is a float/integer mismatch. Pointers
  // can be converted back-and-forth to integers as long as the sizes match.
  // Composite types can be copied if the type names and sizes match (the
  // type objects don't necessarily need to point to the same thing because
  // we can easily get different definitions of the same type).
  kImplicit,

  // A C-style cast: "(int)foo;".
  //
  // This attempts a static_cast and falls back to reinterpret_cast.
  kC,

  // Converts pointer types.
  //
  // Our rules are more lax than C++, allowing any conversion that can be
  // reasonable executed. C++ will, for example, prohibit conversion of a
  // 32-bit integer to a 64-bit pointer, but if the user types
  // "reinterpret_cast<char*>(0x12343567)" we want the debugger to be able to
  // execute.
  kReinterpret,

  // TODO(DX-1178) write static cast.
  // kStatic

  // We don't bother implementing const_cast and dynamic_cast yet because
  // they're less useful in a debugger.
};

const char* CastTypeToString(CastType);

// Casts to a given type using a specific set of casting rules.
//
// The source type should not be a reference type since this function is
// synchronous and will not follow references to get the referenced value.
// Calling code should use ExprNode::EvalFollowReferences() to compute the
// value or have called EnsureResolveReference().
//
// The dest_source is an optional specification of what "source location" the
// returned value should have.
Err CastExprValue(CastType cast_type, const ExprValue& source,
                  const fxl::RefPtr<Type>& dest_type, ExprValue* result,
                  const ExprValueSource& dest_source = ExprValueSource());

}  // namespace zxdb
