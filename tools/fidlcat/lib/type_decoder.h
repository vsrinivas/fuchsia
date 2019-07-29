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

// Types for syscall arguments.
enum class SyscallType { kUint8, kUint32, kHandle, kTime, kStruct };

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

void ObjTypeName(zx_obj_type_t obj_type, std::ostream& os);
void RightsName(zx_rights_t rights, std::ostream& os);
void StatusName(zx_status_t status, std::ostream& os);
void StatusName(const Colors& colors, zx_status_t status, std::ostream& os);
void DisplayHandle(const Colors& colors, const zx_handle_info_t& handle, std::ostream& os);
void DisplayType(const Colors& colors, SyscallType type, std::ostream& os);

class DisplayTime {
 public:
  DisplayTime(const Colors& colors, zx_time_t time_ns) : colors_(colors), time_ns_(time_ns) {}
  const Colors& colors() const { return colors_; }
  zx_time_t time_ns() const { return time_ns_; }

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
    time_t value = time.time_ns() / 1'000'000'000L;
    struct tm tm;
    if (localtime_r(&value, &tm) == &tm) {
      char buffer[100];
      strftime(buffer, sizeof(buffer), "%c", &tm);
      os << time.colors().blue << buffer << " and ";
      snprintf(buffer, sizeof(buffer), "%09" PRId64, time.time_ns() % 1000000000L);
      os << buffer << " ns" << time.colors().reset;
    } else {
      os << time.colors().red << "unknown time" << time.colors().reset;
    }
  }
  return os;
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
