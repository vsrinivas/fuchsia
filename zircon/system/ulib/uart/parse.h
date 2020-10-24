// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_UART_PARSE_H_
#define ZIRCON_SYSTEM_ULIB_UART_PARSE_H_

#include <stdio.h>
#include <stdlib.h>

#include <string_view>

namespace uart::internal {

// Parse a comma-separated list of integers of the form `,1,2,3,...,1000`,
// where the list must begin with a comma.
//
// Input integers may be decimal (42), hexadecimal (0x2a) or octal (052).
//
// The integral values are parsed into a variadic list of pointers. `true` is
// is returned on success; `false` is returned if the string could not be
// parsed.
template <typename... Uint>
inline bool ParseInts(std::string_view string, Uint*... args) {
  auto next = [&](auto* arg) -> bool {
    if (string.size() < 2 || string.front() != ',') {
      return false;
    }
    string.remove_prefix(1);  // Remove the leading comma.

    // Parse the leading sign and 0x (hex) or 0 (octal) indicator.  Note that
    // strtoul would do this with base=0, but doing it ahead of time allows us
    // to trim leading zeros from the string so that we can always use a small
    // buffer for the NUL-terminated copy of the digits to parse.
    bool negative = false;
    if ((string[0] == '-' || string[0] == '+') && string.size() > 1) {
      negative = string[0] == '-';
      string.remove_prefix(1);
    }
    int base = 10;
    if (string[0] == '0' && string.size() > 1) {
      if (string[1] == 'x') {
        base = 16;
        string.remove_prefix(2);
      } else {
        base = 8;
      }
      while (string[0] == '0' && string.size() > 1) {
        string.remove_prefix(1);
      }
    }
    size_t end = string.find_first_not_of("0123456789abcdefABCDEF");
    if (end == std::string_view::npos) {
      end = string.size();
    } else if (end == 0) {
      // The comma was followed by a non-numerical character.
      return false;
    }
    // Since leading zeros were removed, any string of digits that doesn't fit
    // in this buffer would be an integer that overflows the type anyway.
    char buf[32];
    if (end < sizeof(buf)) {
      buf[string.substr(0, end).copy(buf, sizeof(buf) - 1)] = '\0';
      string.remove_prefix(end);
      char* p = nullptr;
      uint64_t value = strtoul(buf, &p, base);
      if (p == &buf[end]) {
        if (negative) {
          value = -value;
        }
        *arg = static_cast<std::decay_t<decltype(*arg)>>(value);
        return true;
      }
    }
    return false;
  };
  return (next(args) && ... && string.empty());
}

template <typename... Uint>
inline void UnparseInts(FILE* out, Uint... args) {
  (fprintf(out, ",%#lx", static_cast<unsigned long int>(args)), ...);
}

}  // namespace uart::internal

#endif  // ZIRCON_SYSTEM_ULIB_UART_PARSE_H_
