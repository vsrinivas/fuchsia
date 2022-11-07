// // Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_COMMON_LOGGING_H_
#define SRC_MEDIA_AUDIO_SERVICES_COMMON_LOGGING_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <ffl/string.h>

namespace media_audio {

// A macro that can be used whenever code is unreachable. This is equivalent to `FX_CHECK(false)`
// but also tells the compiler that nothing is reachable after this statement. Like other logging
// macros, it accepts stream operators to write a custom failure message.
#define UNREACHABLE                            \
  ::media_audio::LogMessageVoidifyNoReturn() & \
      ::syslog::LogMessage(::syslog::LOG_FATAL, __FILE__, __LINE__, nullptr, nullptr).stream()

// Implementation detail of UNREACHABLE.
class LogMessageVoidifyNoReturn {
 public:
  [[noreturn]] void operator&(std::ostream&) {
    // Should never happen: `LogMessage(LOG_FATAL)` should crash first.
    __builtin_abort();
  }
};

// A macro that logs (or not) according to a policy specified by a ThrottledLogger.
#define THROTTLED_LOG(logger)                                                                 \
  FX_LAZY_STREAM(                                                                             \
      ::syslog::LogMessage((logger).current_severity(), __FILE__, __LINE__, nullptr, nullptr) \
          .stream(),                                                                          \
      (logger).next_enabled())

class ThrottledLogger {
 public:
  virtual ~ThrottledLogger() = default;

  // TODO(fxbug.dev/114393): Add another implementation that throttles to N log messages per second.
  // Consider using this instead of FromCounts, especially anywhere that logging frequency is
  // derived from external inputs.

  // Given a list of pairs `(severity, count)`, each `count` messages are logged at `severity`.
  // If multiple severities are enabled at a specific time, the higest severity is used.
  static std::unique_ptr<ThrottledLogger> FromCounts(
      std::vector<std::pair<syslog::LogSeverity, int64_t>> counts);

  // Returns true if the next log message should be enabled.
  // Intended to be called by THROTTLED_LOG only.
  virtual bool next_enabled() = 0;

  // Returns the severity to use for the current log message.
  // Intended to be called by THROTTLED_LOG only.
  virtual syslog::LogSeverity current_severity() = 0;
};

}  // namespace media_audio

// Output operators for zx::time and zx::duration. These must be defined in namespace `zx` or else
// they will not work in gtest EXPECT or ASSERT statements.
namespace zx {
inline ::std::ostream& operator<<(::std::ostream& out, time t) {
  out << t.get();
  return out;
}
inline ::std::ostream& operator<<(::std::ostream& out, duration d) {
  out << d.get() << "ns";
  return out;
}
}  // namespace zx

#endif  // SRC_MEDIA_AUDIO_SERVICES_COMMON_LOGGING_H_
