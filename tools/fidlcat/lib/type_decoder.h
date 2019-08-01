// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
#define TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_

#include <zircon/system/public/zircon/rights.h>
#include <zircon/system/public/zircon/syscalls/object.h>
#include <zircon/system/public/zircon/types.h>

#include <cinttypes>
#include <cstdint>
#include <ostream>

namespace fidlcat {

#define kOneBillion 1'000'000'000L
#define kSecondsPerMinute 60
#define kMinutesPerHour 60
#define kHoursPerDay 24

// Types for syscall arguments.
enum class SyscallType { kInt64, kUint8, kUint32, kClock, kDuration, kHandle, kTime, kStruct };

enum class SyscallReturnType {
  kVoid,
  kStatus,
  kTicks,
  kTime,
  kUint32,
  kUint64,
};

struct Colors {
  Colors(const char* new_reset, const char* new_red, const char* new_green, const char* new_blue,
         const char* new_white_on_magenta)
      : reset(new_reset),
        red(new_red),
        green(new_green),
        blue(new_blue),
        white_on_magenta(new_white_on_magenta) {}

  const char* const reset;
  const char* const red;
  const char* const green;
  const char* const blue;
  const char* const white_on_magenta;
};

void ClockName(zx_clock_t clock, std::ostream& os);
void ObjTypeName(zx_obj_type_t obj_type, std::ostream& os);
void RightsName(zx_rights_t rights, std::ostream& os);
void StatusName(zx_status_t status, std::ostream& os);
void StatusName(const Colors& colors, zx_status_t status, std::ostream& os);
void DisplayHandle(const Colors& colors, const zx_handle_info_t& handle, std::ostream& os);
void DisplayType(const Colors& colors, SyscallType type, std::ostream& os);

class DisplayDuration {
 public:
  DisplayDuration(const Colors& colors, zx_duration_t duration_ns)
      : colors_(colors), duration_ns_(duration_ns) {}
  [[nodiscard]] const Colors& colors() const { return colors_; }
  [[nodiscard]] zx_duration_t duration_ns() const { return duration_ns_; }

 private:
  const Colors& colors_;
  const zx_duration_t duration_ns_;
};

inline std::ostream& operator<<(std::ostream& os, const DisplayDuration& duration) {
  if (duration.duration_ns() == ZX_TIME_INFINITE) {
    os << duration.colors().blue << "ZX_TIME_INFINITE" << duration.colors().reset;
    return os;
  }
  if (duration.duration_ns() == ZX_TIME_INFINITE_PAST) {
    os << duration.colors().blue << "ZX_TIME_INFINITE_PAST" << duration.colors().reset;
    return os;
  }
  int64_t duration_ns = duration.duration_ns();
  os << duration.colors().blue;
  if (duration_ns < 0) {
    os << '-';
    duration_ns = -duration_ns;
  }
  const char* separator = "";
  int64_t nanoseconds = duration_ns % kOneBillion;
  int64_t seconds = duration_ns / kOneBillion;
  if (seconds != 0) {
    int64_t minutes = seconds / kSecondsPerMinute;
    if (minutes != 0) {
      int64_t hours = minutes / kMinutesPerHour;
      if (hours != 0) {
        int64_t days = hours / kHoursPerDay;
        if (days != 0) {
          os << days << " days";
          separator = ", ";
        }
        os << separator << (hours % kHoursPerDay) << " hours";
        separator = ", ";
      }
      os << separator << (minutes % kMinutesPerHour) << " minutes";
      separator = ", ";
    }
    os << separator << (seconds % kSecondsPerMinute) << " seconds";
    if (nanoseconds != 0) {
      os << " and " << nanoseconds << " nano seconds";
    }
  } else if (nanoseconds != 0) {
    os << nanoseconds << " nano seconds";
  } else {
    os << "0 seconds";
  }
  os << duration.colors().reset;
  return os;
}

class DisplayTime {
 public:
  DisplayTime(const Colors& colors, zx_time_t time_ns) : colors_(colors), time_ns_(time_ns) {}
  [[nodiscard]] const Colors& colors() const { return colors_; }
  [[nodiscard]] zx_time_t time_ns() const { return time_ns_; }

 private:
  const Colors& colors_;
  const zx_time_t time_ns_;
};

inline std::ostream& operator<<(std::ostream& os, const DisplayTime& time) {
  if (time.time_ns() == ZX_TIME_INFINITE) {
    os << time.colors().blue << "ZX_TIME_INFINITE" << time.colors().reset;
  } else if (time.time_ns() == ZX_TIME_INFINITE_PAST) {
    os << time.colors().blue << "ZX_TIME_INFINITE_PAST" << time.colors().reset;
  } else {
    time_t value = time.time_ns() / kOneBillion;
    struct tm tm;
    if (localtime_r(&value, &tm) == &tm) {
      char buffer[100];
      strftime(buffer, sizeof(buffer), "%c", &tm);
      os << time.colors().blue << buffer << " and ";
      snprintf(buffer, sizeof(buffer), "%09" PRId64, time.time_ns() % kOneBillion);
      os << buffer << " ns" << time.colors().reset;
    } else {
      os << time.colors().red << "unknown time" << time.colors().reset;
    }
  }
  return os;
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
