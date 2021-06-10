// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include <cinttypes>

#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <zxtest/base/message.h>

namespace zxtest {

Message::Message(const fbl::String& text, const SourceLocation& location)
    : text_(text), location_(location) {}

Message::Message(Message&& other) noexcept = default;
Message::~Message() = default;

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

}  // namespace zxtest
