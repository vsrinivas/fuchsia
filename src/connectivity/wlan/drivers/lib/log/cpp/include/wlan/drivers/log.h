// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_

#include <lib/ddk/debug.h>
#include <stdarg.h>

#include <wlan/drivers/internal/throttle_counter.h>
#include <wlan/drivers/internal/token_bucket.h>

namespace wlan::drivers {

class Log {
 public:
  // Log severity levels.
  static constexpr fx_log_severity_t kERROR = DDK_LOG_ERROR;
  static constexpr fx_log_severity_t kWARNING = DDK_LOG_WARNING;
  static constexpr fx_log_severity_t kINFO = DDK_LOG_INFO;
  static constexpr fx_log_severity_t kDEBUG = DDK_LOG_DEBUG;
  static constexpr fx_log_severity_t kTRACE = DDK_LOG_TRACE;

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

#define log_(level, fmt, ...)                                    \
  do {                                                           \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::k##level) { \
      zxlogf(level, fmt, ##__VA_ARGS__);                         \
    }                                                            \
  } while (0)

#define log_tag_(level, filter, tag, fmt, ...)                   \
  do {                                                           \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::k##level) { \
      if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {    \
        zxlogf_tag(level, tag, fmt, ##__VA_ARGS__);              \
      }                                                          \
    }                                                            \
  } while (0)

// Throttle calls to |event| to only happen at a specific rate per second. If an event is called
// it will be passed the parameters fmt and variadic arguments in |...|. In the case of an event
// being allowed after previous events have been throttled an additional string will be appended
// at the end of |fmt| which will indicate the number of times the event was previously throttled.
// This counter is reset on each non-throttled event. This behavior makes the assumption that the
// event being throttled is printing or logging a message in printf style. If an event is throttled
// it will not be called and no additional side effects take place.
//
// NOTE: A log message may produce different output because of different arguments to the printf
//       style call but it may still be throttled even if it's different from the previous message.
//       Each BRCMF_THROTTLE_MSG statement is its own throttler that is independent of other
//       throttlers but will evaluate its throttling condition every time regardless of parameters.
#define lthrottle_(events_per_second, event, fmt, ...)              \
  do {                                                              \
    static wlan::drivers::TokenBucket bucket(events_per_second);    \
    static wlan::drivers::ThrottleCounter counter(bucket);          \
    uint64_t events = 0;                                            \
    if (counter.consume(&events)) {                                 \
      if (events > 0) {                                             \
        event(fmt " [Throttled %lu times]", ##__VA_ARGS__, events); \
      } else {                                                      \
        event(fmt, ##__VA_ARGS__);                                  \
      }                                                             \
    }                                                               \
  } while (0)

#define lthrottle_tag_(events_per_second, event, filter, tag, fmt, ...)          \
  do {                                                                           \
    static wlan::drivers::TokenBucket bucket(events_per_second);                 \
    static wlan::drivers::ThrottleCounter counter(bucket);                       \
    uint64_t events = 0;                                                         \
    if (counter.consume(&events)) {                                              \
      if (events > 0) {                                                          \
        event(filter, tag, fmt " [Throttled %lu times]", ##__VA_ARGS__, events); \
      } else {                                                                   \
        event(filter, tag, fmt, ##__VA_ARGS__);                                  \
      }                                                                          \
    }                                                                            \
  } while (0)

#define lhexdump_(level, data, length)                                                 \
  do {                                                                                 \
    const void* ptr = (data);                                                          \
    size_t len = (length);                                                             \
    log_(level, "dumping %zu (0x%zx) bytes, data:%p\n", len, len, ptr);                \
    if (!ptr) {                                                                        \
      return;                                                                          \
    }                                                                                  \
    for (size_t i = 0; i < len; i += Log::kHexDumpMaxBytesPerLine) {                   \
      char buf[Log::kHexDumpMinBufSize];                                               \
      Log::HexDump(reinterpret_cast<const char*>(ptr) + i,                             \
                   std::min(len - i, Log::kHexDumpMaxBytesPerLine), buf, sizeof(buf)); \
      log_(level, "%s\n", buf);                                                        \
    }                                                                                  \
  } while (0)

#define lhexdump_tag_(level, filter, tag, data, length)                                  \
  do {                                                                                   \
    const void* ptr = (data);                                                            \
    size_t len = (length);                                                               \
    log_tag_(level, filter, tag, "dumping %zu (0x%zx) bytes, data:%p\n", len, len, ptr); \
    if (!ptr) {                                                                          \
      return;                                                                            \
    }                                                                                    \
    for (size_t i = 0; i < len; i += Log::kHexDumpMaxBytesPerLine) {                     \
      char buf[Log::kHexDumpMinBufSize];                                                 \
      Log::HexDump(reinterpret_cast<const char*>(ptr) + i,                               \
                   std::min(len - i, Log::kHexDumpMaxBytesPerLine), buf, sizeof(buf));   \
      log_tag_(level, filter, tag, "%s\n", buf);                                         \
    }                                                                                    \
  } while (0)

//
// Below is the list of of macros that are available for public use. All macros
// above this (have _ at the end) are intended for internal use within the log library.
//
// Note: The users of this library are expected to define a macro named WLAN_DRIVER_LOG_LEVEL
// with the appropriate level of severity for which logs need to be compiled in.
//
// Here is the list of allowed values for WLAN_DRIVER_LOG_LEVEL:
//   wlan::drivers::Log::kERROR
//   wlan::drivers::Log::kWARNING
//   wlan::drivers::Log::kINFO
//   wlan::drivers::Log::kDEBUG
//   wlan::drivers::Log::kTRACE
//
// Setting WLAN_DRIVER_LOG_LEVEL to one of the above severity levels compiles in logs for that
// level, as well as all levels above it. For example, defining it as shown below will compile
// in severity levels info, warn and error.
//
//   #define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kINFO

// TODO (fxbug.dev/81914) - Add support for log level fatal i.e. lfatal().
#define lerror(fmt, ...) log_(ERROR, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define lwarn(fmt, ...) log_(WARNING, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define linfo(fmt, ...) log_(INFO, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define ldebug(filter, tag, fmt, ...) \
  log_tag_(DEBUG, filter, tag, "(%s): " fmt, __func__, ##__VA_ARGS__)
#define ltrace(filter, tag, fmt, ...) \
  log_tag_(TRACE, filter, tag, "(%s): " fmt, __func__, ##__VA_ARGS__)

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

#define lhexdump_error(data, length) lhexdump_(ERROR, data, length)
#define lhexdump_warn(data, length) lhexdump_(WARNING, data, length)
#define lhexdump_info(data, length) lhexdump_(INFO, data, length)
#define lhexdump_debug(filter, tag, data, length) lhexdump_tag_(DEBUG, filter, tag, data, length)
#define lhexdump_trace(filter, tag, data, length) lhexdump_tag_(TRACE, filter, tag, data, length)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
