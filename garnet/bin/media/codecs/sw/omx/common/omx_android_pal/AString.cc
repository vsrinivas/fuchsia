// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "./include/media/stagefright/foundation/AString.h"

#include <media/stagefright/foundation/ADebug.h>
#include <cstdarg>

namespace android {

AString::AString() = default;

AString::AString(const char* from_string) : std::string(from_string) {}

AString::AString(const char* from_string, size_t size)
    : std::string(from_string, size) {}

AString::AString(const AString& from) : std::string(from) {}

void AString::append(int int_to_append) {
  char string_buf[16];
  int result = snprintf(string_buf, sizeof(string_buf), "%d", int_to_append);
  (void)result;
  assert((result > 0) && ((size_t)result) < sizeof(string_buf));
  append(string_buf);
}

void AString::append(const char* string_to_append) {
  (*this) += string_to_append;
}

void AString::append(const char* string_to_append, size_t size) {
  (*this) += std::string(string_to_append, size);
}

void AString::append(const AString& string_to_append) {
  (*this) += string_to_append;
}

AString& AString::operator=(const AString& from) {
  std::string::operator=(from);
  return (*this);
}

AString AStringPrintf(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  char* buffer;
  vasprintf(&buffer, format, ap);
  va_end(ap);
  AString result(buffer);
  free(buffer);
  buffer = nullptr;
  return result;
}

}  // namespace android
