/*
 * Copyright (c) 2019 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

#include <algorithm>

#include "gtest/gtest.h"

namespace {

// This is a simple test to verify that printing various sized buffers doesn't crash. Since
// debugging is typically disabled by default, this gives us some indication that it hasn't
// broken.
TEST(DebugTest, NoCrash) {
  size_t buffer_size = std::max(kMaxHexDumpBytes, kMaxStringDumpBytes) + 1;
  uint8_t buffer[buffer_size];
  for (size_t i = 0; i < buffer_size; i++) {
    buffer[i] = i;
  }
  buffer[buffer_size - 1] = 0;

  // First test all sizes from [0,100]
  size_t max_test_size = std::min<size_t>(100, buffer_size);
  for (size_t i = 0; i < max_test_size; i++) {
    BRCMF_DBG_HEX_DUMP(true, buffer, i, "Buffer size of %d\n", i);
    BRCMF_DBG_STRING_DUMP(true, buffer, i, "String size of %d\n", i);
  }

  // Test upper limits of hex dump
  for (size_t i = kMaxHexDumpBytes - 1; i < kMaxHexDumpBytes + 2; i++) {
    BRCMF_DBG_HEX_DUMP(true, buffer, i, "Buffer size of %d\n", i);
  }

  // Test upper limits of string dump
  for (size_t i = kMaxStringDumpBytes - 1; i < kMaxStringDumpBytes + 2; i++) {
    BRCMF_DBG_STRING_DUMP(true, buffer, i, "String size of %d\n", i);
  }
};

}  // namespace
