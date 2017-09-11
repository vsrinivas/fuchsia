// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_STRINGS_ASCII_H_
#define LIB_FXL_STRINGS_ASCII_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

inline char ToLowerASCII(char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline char ToUpperASCII(char c) {
  return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

FXL_EXPORT bool EqualsCaseInsensitiveASCII(fxl::StringView v1,
                                           fxl::StringView v2);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_ASCII_H_
