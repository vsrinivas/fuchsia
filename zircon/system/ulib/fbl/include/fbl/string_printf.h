// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STRING_PRINTF_H_
#define FBL_STRING_PRINTF_H_

#include <stdarg.h>

#include <zircon/compiler.h>
#include <fbl/string.h>

namespace fbl {

// Formats |printf()|-like input and returns it as an |fbl::String|.
String StringPrintf(const char* format, ...) __PRINTFLIKE(1, 2) __WARN_UNUSED_RESULT;

// Formats |vprintf()|-like input and returns it as an |fbl::String|.
String StringVPrintf(const char* format, va_list ap) __WARN_UNUSED_RESULT;

}  // namespace fbl

#endif  // FBL_STRING_PRINTF_H_
