// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class ExprValue;
class Type;
class ExprValueSource;

enum class CastType {
  kImplicit,  // Things like double d = (float)f;
  kC,         // C-style cast: (int)foo;
  kReinterpret,
  kStatic
  // We don't bother implementing const_cast and dynamic_cast yet because
  // they're less useful in a debugger.
};

const char* CastTypeToString(CastType);

// TODO(brettw) replace the other cast calls with calls to this one:
Err CastExprValue(CastType cast_type, const ExprValue& source,
                  const fxl::RefPtr<Type>& dest_type, ExprValue* result);

// Attempts to convert the "source" value to the given type. This attempts
// to be as permissive as possible. In a debugger context, people want to be
// able to make arbitrary binary assignments without being told to do an
// explicit cast.
//
// Any number can be converted to any other number, even if the sign is
// different, it is truncated, or there is a float/integer mismatch. Pointers
// can be converted back-and-forth to integers as long as the sizes match.
// Composite types can be copied if the type *names* and sizes match (the
// type objects don't necessarily need to point to the same thing).
//
// This does not implement static-cast-like conversions of derived classes
// where the cast involves adjusting the value of the pointer.
//
// The dest_source will be set as the "source" of the result ExprValue. When
// generating temporaries, this should be a default-constructed
// ExprValueSource, but this is useful when doing implicit casts for assignment
// where the destination location is given.
//
// TODO(brettw) this should be renamed "ImplicitCast".
Err CoerceValueTo(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                  const ExprValueSource& dest_source, ExprValue* result);

// Executes a C++-style reinterpret_cast. The first version takes a known type
// to convert to, while the second attempts to find the correct type that
// matches the string.
//
// The source type should not be a reference type since this function is
// synchronous and will not follow references to get the referenced value.
// Calling code should use ExprNode::EvalFollowReferences() to compute the
// value or have called EnsureResolveReference().
//
// This implementation is more lax than C++, allowing any conversion that can
// be reasonable executed. C++ will, for example, prohibit conversion of a
// 32-bit integer to a 64-bit pointer, but if the user types
// "reinterpret_cast<char*>(0x12343567)" we want the debugger to be able to
// execute.
Err ReinterpretCast(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                    ExprValue* result);
Err ReinterpretCast(const ExprValue& source, const std::string& dest_type,
                    ExprValue* result);

}  // namespace zxdb
