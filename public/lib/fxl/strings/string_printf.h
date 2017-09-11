// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// |printf()|-like formatting functions that output/append to C++ strings.

#ifndef LIB_FXL_STRINGS_STRING_PRINTF_H_
#define LIB_FXL_STRINGS_STRING_PRINTF_H_

#include <stdarg.h>

#include <string>

#include "lib/fxl/compiler_specific.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"

namespace fxl {

// Formats |printf()|-like input and returns it as an |std::string|.
FXL_EXPORT std::string StringPrintf(const char* format, ...)
    FXL_PRINTF_FORMAT(1, 2) FXL_WARN_UNUSED_RESULT;

// Formats |vprintf()|-like input and returns it as an |std::string|.
FXL_EXPORT std::string StringVPrintf(const char* format,
                                     va_list ap) FXL_WARN_UNUSED_RESULT;

// Formats |printf()|-like input and appends it to |*dest|.
FXL_EXPORT void StringAppendf(std::string* dest, const char* format, ...)
    FXL_PRINTF_FORMAT(2, 3);

// Formats |vprintf()|-like input and appends it to |*dest|.
FXL_EXPORT void StringVAppendf(std::string* dest,
                               const char* format,
                               va_list ap);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_STRING_PRINTF_H_
