// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/error_string.h>
#include <lib/zbitl/items/bootfs.h>
#include <zircon/assert.h>
#include <zircon/boot/bootfs.h>

#include <string>

#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  const std::string dirname = provider.ConsumeRandomLengthString();
  const std::string filename = provider.ConsumeRandomLengthString();
  const std::string raw = provider.ConsumeRemainingBytesAsString();

  zbitl::BootfsView<std::string_view> bootfs;
  if (auto result = zbitl::BootfsView<std::string_view>::Create(raw); result.is_error()) {
    return 0;
  } else {
    bootfs = std::move(result).value();
  }

  for (const auto& file : bootfs) {
    ZX_ASSERT(file.name.size() <= ZBI_BOOTFS_MAX_NAME_LEN);
    if (!file.name.empty()) {
      ZX_ASSERT(file.name.front() != '/');
    }
    ZX_ASSERT(file.offset % ZBI_BOOTFS_PAGE_SIZE == 0);
  }

  if (auto result = bootfs.take_error(); result.is_error()) {
    printf("error: %s", zbitl::BootfsErrorString(result.error_value()).c_str());
  }

  if ((dirname.empty() || (dirname.front() != '/' && dirname.back() != '/')) &&
      (filename.empty() || filename.front() != '/')) {
    if (auto it = bootfs.find(filename, dirname); it != bootfs.end()) {
      std::string expected(dirname);
      if (!expected.empty()) {
        expected += '/';
      }
      expected += filename;
      ZX_ASSERT(it->name == expected);
    }

    bootfs.ignore_error();
  }

  return 0;
}
