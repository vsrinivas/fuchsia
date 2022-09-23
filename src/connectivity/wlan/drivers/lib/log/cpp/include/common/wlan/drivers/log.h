// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_LOG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_LOG_H_

#include <zircon/compiler.h>

#include <wlan/drivers/internal/macro_helpers.h>
#include <wlan/drivers/internal/throttle_counter.h>

// TODO (fxbug.dev/81914) - Add support for log level fatal i.e. lfatal().
#define lerror(fmt, ...) (wlan_drivers_log_internal(ERROR, 0, NULL, fmt, ##__VA_ARGS__))
#define lwarn(fmt, ...) (wlan_drivers_log_internal(WARNING, 0, NULL, fmt, ##__VA_ARGS__))
#define linfo(fmt, ...) (wlan_drivers_log_internal(INFO, 0, NULL, fmt, ##__VA_ARGS__))
#define ldebug(filter, tag, fmt, ...) \
  (wlan_drivers_log_internal(DEBUG, filter, tag, fmt, ##__VA_ARGS__))
#define ltrace(filter, tag, fmt, ...) \
  (wlan_drivers_log_internal(TRACE, filter, tag, fmt, ##__VA_ARGS__))

#define lhexdump_error(data, length) \
  (wlan_drivers_log_hexdump_internal(ERROR, 0, NULL, data, length))
#define lhexdump_warn(data, length) \
  (wlan_drivers_log_hexdump_internal(WARNING, 0, NULL, data, length))
#define lhexdump_info(data, length) (wlan_drivers_log_hexdump_internal(INFO, 0, NULL, data, length))
#define lhexdump_debug(filter, tag, data, length) \
  (wlan_drivers_log_hexdump_internal(DEBUG, filter, tag, data, length))
#define lhexdump_trace(filter, tag, data, length) \
  (wlan_drivers_log_hexdump_internal(TRACE, filter, tag, data, length))

#define LOG_THROTTLE_EVENTS_PER_SEC (2)
#define lthrottle_error(fmt...) \
  wlan_drivers_lthrottle_internal(LOG_THROTTLE_EVENTS_PER_SEC, ERROR, 0, NULL, fmt)
#define lthrottle_warn(fmt...) \
  wlan_drivers_lthrottle_internal(LOG_THROTTLE_EVENTS_PER_SEC, WARNING, 0, NULL, fmt)
#define lthrottle_info(fmt...) \
  wlan_drivers_lthrottle_internal(LOG_THROTTLE_EVENTS_PER_SEC, INFO, 0, NULL, fmt)
#define lthrottle_debug(filter, tag, fmt...) \
  wlan_drivers_lthrottle_internal(LOG_THROTTLE_EVENTS_PER_SEC, DEBUG, filter, tag, fmt)
#define lthrottle_trace(filter, tag, fmt...) \
  wlan_drivers_lthrottle_internal(LOG_THROTTLE_EVENTS_PER_SEC, TRACE, filter, tag, fmt)

// ToDo (fxbug.dev/82722) - Remove lthrottle_log_if() in favor of throttle macros that provide
// additional information on how many times the logs got throttled.
#define lthrottle_log_if(events_per_second, condition, log) \
  do {                                                      \
    if (condition) {                                        \
      static struct throttle_counter _counter = {           \
          .capacity = 1,                                    \
          .tokens_per_second = (events_per_second),         \
          .num_throttled_events = 0uLL,                     \
          .last_issued_tick = INT64_MIN,                    \
      };                                                    \
      uint64_t events = 0;                                  \
      if (throttle_counter_consume(&_counter, &events)) {   \
        log;                                                \
      }                                                     \
    }                                                       \
  } while (0)

#define FMT_MAC "%02x:%02x:%02x:%02x:%02x:%02x"
#define FMT_MAC_ARGS(arr) (arr)[0], (arr)[1], (arr)[2], (arr)[3], (arr)[4], (arr)[5]

// Example usage - lerror("Failed to connect to ssid: " FMT_SSID, FMT_SSID_VECT(ssid_vect));
#define FMT_SSID "<ssid-%s>"
#define FMT_SSID_BYTES(ssid, len) (wlan_drivers_log_ssid_bytes_to_string((ssid), (len)).str)

#ifdef __cplusplus
#define FMT_SSID_VECT(ssid) \
  (wlan_drivers_log_ssid_bytes_to_string((ssid).data(), (ssid).size()).str)
#endif

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_LOG_H_
