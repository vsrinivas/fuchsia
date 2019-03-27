// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class Register;

// Obtains the value as a series of readable 32-bit separated hex values.
// This will interpret is as little endian (first byte is less significant).
// |length| is how many bytes of the value we want to output. 0 means unbounded.
// Will round up to the closest upper 4-byte multiple.
Err GetLittleEndianHexOutput(const std::vector<uint8_t>& value,
                             std::string* out, int length = 0);

// Format for float, double and long double
// |precision| sets the amount of digits to be written. If 0, the maximum for
// that particular floating type will be used.
Err GetFPString(const std::vector<uint8_t>& value, std::string* out,
                int precision = 0);

}  // namespace zxdb
