// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

#include <string>

#include <fuzzer/FuzzedDataProvider.h>

// If FuzzedDataProvider::ConsumeEnum() did not require a terminal kMaxValue
// enum value, we would just use zbitl::Checking.
enum class Checking { kPermissive, kStrict, kCrc, kMaxValue };

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  auto checking = provider.ConsumeEnum<Checking>();
  std::string zbi = provider.ConsumeRemainingBytesAsString();

  auto iterate = [](auto view) {
    for (auto [header, payload] : view) {
      static_cast<void>(header);
      static_cast<void>(payload);
    }
    view.ignore_error();
  };

  switch (checking) {
    case Checking::kPermissive: {
      zbitl::View<std::string_view, zbitl::Checking::kPermissive> view(zbi);
      iterate(view);
      break;
    }
    case Checking::kStrict: {
      zbitl::View<std::string_view, zbitl::Checking::kStrict> view(zbi);
      iterate(view);
      break;
    }
    case Checking::kCrc: {
      zbitl::View<std::string_view, zbitl::Checking::kCrc> view(zbi);
      iterate(view);
      break;
    }
    case Checking::kMaxValue:  // Just a placeholder.
      break;
  };
  return 0;
}
