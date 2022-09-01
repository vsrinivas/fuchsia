// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STRING_FORMATTERS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STRING_FORMATTERS_H_

#include <lib/stdcompat/span.h>

#include <string>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// Obtains the value as a series of readable 32-bit separated hex values. This will interpret is as
// little endian (first byte is less significant). Will left-zero-pad up to the closest upper 4-byte
// multiple.
std::string GetLittleEndianHexOutput(cpp20::span<const uint8_t> data);

// Format for float, double and long double. The |precision| sets the amount of digits to be
// written. If 0, the maximum for that particular floating type will be used.
std::string GetFPString(cpp20::span<const uint8_t> value, int precision = 0);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_STRING_FORMATTERS_H_
