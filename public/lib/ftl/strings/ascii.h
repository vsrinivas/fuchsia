// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_STRINGS_ASCII_H_
#define LIB_FTL_STRINGS_ASCII_H_

#include <string>

#include "lib/ftl/strings/string_view.h"

namespace ftl {

inline char ToLowerASCII(char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline char ToUpperASCII(char c) {
  return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

bool EqualsCaseInsensitiveASCII(ftl::StringView v1, ftl::StringView v2);

}  // namespace ftl

#endif  // LIB_FTL_STRINGS_STRING_UTIL_H_
