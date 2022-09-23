// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stdint.h>
#include <string.h>

#include <cctype>

#include <wlan/drivers/internal/hexdump.h>

namespace wlan::drivers::log {

namespace {
char hex_char(uint8_t ch) { return (ch >= 10) ? (ch - 10) + 'a' : ch + '0'; }
}  // namespace

void HexDump(const void* ptr, size_t len, char* output, size_t output_size) {
  static constexpr size_t kStrStartOffset =
      (kHexDumpMaxBytesPerLine * kCharPerByte) + kSpaceBetHexAndStr;

  if (output_size < kHexDumpMinBufSize || len > kHexDumpMaxBytesPerLine) {
    output[0] = '\0';
    return;
  }

  const char* ch = reinterpret_cast<const char*>(ptr);
  memset(output, ' ', kHexDumpMinBufSize);
  for (size_t j = 0; j < kHexDumpMaxBytesPerLine && j < len; j++) {
    const char val = ch[j];

    // Print the hex part.
    char* hex = &output[j * 3];
    hex[0] = hex_char((val >> 4) & 0xf);
    hex[1] = hex_char(val & 0xf);
    hex[2] = ' ';

    // print the ASCII part.
    char* chr = &output[kStrStartOffset + j];
    *chr = std::isprint(val) ? val : kNP;
  }
  output[kHexDumpMinBufSize - 1] = '\0';  // null-terminator
}

}  // namespace wlan::drivers::log
