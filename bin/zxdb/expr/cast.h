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
Err CoerceValueTo(const ExprValue& source, const fxl::RefPtr<Type>& dest_type,
                  const ExprValueSource& dest_source, ExprValue* result);

}  // namespace zxdb
