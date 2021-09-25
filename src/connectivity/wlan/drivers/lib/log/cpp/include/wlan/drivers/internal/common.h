// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_

#include <wlan/drivers/internal/throttle_counter.h>
#include <wlan/drivers/internal/token_bucket.h>

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

// The 'log' parameter is the macro that the user would like lhexdump_ to use to log the
// hex values. This allows us to have a common helper API without having to duplicate it
// for every level of logging that we support.
#define lhexdump_(log, data, length)                                                   \
  do {                                                                                 \
    const void* ptr = (data);                                                          \
    size_t len = (length);                                                             \
    log("dumping %zu (0x%zx) bytes, data:%p\n", len, len, ptr);                        \
    if (!ptr) {                                                                        \
      return;                                                                          \
    }                                                                                  \
    for (size_t i = 0; i < len; i += Log::kHexDumpMaxBytesPerLine) {                   \
      char buf[Log::kHexDumpMinBufSize];                                               \
      Log::HexDump(reinterpret_cast<const char*>(ptr) + i,                             \
                   std::min(len - i, Log::kHexDumpMaxBytesPerLine), buf, sizeof(buf)); \
      log("%s\n", buf);                                                                \
    }                                                                                  \
  } while (0)

// Same as lhexdump_() defined above, but added capability to handle filter and tag.
#define lhexdump_tag_(log, filter, tag, data, length)                                  \
  do {                                                                                 \
    const void* ptr = (data);                                                          \
    size_t len = (length);                                                             \
    log(filter, tag, "dumping %zu (0x%zx) bytes, data:%p\n", len, len, ptr);           \
    if (!ptr) {                                                                        \
      return;                                                                          \
    }                                                                                  \
    for (size_t i = 0; i < len; i += Log::kHexDumpMaxBytesPerLine) {                   \
      char buf[Log::kHexDumpMinBufSize];                                               \
      Log::HexDump(reinterpret_cast<const char*>(ptr) + i,                             \
                   std::min(len - i, Log::kHexDumpMaxBytesPerLine), buf, sizeof(buf)); \
      log(filter, tag, "%s\n", buf);                                                   \
    }                                                                                  \
  } while (0)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_
