// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_STRINGS_STRING_UTIL_H_
#define LIB_FTL_STRINGS_STRING_UTIL_H_

#include <string>

#include "lib/ftl/strings/string_view.h"

namespace ftl {

// Returns a StringView over str, where chars_to_trim are removed from the
// beginning and end of the StringView.
ftl::StringView TrimString(ftl::StringView str, ftl::StringView chars_to_trim);

}  // namespace ftl

#endif  // LIB_FTL_STRINGS_STRING_UTIL_H_
