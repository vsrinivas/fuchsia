// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_

#include <stdarg.h>
#include <zircon/compiler.h>

#include <string>

namespace sysmem_driver {

void vLog(bool is_error, const char* file, int line, const char* prefix1, const char* prefix2,
          const char* format, va_list args);

// Creates a unique name by concatenating prefix and a 64-bit unique number.
std::string CreateUniqueName(const char* prefix);

// Represents a source code location. Use FROM_HERE to get the current file location.
class Location {
 public:
  static Location FromHere(const char* file, int line_number) {
    return Location(file, line_number);
  }
  Location(const char* file, int line_number) : file_(file), line_(line_number) {}

  const char* file() const { return file_; }
  int line() const { return line_; }

 private:
  const char* file_{};
  int line_{};
};

#define FROM_HERE Location::FromHere(__FILE__, __LINE__)

class LoggingMixin {
 protected:
  explicit LoggingMixin(const char* logging_prefix) : logging_prefix_(logging_prefix) {}
  void LogInfo(Location location, const char* format, ...) __PRINTFLIKE(3, 4) {
    va_list args;
    va_start(args, format);
    vLog(false, location.file(), location.line(), logging_prefix_, "info", format, args);
    va_end(args);
  }
  void LogError(Location location, const char* format, ...) __PRINTFLIKE(3, 4) {
    va_list args;
    va_start(args, format);
    vLog(true, location.file(), location.line(), logging_prefix_, "error", format, args);
    va_end(args);
  }

  const char* logging_prefix() const { return logging_prefix_; }

 private:
  const char* logging_prefix_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGGING_H_
