// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_TEST_UTILS_H_
#define LIB_URL_URL_TEST_UTILS_H_

// Convenience functions for string conversions.
// These are mostly intended for use in unit tests.

#include <string>

#include "third_party/gtest/include/gtest/gtest.h"
#include "lib/url/url_canon_internal.h"

namespace url {

namespace test_utils {


// Converts a UTF-16 string from native wchar_t format to uint16_t, by
// truncating the high 32 bits. This is not meant to handle true UTF-32
// encoded strings.
inline std::basic_string<uint16_t> WStringToUTF16(const wchar_t* src) {
  std::basic_string<uint16_t> str;
  int length = static_cast<int>(wcslen(src));
  for (int i = 0; i < length; ++i) {
    str.push_back(static_cast<uint16_t>(src[i]));
  }
  return str;
}

}  // namespace test_utils

}  // namespace url

#endif  // LIB_URL_URL_TEST_UTILS_H_
