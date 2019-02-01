// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utils/String16.h>

#include <codecvt>
#include <cwchar>
#include <locale>
#include <string>

namespace {

class DestructibleConverter : public std::codecvt<char16_t, char, mbstate_t> {
 public:
  DestructibleConverter(std::size_t refs = 0)
      : std::codecvt<char16_t, char, mbstate_t>(refs) {}
  ~DestructibleConverter() override = default;
};

}  // anonymous namespace

namespace android {

String16::String16(const char* from_string) {
  std::wstring_convert<DestructibleConverter, char16_t> converter;
  std::u16string::operator=(converter.from_bytes(from_string));
}

}  // namespace android
