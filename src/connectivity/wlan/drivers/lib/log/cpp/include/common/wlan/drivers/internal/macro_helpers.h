// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_MACRO_HELPERS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_MACRO_HELPERS_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <wlan/drivers/internal/log_severity.h>

// This file contains internal functions and macros that are used by the public-facing macros.
// Users should not use any of these functions or macros directly.

// This constant is defined in FIDL, but the constant definitions in C++ are not usable from C
// due to the use of constexpr/namespaces. Because FIDL C bindings are deprecated, we don't use
// them here either.
//
// We only use this one constant that is defined by the IEEE 802.11 standard, so I don't expect it
// to change very often, which is why it's redefined here.
//
// In order to catch when this constant does change, there is a static assert that checks that this
// constant is the same as the FIDL-defined constant in common_log_funcs.cc.
#define WLAN_IEEE80211_MAX_SSID_BYTE_LEN (32)

// Defines the maximum length of the SSID as a string. Each byte in the SSID becomes two characters
// in the string. E.g., an SSID byte with value 0xF becomes "0F" in the string. So we multiply by
// two to take this into account. The +1 is for the null terminator.
#define MAX_SSID_STR_LEN ((2 * WLAN_IEEE80211_MAX_SSID_BYTE_LEN) + 1)

__BEGIN_CDECLS

void wlan_drivers_log_with_severity(LOG_SEVERITY_TYPE severity, uint32_t filter, const char* tag,
                                    const char* file, int line, const char* fmt, ...);
void wlan_drivers_log_hexdump(LOG_SEVERITY_TYPE severity, uint32_t filter, const char* tag,
                              const char* file, int line, const char* func, const void* data,
                              size_t length);

// Wrapper struct that lets us return a char array from a function easily.
struct _ssid_string {
  char str[MAX_SSID_STR_LEN];
};

struct _ssid_string wlan_drivers_log_ssid_bytes_to_string(const uint8_t* ssid_bytes, size_t len);

__END_CDECLS

//
// Internal helper macros that insert context through __FILE__/__LINE__/__func__
//
#define wlan_drivers_log_internal(severity, filter, tag, fmt, ...)                        \
  wlan_drivers_log_with_severity(LOG_SEVERITY(severity), filter, tag, __FILE__, __LINE__, \
                                 "(%s): " fmt, __func__, ##__VA_ARGS__)

#define wlan_drivers_log_hexdump_internal(severity, filter, tag, data, length)                \
  wlan_drivers_log_hexdump(LOG_SEVERITY(severity), filter, tag, __FILE__, __LINE__, __func__, \
                           data, length)

#define wlan_drivers_lthrottle_internal(events_per_second, severity, filter, tag, fmt, ...) \
  do {                                                                                      \
    static struct throttle_counter _counter = {                                             \
        .capacity = 1,                                                                      \
        .tokens_per_second = (events_per_second),                                           \
        .num_throttled_events = 0uLL,                                                       \
        .last_issued_tick = INT64_MIN,                                                      \
    };                                                                                      \
    uint64_t events = 0;                                                                    \
    if (throttle_counter_consume(&_counter, &events)) {                                     \
      if (events > 0) {                                                                     \
        wlan_drivers_log_internal(severity, filter, tag, fmt " [Throttled %lu times]",      \
                                  ##__VA_ARGS__, events);                                   \
      } else {                                                                              \
        wlan_drivers_log_internal(severity, filter, tag, fmt, ##__VA_ARGS__);               \
      }                                                                                     \
    }                                                                                       \
  } while (0)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_MACRO_HELPERS_H_
