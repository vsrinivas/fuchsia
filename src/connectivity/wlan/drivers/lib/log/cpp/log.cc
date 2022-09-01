// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <wlan/drivers/internal/common.h>
#include <wlan/drivers/internal/macro_helpers.h>

extern "C" void wlan_drivers_log_set_filter(uint32_t filter) {
  wlan::drivers::Log::SetFilter(filter);
}

extern "C" void wlan_drivers_log_with_severity(fx_log_severity_t severity, uint32_t filter,
                                               const char* tag, const char* file, int line,
                                               const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  switch (severity) {
    case DDK_LOG_ERROR:
    case DDK_LOG_WARNING:
    case DDK_LOG_INFO:
      zxlogvf_etc(severity, tag, file, line, fmt, args);
      break;
    case DDK_LOG_DEBUG:
    case DDK_LOG_TRACE:
      if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {
        zxlogvf_etc(severity, tag, file, line, fmt, args);
      }
      break;
    default:
      zxlogf(WARNING, "Unrecognized log severity: %u. Logging message with WARNING level instead.",
             severity);
      zxlogvf_etc(DDK_LOG_WARNING, tag, file, line, fmt, args);
      break;
  }

  va_end(args);
}

extern "C" void wlan_drivers_log_hexdump(fx_log_severity_t severity, uint32_t filter,
                                         const char* tag, const char* file, int line,
                                         const char* func, const void* data, size_t length) {
  if (!data) {
    return;
  }

  constexpr size_t max_per_line = wlan::drivers::Log::kHexDumpMaxBytesPerLine;

  wlan_drivers_log_with_severity(severity, filter, tag, file, line,
                                 "(%s): dumping data_ptr:%p len:%zu bytes", func, data, length);

  for (size_t i = 0; i < length; i += max_per_line) {
    char buf[wlan::drivers::Log::kHexDumpMinBufSize];
    wlan::drivers::Log::HexDump(reinterpret_cast<const char*>(data) + i,
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
