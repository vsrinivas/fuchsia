// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#include "all_tests.h"

#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <unittest.h>

namespace boring_crypto {

size_t FromHex(const char *hex, uint8_t* out, size_t max) {
  size_t len = strlen(hex);
  assert(len % 2 == 0 && len / 2 <= max);
  size_t j = 0;
  memset(out,0, max);
  for(size_t i = 0; i < len; ++i) {
    assert(isxdigit(hex[i]));
    uint8_t c = static_cast<uint8_t>(tolower(hex[i]));
    c = static_cast<uint8_t>(c < ':' ? c - '0' : c - 'a' + 10);
    if (i % 2 == 0) {
      out[j] = static_cast<uint8_t>(c << 4);
    } else {
      out[j] |= c;
      ++j;
    }
  }
  return j;
}

} // namespace boring_crypto

UNITTEST_START_TESTCASE(crypto_tests)
UNITTEST("ChaChaUnitTests", boring_crypto::ChaChaUnitTests)
UNITTEST_END_TESTCASE(crypto_tests, "crypto", "Test kernel crypto algorithms",
                      nullptr, nullptr);
