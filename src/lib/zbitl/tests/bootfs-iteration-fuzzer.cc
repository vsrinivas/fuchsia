// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/error-string.h>
#include <lib/zbitl/items/bootfs.h>
#include <zircon/assert.h>
#include <zircon/boot/bootfs.h>

#include <string>
#include <string_view>

#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  const std::string dirname = provider.ConsumeRandomLengthString();
  const std::string filename = provider.ConsumeRandomLengthString();
  const std::string raw = provider.ConsumeRemainingBytesAsString();

  zbitl::Bootfs<std::string_view> bootfs_reader;
  if (auto result = zbitl::Bootfs<std::string_view>::Create(raw); result.is_error()) {
    return 0;
  } else {
    bootfs_reader = std::move(result).value();
  }

  auto bootfs = bootfs_reader.root();
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

  constexpr auto valid_path_part = [](std::string_view part) -> bool {
    return !part.empty() && part.front() != '/' && part.back() != '/';
  };

  if (valid_path_part(dirname) && valid_path_part(filename)) {
    if (auto it = bootfs.find({dirname, filename}); it != bootfs.end()) {
      ZX_ASSERT(it->name == dirname + '/' + filename);
    }

    bootfs.ignore_error();
  }

  return 0;
}
