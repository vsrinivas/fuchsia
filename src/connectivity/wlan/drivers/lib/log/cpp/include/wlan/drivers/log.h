// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_

#include <lib/ddk/debug.h>
#include <stdarg.h>

#include <wlan/drivers/internal/common.h>

namespace wlan::drivers {

class Log {
 public:
  // Throttle constant.
  static constexpr int kLogThrottleEventsPerSec = 2;

  // Hex dump constants.
  static constexpr char kNP = '.';                       // Char used to show non-printable chars.
  static constexpr size_t kHexDumpMaxBytesPerLine = 16;  // Bytes to print per line in hex dump.
  static constexpr size_t kCharPerByte = 3;              // Since each byte is represened as "xx "
  static constexpr size_t kSpaceBetHexAndStr = 3;        // Space between hex & str representation.
  static constexpr size_t kHexDumpMinBufSize =
      (kHexDumpMaxBytesPerLine * kCharPerByte)  // # of hex chars
      + kSpaceBetHexAndStr                      // space between hex & str repr
      + kHexDumpMaxBytesPerLine                 // # of str chars
      + 1;                                      // null termination.

  static void SetFilter(uint32_t filter);
  static bool IsFilterOn(uint32_t filter) { return getInstance().filter_ & filter; }
  static void HexDump(const void* ptr, size_t len, char* output, size_t output_size);

 private:
  static Log& getInstance() {
    static Log w;
    return w;
  }

  uint32_t filter_;
};
}  // namespace wlan::drivers

// TODO (fxbug.dev/81914) - Add support for log level fatal i.e. lfatal().
#define lerror(fmt, ...) zxlogf(ERROR, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define lwarn(fmt, ...) zxlogf(WARNING, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define linfo(fmt, ...) zxlogf(INFO, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define ldebug(filter, tag, fmt, ...)                                \
  do {                                                               \
    if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {          \
      zxlogf_tag(DEBUG, tag, "(%s): " fmt, __func__, ##__VA_ARGS__); \
    }                                                                \
  } while (0)

#define ltrace(filter, tag, fmt, ...)                                \
  do {                                                               \
    if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {          \
      zxlogf_tag(TRACE, tag, "(%s): " fmt, __func__, ##__VA_ARGS__); \
    }                                                                \
  } while (0)

#define lthrottle_error(fmt...) \
  lthrottle_(wlan::drivers::Log::kLogThrottleEventsPerSec, lerror, fmt)
#define lthrottle_warn(fmt...) lthrottle_(wlan::drivers::Log::kLogThrottleEventsPerSec, lwarn, fmt)
#define lthrottle_info(fmt...) lthrottle_(wlan::drivers::Log::kLogThrottleEventsPerSec, linfo, fmt)
#define lthrottle_debug(filter, tag, fmt...) \
  lthrottle_tag_(wlan::drivers::Log::kLogThrottleEventsPerSec, ldebug, filter, tag, fmt)
#define lthrottle_trace(filter, tag, fmt...) \
  lthrottle_tag_(wlan::drivers::Log::kLogThrottleEventsPerSec, ltrace, filter, tag, fmt)

// ToDo (fxbug.dev/82722) - Remove lthrottle_log_if() in favor of throttle macros that provide
// additional information on how many times the logs got throttled.
#define lthrottle_log_if(events_per_second, condition, log)        \
  do {                                                             \
    if (condition) {                                               \
      static wlan::drivers::TokenBucket bucket(events_per_second); \
      if (bucket.consume()) {                                      \
        log;                                                       \
      }                                                            \
    }                                                              \
  } while (0)

#define lhexdump_error(data, length) lhexdump_(lerror, data, length)
#define lhexdump_warn(data, length) lhexdump_(lwarn, data, length)
#define lhexdump_info(data, length) lhexdump_(linfo, data, length)
#define lhexdump_debug(filter, tag, data, length) lhexdump_tag_(ldebug, filter, tag, data, length)
#define lhexdump_trace(filter, tag, data, length) lhexdump_tag_(ltrace, filter, tag, data, length)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
