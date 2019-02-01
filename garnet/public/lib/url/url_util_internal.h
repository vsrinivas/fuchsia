// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_UTIL_INTERNAL_H_
#define LIB_URL_URL_UTIL_INTERNAL_H_

#include <string>

#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/url/third_party/mozilla/url_parse.h"

namespace url {

// Given a string and a range inside the string, compares it to the given
// lower-case |compare_to| buffer.
bool CompareSchemeComponent(const char* spec, const Component& component,
                            const char* compare_to);

static inline bool LowerCaseEqualsASCII(fxl::StringView str,
                                        fxl::StringView lowercase_ascii) {
  if (str.size() != lowercase_ascii.size())
    return false;
  for (size_t i = 0; i < str.size(); i++) {
    if (fxl::ToLowerASCII(str[i]) != lowercase_ascii[i])
      return false;
  }
  return true;
}

}  // namespace url

#endif  // LIB_URL_URL_UTIL_INTERNAL_H_
