// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_HEXDUMP_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_HEXDUMP_H_

#include <stddef.h>

namespace wlan::drivers::log {

// Hex dump constants.
constexpr char kNP = '.';                       // Char used to show non-printable chars.
constexpr size_t kHexDumpMaxBytesPerLine = 16;  // Bytes to print per line in hex dump.
constexpr size_t kCharPerByte = 3;              // Since each byte is represened as "xx "
constexpr size_t kSpaceBetHexAndStr = 3;        // Space between hex & str representation.
constexpr size_t kHexDumpMinBufSize = (kHexDumpMaxBytesPerLine * kCharPerByte)  // # of hex chars
                                      + kSpaceBetHexAndStr       // space between hex & str repr
                                      + kHexDumpMaxBytesPerLine  // # of str chars
                                      + 1;                       // null termination.

void HexDump(const void* ptr, size_t len, char* output, size_t output_size);

}  // namespace wlan::drivers::log

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_HEXDUMP_H_
