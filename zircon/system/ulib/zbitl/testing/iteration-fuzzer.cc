// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/memory.h>
#include <lib/zbitl/view.h>

constexpr size_t kMaxSize = 1024;

template <zbitl::Checking Check>
using MemoryView = zbitl::View<fbl::Span<const uint8_t>, Check>;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > kMaxSize) {
    return 0;
  }
  fbl::Span<const uint8_t> span{data, size};
  {
    MemoryView<zbitl::Checking::kPermissive> view(span);
    for (auto [header, payload] : view) {
      static_cast<void>(header);
      static_cast<void>(payload);
    }
    view.ignore_error();
  }
  {
    MemoryView<zbitl::Checking::kStrict> view(span);
    for (auto [header, payload] : view) {
      static_cast<void>(header);
      static_cast<void>(payload);
    }
    view.ignore_error();
  }
  {
    MemoryView<zbitl::Checking::kCrc> view(span);
    for (auto [header, payload] : view) {
      static_cast<void>(header);
      static_cast<void>(payload);
    }
    view.ignore_error();
  }
  return 0;
}
