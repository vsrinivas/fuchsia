// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TYPES_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TYPES_H_

#include <array>
#include <string_view>

// This declares special types used for BootOptions members.  These, as well
// as std::string_view, bool, and uintNN_t, can be used in DEFINE_OPTIONS in
// "options.inc".

#if BOOT_OPTIONS_GENERATOR || defined(__x86_64__)
// This declares special types used by machine-specific options in "x86.inc".
#include "x86.h"
#endif

// This holds a C string always guaranteed to have a '\0' terminator.  As a
// simple invariant, SmallString::back() == '\0' is always maintained even if
// there is an earlier terminator.
using SmallString = std::array<char, 160>;

// This is used for passing in secure random bits as ASCII hex digits.  As a
// special exception to the normal constraint that the command line text be
// left as is in the ZBI item memory, the original command line text of the
// RedactedHex option's value is redacted (the buffer modified in place) so it
// does not propagate to userland.
struct RedactedHex {
  constexpr const char* c_str() const { return &hex[0]; }

  constexpr explicit operator std::string_view() const { return {&hex[0], len}; }

  constexpr bool operator==(const RedactedHex& other) const {
    return std::string_view(*this) == std::string_view(other);
  }

  constexpr bool operator!=(const RedactedHex& other) const { return !(*this == other); }

  SmallString hex{};
  size_t len = 0;
};

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_TYPES_H_
