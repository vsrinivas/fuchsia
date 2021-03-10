// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

#include <string>

#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  const bool check_crc = provider.ConsumeBool();
  const std::string zbi = provider.ConsumeRemainingBytesAsString();
  zbitl::View<std::string_view> view(zbi);

  for (auto it = view.begin(); it != view.end(); ++it) {
    if (check_crc) {
      static_cast<void>(view.CheckCrc32(it));
    }
  }
  view.ignore_error();
  return 0;
}
