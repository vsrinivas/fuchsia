// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <stdio.h>

#include <algorithm>
#include <locale>

#include <wlan/drivers/log.h>

namespace wlan::drivers {

static char hex_char(uint8_t ch) { return (ch >= 10) ? (ch - 10) + 'a' : ch + '0'; }

// static
std::string Log::SsidBytes2Str(uint8_t const ssid[], size_t len) {
  constexpr size_t max_ssid_len =
      static_cast<size_t>(::fuchsia::wlan::ieee80211::MAX_SSID_BYTE_LEN);
  size_t ssid_len = std::min(len, max_ssid_len);

  // Since each byte in the ssid will be represented by two hex char, we multiple by 2.
  size_t buf_size = (2 * ssid_len) + 1;
  char fmt_ssid[buf_size];

  fmt_ssid[0] = '\0';
  size_t offset = 0;
  for (size_t i = 0; i < ssid_len; i++) {
    offset += snprintf(fmt_ssid + offset, sizeof(fmt_ssid) - offset, "%02x", ssid[i]);
  }
  return fmt_ssid;
}

// static
void Log::HexDump(const void* ptr, size_t len, char* output, size_t output_size) {
  static constexpr size_t kStrStartOffset =
      (kHexDumpMaxBytesPerLine * kCharPerByte) + kSpaceBetHexAndStr;

  if (output_size < kHexDumpMinBufSize || len > kHexDumpMaxBytesPerLine) {
    output[0] = '\0';
    return;
  }

  const uint8_t* ch = reinterpret_cast<const uint8_t*>(ptr);
  memset(output, ' ', kHexDumpMinBufSize);
  for (size_t j = 0; j < Log::kHexDumpMaxBytesPerLine && j < len; j++) {
    char val = ch[j];

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

}  // namespace wlan::drivers
