// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_TESTS_TRAITS_H_
#define SRC_LIB_ZBITL_TESTS_TRAITS_H_

#include <lib/zbitl/memory.h>
#include <lib/zbitl/storage-traits.h>

#include <cstddef>

#ifdef __Fuchsia__
#include <lib/zbitl/vmo.h>
#endif

// A convenience enum for picking a storage type.
enum class StorageType {
  kFblByteArray,
#ifdef __Fuchsia__
  kVmo,
#endif
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

// We abstract some of the repeated storage-specific fuzzing logic in a
// FuzzTraits instance, inheriting the official storage traits.
template <typename Storage>
struct FuzzTraits : public zbitl::StorageTraits<Storage> {
  static_assert(!std::is_same_v<Storage, Storage>("unrecognized storage type"));

  // This maximum is used to restrict variables that will extend the storage
  // type past the given size. The reason to restrict is in certain cases
  // we can easily exceed libFuzzer's memory cap, which is controlled by the
  // -rss_limit_mb flag; the flag limits the peak RSS of the whole process
  // with all other overheads, and defaults to 2000MiB.
  static constexpr uint32_t kRoughSizeMax = 0;

  static Storage FromString(const std::string& s) { return std::declval<Storage>(); }
};

template <>
struct FuzzTraits<fbl::Array<std::byte>> : public zbitl::StorageTraits<fbl::Array<std::byte>> {
  // To stay within libFuzzer's allowed allocation budget (see above), which
  // defaults to 2000MiB. Our choice of 1MiB stays well within this range.
  static constexpr uint32_t kRoughSizeMax = 0x100000;

  static fbl::Array<std::byte> FromString(const std::string& bytes) {
    fbl::Array<std::byte> array{new std::byte[bytes.size()], bytes.size()};
    memcpy(array.data(), bytes.data(), bytes.size());
    return array;
  }
};

#ifdef __Fuchsia__
template <>
struct FuzzTraits<zx::vmo> : public zbitl::StorageTraits<zx::vmo> {
  // A VMO of "size" UINT32_MAX will not actually necessarily occupy that
  // amount of space, but only that comprised by the number of pages written
  // to, which should not be particularly large here.
  static constexpr uint32_t kRoughSizeMax = std::numeric_limits<uint32_t>::max();

  static zx::vmo FromString(const std::string& bytes) {
    zx::vmo vmo;
    ZX_ASSERT(ZX_OK == zx::vmo::create(bytes.size(), 0, &vmo));
    ZX_ASSERT(ZX_OK == vmo.write(bytes.data(), 0, bytes.size()));
    return vmo;
  }

  // In order for StorageTraits<zx::vmo>::Create() to create a resiable VMO,
  // its input VMO must be resizable: so just create one directly.
  static fit::result<zx_status_t, zx::vmo> Create(zx::vmo& vmo, uint32_t capacity,
                                                  uint32_t initial_zero_size) {
    ZX_ASSERT(ZX_OK == zx::vmo::create(capacity, ZX_VMO_RESIZABLE, &vmo));
    return fit::ok(std::move(vmo));
  }
};
#endif  // __Fuchsia__

#endif  // SRC_LIB_ZBITL_TESTS_TRAITS_H_
