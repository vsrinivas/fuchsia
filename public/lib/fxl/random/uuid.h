// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_RANDOM_UUID_H_
#define LIB_FXL_RANDOM_UUID_H_

#include <stdint.h>

#include <string>

#include "lib/fxl/fxl_export.h"

namespace fxl {

// Generate a 128-bit (pseudo) random UUID in the form of version 4 as described
// in RFC 4122, section 4.4.
// The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
// where y is one of [8, 9, A, B].
// The hexadecimal values "a" through "f" are output as lower case characters.
// If UUID generation fails an empty string is returned.
FXL_EXPORT std::string GenerateUUID();

// Returns true if the input string conforms to the version 4 UUID format.
// Note that this does NOT check if the hexadecimal values "a" through "f"
// are in lower case characters, as Version 4 RFC says onput they're
// case insensitive. (Use IsValidUUIDOutputString for checking if the
// given string is valid output string)
FXL_EXPORT bool IsValidUUID(const std::string& guid);

// Returns true if the input string is valid version 4 UUID output string.
// This also checks if the hexadecimal values "a" through "f" are in lower
// case characters.
FXL_EXPORT bool IsValidUUIDOutputString(const std::string& guid);

}  // namespace fxl

#endif  // LIB_FXL_RANDOM_UUID_H_
