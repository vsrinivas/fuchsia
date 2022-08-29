// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_

#include <string>
#include <vector>

namespace wlan::drivers {

class Log {
 public:
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
  static bool IsFilterOn(uint32_t filter);
  static void HexDump(const void* ptr, size_t len, char* output, size_t output_size);

 private:
  static Log& getInstance();
  uint32_t filter_;
};
}  // namespace wlan::drivers

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_WLAN_DRIVERS_INTERNAL_COMMON_H_
