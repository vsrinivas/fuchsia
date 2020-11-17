// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_

#include <stdarg.h>

#include <string>

namespace sysmem_driver {

void vLog(bool is_error, const char* prefix1, const char* prefix2, const char* format,
          va_list args);

// Creates a unique name by concatenating prefix and a 64-bit unique number.
std::string CreateUniqueName(const char* prefix);

class LoggingMixin {
 protected:
  explicit LoggingMixin(const char* logging_prefix) : logging_prefix_(logging_prefix) {}
  void LogInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLog(false, logging_prefix_, "info", format, args);
    va_end(args);
  }
  void LogError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vLog(true, logging_prefix_, "error", format, args);
    va_end(args);
  }

  const char* logging_prefix() const { return logging_prefix_; }

 private:
  const char* logging_prefix_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_
