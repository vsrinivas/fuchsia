// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/error_string.h>
#include <lib/zbitl/memory.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstddef>
#include <iterator>
#include <string>

#include <fbl/algorithm.h>
#include <fuzzer/FuzzedDataProvider.h>

// The maximal allowed `to_offset` parameter to a byte-range copy method,
// which will result of an allocation - inherently restricted by the fuzzing
// framework - of that size.
constexpr uint32_t kToOffsetMax = 0x100000;  // 1MiB.

// Corresponds to a particular `Copy` `View` method. See usage below for the
// exact mapping.
enum class CopyCodepath {
  kDirectRawItem,
  kDirectRawItemWithHeader,
  kDirectIteratorRange,
  kDirectByteRange,
  kCreationRawItem,
  kCreationRawItemWithHeader,
  kCreationIteratorRange,
  kCreationByteRange,
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  // Compute some parameters up front (not necessarily used), so that we can
  // consume the rest of the data to create the ZBI.
  const auto codepath = provider.ConsumeEnum<CopyCodepath>();
  const auto from_offset = provider.ConsumeIntegral<uint32_t>();
  const auto from_length = provider.ConsumeIntegral<uint32_t>();
  const auto to_offset = provider.ConsumeIntegralInRange<uint32_t>(
      0, kToOffsetMax - std::min(kToOffsetMax, from_length));

  // Use a fbl::Array-backed view to exercise the copy-creation codepaths.
  std::string zbi = provider.ConsumeRemainingBytesAsString();
  fbl::Array<std::byte> array{new std::byte[zbi.size()], zbi.size()};
  memcpy(array.data(), zbi.data(), zbi.size());
  zbitl::View view(std::move(array));

  // Storage destination (only used in the kDirect* codepaths).
  std::unique_ptr<std::byte[]> buff(new std::byte[size]);
  fbl::Span<std::byte> to{buff.get(), size};

  // These two codepaths are per-view (and not per-iterator) and should not
  // affect the view's internal error state.
  if (codepath == CopyCodepath::kDirectByteRange) {
    static_cast<void>(view.Copy(to, from_offset, from_length, to_offset));
    return 0;
  } else if (codepath == CopyCodepath::kCreationByteRange) {
    static_cast<void>(view.Copy(from_offset, from_length, to_offset));
    return 0;
  }

  for (auto it = view.begin(); it != view.end(); ++it) {
    switch (codepath) {
      case CopyCodepath::kDirectRawItem:
        static_cast<void>(view.CopyRawItem(to, it));
        break;
      case CopyCodepath::kDirectRawItemWithHeader:
        static_cast<void>(view.CopyRawItemWithHeader(to, it));
        break;
      case CopyCodepath::kDirectIteratorRange:
        static_cast<void>(view.Copy(to, view.begin(), it));
        static_cast<void>(view.Copy(to, it, view.end()));
        break;
      case CopyCodepath::kCreationRawItem:
        static_cast<void>(view.CopyRawItem(it));
        break;
      case CopyCodepath::kCreationRawItemWithHeader:
        static_cast<void>(view.CopyRawItemWithHeader(it));
        break;
      case CopyCodepath::kCreationIteratorRange:
        static_cast<void>(view.Copy(view.begin(), it));
        static_cast<void>(view.Copy(it, view.end()));
        break;
      case CopyCodepath::kMaxValue:  // Just a placeholder.
        break;
      case CopyCodepath::kDirectByteRange:
      case CopyCodepath::kCreationByteRange:
        ZX_ASSERT_MSG(false, "byte range codepaths should have been handled separately");
    };
  }

  view.ignore_error();
  return 0;
}
