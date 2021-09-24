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
  static constexpr fx_log_severity_t kLevelError = DDK_LOG_ERROR;
  static constexpr fx_log_severity_t kLevelWarn = DDK_LOG_WARNING;
  static constexpr fx_log_severity_t kLevelInfo = DDK_LOG_INFO;
  static constexpr fx_log_severity_t kLevelDebug = DDK_LOG_DEBUG;
  static constexpr fx_log_severity_t kLevelTrace = DDK_LOG_TRACE;

  static constexpr int kLogThrottleEventsPerSec = 2;
  static void SetFilter(uint32_t filter);
  static bool IsFilterOn(uint32_t filter) { return getInstance().filter_ & filter; }

 private:
  static Log& getInstance() {
    static Log w;
    return w;
  }

  uint32_t filter_;
};
}  // namespace wlan::drivers

// Note: The users of this library are expected to define a macro named WLAN_DRIVER_LOG_LEVEL
// with the appropriate level of severity that needs to be compiled in.
//
// Here is the list of allowed values for WLAN_DRIVER_LOG_LEVEL:
//   wlan::drivers::Log::kLevelError
//   wlan::drivers::Log::kLevelWarn
//   wlan::drivers::Log::kLevelInfo
//   wlan::drivers::Log::kLevelDebug
//   wlan::drivers::Log::kLevelTrace
//
// Setting WLAN_DRIVER_LOG_LEVEL to one of the above severity levels compiles in logs for that
// level, as well as all levels above it. For example, defining it as shown below will compile
// in severity levels info, warn and error.
//
//   #define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kLevelInfo

// TODO (fxbug.dev/81914) - Add support for log level fatal i.e. lfatal().
#define lerror(fmt, ...)                                            \
  do {                                                              \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::kLevelError) { \
      zxlogf(ERROR, "(%s): " fmt, __func__, ##__VA_ARGS__);         \
    }                                                               \
  } while (0)

#define lwarn(fmt, ...)                                            \
  do {                                                             \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::kLevelWarn) { \
      zxlogf(WARNING, "(%s): " fmt, __func__, ##__VA_ARGS__);      \
    }                                                              \
  } while (0)

#define linfo(fmt, ...)                                            \
  do {                                                             \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::kLevelInfo) { \
      zxlogf(INFO, "(%s): " fmt, __func__, ##__VA_ARGS__);         \
    }                                                              \
  } while (0)

#define ldebug(filter, tag, fmt, ...)                                  \
  do {                                                                 \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::kLevelDebug) {    \
      if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {          \
        zxlogf_tag(DEBUG, tag, "(%s): " fmt, __func__, ##__VA_ARGS__); \
      }                                                                \
    }                                                                  \
  } while (0)

#define ltrace(filter, tag, fmt, ...)                                  \
  do {                                                                 \
    if (WLAN_DRIVER_LOG_LEVEL <= wlan::drivers::Log::kLevelTrace) {    \
      if (unlikely(wlan::drivers::Log::IsFilterOn(filter))) {          \
        zxlogf_tag(TRACE, tag, "(%s): " fmt, __func__, ##__VA_ARGS__); \
      }                                                                \
    }                                                                  \
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

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
