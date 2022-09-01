// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/drivers/internal/common.h>

namespace wlan::drivers {

static char hex_char(uint8_t ch) { return (ch >= 10) ? (ch - 10) + 'a' : ch + '0'; }

// static
void Log::HexDump(const void* ptr, size_t len, char* output, size_t output_size) {
  static constexpr size_t kStrStartOffset =
      (kHexDumpMaxBytesPerLine * kCharPerByte) + kSpaceBetHexAndStr;

  if (output_size < kHexDumpMinBufSize || len > kHexDumpMaxBytesPerLine) {
    output[0] = '\0';
    return;
  }

  const char* ch = reinterpret_cast<const char*>(ptr);
  memset(output, ' ', kHexDumpMinBufSize);
  for (size_t j = 0; j < Log::kHexDumpMaxBytesPerLine && j < len; j++) {
    const char val = ch[j];

    // Print the hex part.
    char* hex = &output[j * 3];
    hex[0] = hex_char((val >> 4) & 0xf);
    hex[1] = hex_char(val & 0xf);
    hex[2] = ' ';

    // print the ASCII part.
    char* chr = &output[kStrStartOffset + j];
    *chr = std::isprint(val) ? val : Log::kNP;
  }
  output[kHexDumpMinBufSize - 1] = '\0';  // null-terminator
}

// static
void Log::SetFilter(uint32_t filter) { getInstance().filter_ = filter; }

// static
bool Log::IsFilterOn(uint32_t filter) { return getInstance().filter_ & filter; }

// static
Log& Log::getInstance() {
  static Log w;
  return w;
}

}  // namespace wlan::drivers
