// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_UTIL_STRING_ESCAPE_H_
#define PERIDOT_LIB_UTIL_STRING_ESCAPE_H_

#include <vector>

#include <lib/fxl/strings/string_view.h>

namespace modular {

constexpr char kDefaultEscapeChar = '\\';

// Escape the set of chars in |chars_to_escape| in |input||. Use |escape_char|
// to escape. All params are expected to be in ASCII.
std::string StringEscape(fxl::StringView input, fxl::StringView chars_to_escape,
                         char escape_char = kDefaultEscapeChar);

// Unescape all escape sequences in |input|, where the escape sequence begins
// with |escape_char|. All input params are expected to be in ASCII. In debug
// mode, crashes if |input| cannot be unescaped.
std::string StringUnescape(fxl::StringView input,
                           char escape_char = kDefaultEscapeChar);

// Splits an escaped string |input| by |split_char|; this splitter skips over
// any characters escaped using |escape_char|s. All params are expected to be in
// ASCII.
//
// Example:
//  SplitEscapedString("a_b\\_c_d", '_', '\\')
//    => std::vector<StringView>{"a", "b\\_c", "d"}
std::vector<fxl::StringView> SplitEscapedString(
    fxl::StringView input, char split_char,
    char escape_char = kDefaultEscapeChar);

}  // namespace modular

#endif  // PERIDOT_LIB_UTIL_STRING_ESCAPE_H_
