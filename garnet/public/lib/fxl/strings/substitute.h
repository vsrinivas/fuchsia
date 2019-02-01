// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_STRINGS_SUBSTITUTE_H_
#define LIB_FXL_STRINGS_SUBSTITUTE_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

// Perform string substitutions using a positional notation.
//
// The format string uses positional identifiers consisting of a $ sign followed
// by a single digit: $0-$9. Each positional identifier refers to the
// corresponding string in the argument list: $0 for the first argument, etc.
// Unlike fxl::StringPrintf, callers do not have to specify the type, and
// it is possible to reuse the same positional identifier multiple times.
//
// If Substitute encounters an error (for example, not enough arguments), it
// crashes in debug mode, and returns an empty string in non-debug mode.
//
// This function is inspired by Abseil's strings/substitute.h.
FXL_EXPORT std::string Substitute(StringView format, StringView arg0);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4,
                                  StringView arg5);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4,
                                  StringView arg5, StringView arg6);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4,
                                  StringView arg5, StringView arg6,
                                  StringView arg7);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4,
                                  StringView arg5, StringView arg6,
                                  StringView arg7, StringView arg8);
FXL_EXPORT std::string Substitute(StringView format, StringView arg0,
                                  StringView arg1, StringView arg2,
                                  StringView arg3, StringView arg4,
                                  StringView arg5, StringView arg6,
                                  StringView arg7, StringView arg8,
                                  StringView arg9);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_SUBSTITUTE_H_
