// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_KAZOO_STRING_UTIL_H_
#define ZIRCON_TOOLS_KAZOO_STRING_UTIL_H_

#include <zircon/compiler.h>

#include <string>
#include <vector>

bool ReadFileToString(const std::string& path, std::string* result);

// Formats |printf()|-like input and returns it as an |std::string|.
std::string StringPrintf(const char* format, ...) __PRINTFLIKE(1, 2) __WARN_UNUSED_RESULT;

// Formats |vprintf()|-like input and returns it as an |std::string|.
std::string StringVPrintf(const char* format, va_list ap) __WARN_UNUSED_RESULT;

template <typename StringContainer>
std::string JoinStrings(const StringContainer& strings, const std::string& separator = "") {
  size_t output_length = 0;
  // Add up the sizes of the strings.
  for (const auto& i : strings) {
    output_length += i.size();
  }
  // Add the sizes of the separators.
  if (!strings.empty()) {
    output_length += (strings.size() - 1) * separator.size();
  }

  std::string joined;
  joined.reserve(output_length);

  bool first = true;
  for (const auto& i : strings) {
    if (!first) {
      joined.append(separator);
    } else {
      first = false;
    }
    joined.append(i.begin(), i.end());
  }

  return joined;
}

std::string TrimString(const std::string& str, const std::string& chars_to_trim);

enum WhitespaceHandling {
  kKeepWhitespace,
  kTrimWhitespace,
};

std::vector<std::string> SplitString(const std::string& input, char delimiter,
                                     WhitespaceHandling whitespace);

inline char ToLowerASCII(char c) { return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c; }

inline char ToUpperASCII(char c) { return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c; }

int64_t StringToInt(const std::string& str);

uint64_t StringToUInt(const std::string& str);

bool StartsWith(const std::string& str, const std::string& prefix);

#endif  // ZIRCON_TOOLS_KAZOO_STRING_UTIL_H_
