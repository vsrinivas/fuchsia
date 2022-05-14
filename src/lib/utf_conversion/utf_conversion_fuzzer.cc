// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <cstdint>

#include "src/lib/utf_conversion/utf_conversion.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }

  // Choose which direction to test.
  const bool from16 = Data[0] != 0;
  ++Data;
  --Size;

  if (from16) {
    // utf16_to_utf8 expects the number of code units.
    // In utf16, a code unit is 2 bytes.
    size_t code_units = Size / 2;
    const uint16_t* src = reinterpret_cast<const uint16_t*>(Data);
    static uint8_t dstBuffer[4 * 1024 * 1024];
    size_t dst_len = sizeof(dstBuffer);
    utf16_to_utf8(src, code_units, dstBuffer, &dst_len);
    ZX_ASSERT(dst_len <= sizeof(dstBuffer));
  } else {
    static uint16_t dstBuffer[4 * 1024 * 1024];
    size_t dst_len = sizeof(dstBuffer) / 2;
    utf8_to_utf16(Data, Size, dstBuffer, &dst_len);
    ZX_ASSERT(dst_len <= sizeof(dstBuffer) / 2);
  }

  return 0;
}
