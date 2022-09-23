// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <wlan/drivers/internal/hexdump.h>
#include <wlan/drivers/internal/log_severity.h>
#include <wlan/drivers/internal/macro_helpers.h>

extern "C" void wlan_drivers_log_hexdump(LOG_SEVERITY_TYPE severity, uint32_t filter,
                                         const char* tag, const char* file, int line,
                                         const char* func, const void* data, size_t length) {
  if (!data) {
    return;
  }

  constexpr size_t max_per_line = wlan::drivers::log::kHexDumpMaxBytesPerLine;

  wlan_drivers_log_with_severity(severity, filter, tag, file, line,
                                 "(%s): dumping data_ptr:%p len:%zu bytes", func, data, length);

  for (size_t i = 0; i < length; i += max_per_line) {
    char buf[wlan::drivers::log::kHexDumpMinBufSize];
    wlan::drivers::log::HexDump(reinterpret_cast<const char*>(data) + i,
                                std::min(length - i, max_per_line), buf, sizeof(buf));
    wlan_drivers_log_with_severity(severity, filter, tag, file, line, "(%s): %s", func, buf);
  }
}

extern "C" struct _ssid_string wlan_drivers_log_ssid_bytes_to_string(const uint8_t* ssid_bytes,
                                                                     size_t len) {
  // The C++ constant can't be used in the header file since it needs to be accessible from C,
  // so instead we redefine it and check that they're equal at compile time.
  static_assert(WLAN_IEEE80211_MAX_SSID_BYTE_LEN == fuchsia::wlan::ieee80211::MAX_SSID_BYTE_LEN);

  struct _ssid_string ret = {};

  // bound len by max num bytes
  len = std::min(len, static_cast<size_t>(WLAN_IEEE80211_MAX_SSID_BYTE_LEN));

  ZX_ASSERT(sizeof(ret.str) >= (2 * len) + 1);

  size_t offset = 0;
  for (size_t i = 0; i < len; i++) {
    offset += snprintf(ret.str + offset, sizeof(ret.str) - offset, "%02x", ssid_bytes[i]);
  }

  return ret;
}
