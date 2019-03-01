// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UUID_UUID_H_
#define SRC_LIB_UUID_UUID_H_

#include <stdint.h>

#include <string>

namespace uuid {

// Generate a 128-bit (pseudo) random UUID in the form of version 4 as described
// in RFC 4122, section 4.4.
// The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
// where y is one of [8, 9, A, B].
// The hexadecimal values "a" through "f" are output as lower case characters.
// If UUID generation fails an empty string is returned.
std::string Generate();

// Returns true if the input string conforms to the version 4 UUID format.
// Note that this does NOT check if the hexadecimal values "a" through "f"
// are in lower case characters, as Version 4 RFC says they're
// case insensitive. (Use IsValidOutputString for checking if the
// given string is valid output string)
bool IsValid(const std::string& guid);

// Returns true if the input string is valid version 4 UUID output string.
// This also checks if the hexadecimal values "a" through "f" are in lower
// case characters.
bool IsValidOutputString(const std::string& guid);

}  // namespace uuid

#endif  // SRC_LIB_UUID_UUID_H_
