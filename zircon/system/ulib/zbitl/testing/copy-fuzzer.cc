// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/error_string.h>
#include <lib/zbitl/memory.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#ifdef __Fuchsia__
#include <lib/zbitl/vmo.h>
#include <lib/zx/vmo.h>
#endif

#include <cstddef>
#include <iterator>
#include <string>

#include <fbl/algorithm.h>
#include <fuzzer/FuzzedDataProvider.h>

namespace {

enum class StorageType {
  kFblByteArray,
#ifdef __Fuchsia__
  kVmo,
#endif
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

// Corresponds to a particular `Copy` `View` method. See usage below for the
// exact mapping.
enum class CopyCodepath {
  kDirectRawItem,
  kDirectRawItemWithHeader,
  kDirectStorageItem,
  kDirectIteratorRange,
  kDirectByteRange,
  kCreationRawItem,
  kCreationRawItemWithHeader,
  kCreationStorageItem,
  kCreationIteratorRange,
  kCreationByteRange,
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

// We abstract the main fuzzing logic below to allow for different storage
// types, templating on a fuzz trait struct.expected to have the following
// static members and methods:
// * `static constexpr uint32_t kToOffsetMax`, giving the maximum allowed
//   value of `to_offset` to be passed to the byte-range Copy signatures. The
//   reason to restrict it - with copy-creation - is that it will result in a
//   created storage object of size >= `to_offset`, which can easily exceed
//   libFuzzer's memory cap, in certain cases. The cap is controlled by
//   -rss_limit_mb flag, which limits the peak RSS of the whole process with
//    all other overheads, and defaults to 2048MB.
// * `static auto Convert(const std::string& zbi)`, converting a string
//   representation of a ZBI to that of the associated storage type.

struct FblByteArrayFuzzTraits {
  // To stay within libFuzzer's allowed allocation budget (see above), which
  // defaults to 2000MiB. Our choice of 1MiB stays well within this range.
  static constexpr uint32_t kToOffsetMax = 0x100000;

  static fbl::Array<std::byte> Convert(const std::string& zbi) {
    fbl::Array<std::byte> array{new std::byte[zbi.size()], zbi.size()};
    memcpy(array.data(), zbi.data(), zbi.size());
    return array;
  }
};

#ifdef __Fuchsia__
struct VmoFuzzTraits {
  // A VMO of "size" UINT32_MAX will not actually necessarily occupy that
  // amount of space, but only that comprised by the number of pages written
  // to, which should not be particularly large here.
  static constexpr uint32_t kToOffsetMax = std::numeric_limits<uint32_t>::max();

  static zx::vmo Convert(const std::string& zbi) {
    zx::vmo vmo;
    ZX_ASSERT(ZX_OK == zx::vmo::create(zbi.size(), 0, &vmo));
    ZX_ASSERT(ZX_OK == vmo.write(zbi.data(), 0, zbi.size()));
    return vmo;
  }
};
#endif  // __Fuchsia__

template <typename FuzzTraits>
int Fuzz(FuzzedDataProvider& provider) {
  // Compute some parameters up front (not necessarily used), so that we can
  // consume the rest of the data to create the ZBI.
  const auto codepath = provider.ConsumeEnum<CopyCodepath>();
  const auto from_offset = provider.ConsumeIntegral<uint32_t>();
  const auto from_length = provider.ConsumeIntegral<uint32_t>();
  const auto to_offset = provider.ConsumeIntegralInRange<uint32_t>(0, FuzzTraits::kToOffsetMax);

  std::string zbi = provider.ConsumeRemainingBytesAsString();
  auto storage = FuzzTraits::Convert(zbi);
  zbitl::View view(std::move(storage));

  // Storage destination (only used in the kDirect* codepaths).
  std::unique_ptr<std::byte[]> buff(new std::byte[zbi.size()]);
  fbl::Span<std::byte> to{buff.get(), zbi.size()};

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
      case CopyCodepath::kDirectStorageItem:
        static_cast<void>(view.CopyStorageItem(to, it));
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
      case CopyCodepath::kCreationStorageItem:
        static_cast<void>(view.CopyStorageItem(it));
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

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  switch (provider.ConsumeEnum<StorageType>()) {
    case StorageType::kFblByteArray:
      return Fuzz<FblByteArrayFuzzTraits>(provider);
#ifdef __Fuchsia__
    case StorageType::kVmo:
      return Fuzz<VmoFuzzTraits>(provider);
#endif
    case StorageType::kMaxValue:  // Placeholder.
      return 0;
  };
}
