// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <stdint.h>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <zxtest/base/assertion.h>

namespace zxtest {

Assertion::Assertion(const fbl::String& desc, const fbl::String& expected,
                     const fbl::String& expected_eval, const fbl::String& actual,
                     const fbl::String& actual_eval, const SourceLocation& location, bool is_fatal)
    : description_(desc), expected_(expected), expected_eval_(expected_eval), actual_(actual),
      actual_eval_(actual_eval), location_(location), is_fatal_(is_fatal), has_values_(true) {}

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
    // 1 char for each space.
    // 1 char for '\0'.
    size_t expected_size = 3 * size + 1;
    char buffer[expected_size];
    memset(buffer, '\0', static_cast<int32_t>(expected_size));

    for (size_t curr = 0; curr < size; ++curr) {
        snprintf(buffer + 3 * curr, expected_size - curr, "%02X%*s", *((uint8_t*)(ptr) + curr),
                 (curr < size - 1) ? 1 : 0, " ");
    }
    return buffer;
}

} // namespace internal

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
fbl::String PrintValue(const char* value) {
    if (value == nullptr) {
        return "<nullptr>";
    }
    return fbl::StringPrintf("%s", value);
}

template <>
fbl::String PrintValue(const fbl::String& value) {
    return value;
}

bool StrCmp(const fbl::String& actual, const fbl::String& expected) {
    return actual == expected;
}

bool StrCmp(const fbl::String& actual, const char* expected) {
    return strcmp(actual.c_str(), expected) == 0;
}

bool StrCmp(const char* actual, const fbl::String& expected) {
    return strcmp(actual, expected.c_str()) == 0;
}

bool StrCmp(const char* actual, const char* expected) {
    return strcmp(actual, expected) == 0;
}

} // namespace zxtest
