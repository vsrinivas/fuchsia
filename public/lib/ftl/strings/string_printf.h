// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// |printf()|-like formatting functions that output/append to C++ strings.

#ifndef LIB_FTL_STRINGS_STRING_PRINTF_H_
#define LIB_FTL_STRINGS_STRING_PRINTF_H_

#include <stdarg.h>

#include <string>

#include "lib/ftl/compiler_specific.h"
#include "lib/ftl/macros.h"

namespace ftl {

// Formats |printf()|-like input and returns it as an |std::string|.
std::string StringPrintf(const char* format, ...)
    FTL_PRINTF_FORMAT(1, 2) FTL_WARN_UNUSED_RESULT;

// Formats |vprintf()|-like input and returns it as an |std::string|.
std::string StringVPrintf(const char* format,
                          va_list ap) FTL_WARN_UNUSED_RESULT;

// Formats |printf()|-like input and appends it to |*dest|.
void StringAppendf(std::string* dest, const char* format, ...)
    FTL_PRINTF_FORMAT(2, 3);

// Formats |vprintf()|-like input and appends it to |*dest|.
void StringVAppendf(std::string* dest, const char* format, va_list ap);

}  // namespace ftl

#endif  // LIB_FTL_STRINGS_STRING_PRINTF_H_
