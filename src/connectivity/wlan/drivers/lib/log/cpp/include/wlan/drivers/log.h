// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_

#include <lib/ddk/debug.h>
#include <stdarg.h>

namespace wlan::drivers {

class Log {
 public:
  // Log severity levels.
  static constexpr fx_log_severity_t kLevelError = DDK_LOG_ERROR;
  static constexpr fx_log_severity_t kLevelWarn = DDK_LOG_WARNING;
  static constexpr fx_log_severity_t kLevelInfo = DDK_LOG_INFO;
  static constexpr fx_log_severity_t kLevelDebug = DDK_LOG_DEBUG;
  static constexpr fx_log_severity_t kLevelTrace = DDK_LOG_TRACE;

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

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_LOG_H_
