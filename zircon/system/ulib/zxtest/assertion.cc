// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include <cinttypes>
#include <string_view>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <zxtest/base/assertion.h>

namespace zxtest {

Assertion::Assertion(const fbl::String& desc, const fbl::String& expected,
                     const fbl::String& expected_eval, const fbl::String& actual,
                     const fbl::String& actual_eval, const SourceLocation& location, bool is_fatal)
    : description_(desc),
      expected_(expected),
      expected_eval_(expected_eval),
      actual_(actual),
      actual_eval_(actual_eval),
      location_(location),
      is_fatal_(is_fatal),
      has_values_(true) {}

Assertion::Assertion(const fbl::String& desc, const SourceLocation& location, bool is_fatal)
    : description_(desc), location_(location), is_fatal_(is_fatal), has_values_(false) {}

Assertion::Assertion(Assertion&& other) = default;
Assertion::~Assertion() = default;

namespace internal {
fbl::String ToHex(const void* ptr, size_t size) {
  if (size == 0) {
    return "<empty>";
  }

  if (ptr == nullptr) {
    return "<nullptr>";
  }

  // 2 char for 2 4 bit pairs in each byte.
  // 1 char for each space after a byte, except for the last byte.
  const size_t expected_size = 3 * size - 1;
  char buffer[expected_size];
  static constexpr char kHexTable[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  const uint8_t* cur = static_cast<const uint8_t*>(ptr);
  const uint8_t* end = static_cast<const uint8_t*>(ptr) + size;
  size_t index = 0;
  while (cur != end) {
    buffer[index++] = kHexTable[*cur >> 4];
    buffer[index++] = kHexTable[*cur & 0xF];
    if (end - cur > 1) {
      buffer[index++] = ' ';
    }
    ++cur;
  }

  return fbl::String(buffer, index);
}

fbl::String PrintVolatile(volatile const void* ptr, size_t size) {
  if (size == 0) {
    return "<empty>";
  }

  if (ptr == nullptr) {
    return "<nullptr>";
  }

  return "<ptr>";
}

}  // namespace internal

template <>
fbl::String PrintValue(const uint32_t& value) {
  return fbl::StringPrintf("%" PRIu32, value);
}

template <>
fbl::String PrintValue(const int32_t& value) {
  return fbl::StringPrintf("%" PRIi32, value);
}

template <>
fbl::String PrintValue(const int64_t& value) {
  return fbl::StringPrintf("%" PRIi64, value);
}

template <>
fbl::String PrintValue(const uint64_t& value) {
  return fbl::StringPrintf("%" PRIu64, value);
}

template <>
fbl::String PrintValue(const float& value) {
  return fbl::StringPrintf("%f", value);
}

template <>
fbl::String PrintValue(const double& value) {
  return fbl::StringPrintf("%f", value);
}

template <>
fbl::String PrintValue(const char* value) {
  if (value == nullptr) {
    return "<nullptr>";
  }
  return fbl::StringPrintf("%s", value);
}

template <>
fbl::String PrintValue(const std::string& value) {
  return fbl::String(value.data(), value.size());
}

template <>
fbl::String PrintValue(const fbl::String& value) {
  return value;
}

fbl::String PrintStatus(zx_status_t status) {
#ifdef __Fuchsia__
  return fbl::StringPrintf("%s(%d)", zx_status_get_string(status), status);
#else
  return fbl::StringPrintf("%d", status);
#endif
}

bool StrCmp(const char* actual, const char* expected) {
  // We take precaution not to call strcmp on a nullptr, as it varies by
  // implementation whether that is supported.
  if (actual == nullptr && expected == nullptr) {
    return true;
  }
  return (!actual == !expected) && strcmp(actual, expected) == 0;
}

bool StrCmp(const fbl::String& actual, const char* expected) {
  return StrCmp(actual.c_str(), expected);
}

bool StrCmp(const char* actual, const fbl::String& expected) {
  return StrCmp(actual, expected.c_str());
}

bool StrCmp(const fbl::String& actual, const fbl::String& expected) { return actual == expected; }

bool StrContain(const fbl::String& str, const fbl::String& substr) {
  const auto str_view = std::string_view(str.data(), str.size());
  const auto substr_view = std::string_view(substr.data(), substr.size());

  return str_view.find(substr_view) != std::string_view::npos;
}

bool StrContain(const fbl::String& str, const char* substr) {
  return StrContain(str, fbl::String(substr, strlen(substr)));
}

bool StrContain(const char* str, const fbl::String& substr) {
  return StrContain(fbl::String(str, strlen(str)), substr);
}

bool StrContain(const char* str, const char* substr) {
  return StrContain(fbl::String(str, strlen(str)), fbl::String(substr, strlen(substr)));
}

}  // namespace zxtest
