// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/efi/testing/stub_boot_services.h>

#include <string>
#include <string_view>

#include <efi/protocol/loaded-image.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "xefi.h"

// Needed to use the ""s operator to embed nulls in string literals.
using namespace std::string_literals;

namespace {

const efi_handle kImageHandle = reinterpret_cast<efi_handle>(0x10);

// Injects mock load options, calls xefi_get_load_options(), and returns the
// resulting UTF-16 string (including the null terminator).
std::u16string GetLoadOptions(const void* contents, uint32_t size) {
  efi::MockBootServices boot_services;
  gImg = kImageHandle;
  gBS = boot_services.services();

  efi_loaded_image_protocol loaded_image_protocol = {.LoadOptionsSize = size,
                                                     .LoadOptions = const_cast<void*>(contents)};
  boot_services.ExpectProtocol(gImg, LoadedImageProtocol, &loaded_image_protocol);

  void* load_options = nullptr;
  size_t load_options_size = 0;
  EXPECT_EQ(EFI_SUCCESS, xefi_get_load_options(&load_options_size, &load_options));

  // Extract the result before we free the pool.
  EXPECT_EQ(0u, load_options_size % sizeof(char16_t));
  std::u16string result(reinterpret_cast<char16_t*>(load_options),
                        load_options_size / sizeof(char16_t));

  boot_services.FreePool(load_options);

  return result;
}

// Overload to make it easier to call using just a string_view param.
std::u16string GetLoadOptions(std::u16string_view contents) {
  return GetLoadOptions(contents.data(),
                        static_cast<uint32_t>(contents.length() * sizeof(contents[0])));
}

TEST(Cmdline, XefiGetLoadOptions) { EXPECT_EQ(GetLoadOptions(u"foo bar 123"), u"foo bar 123"); }

TEST(Cmdline, XefiGetLoadOptionsNull) { EXPECT_EQ(GetLoadOptions(nullptr, 0), u""); }

TEST(Cmdline, XefiGetLoadOptionsEmpty) { EXPECT_EQ(GetLoadOptions(u""), u""); }

TEST(Cmdline, XefiGetLoadOptionsEmbeddedNull) {
  EXPECT_EQ(GetLoadOptions(u"foo\0bar"s), u"foo\0bar"s);
}

}  // namespace
