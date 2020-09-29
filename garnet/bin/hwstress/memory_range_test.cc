// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_range.h"

#include <lib/zx/time.h>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(MemoryRange, CreateDestroy) {
  auto range = MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
  ASSERT_NE(range, nullptr);
  EXPECT_EQ(range->size_bytes(), ZX_PAGE_SIZE);
  EXPECT_EQ(range->size_words(), ZX_PAGE_SIZE / sizeof(uint64_t));
}

TEST(MemoryRange, MemoryWrite) {
  // Create the range.
  auto range = MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached);

  // Make sure we can write to it.
  for (size_t i = 0; i < range->size_bytes(); i++) {
    range->bytes()[i] = 0xaa;
  }
  for (size_t i = 0; i < range->size_words(); i++) {
    range->words()[i] = 0xaabbccdd;
  }
}

uint32_t GetVmoCachePolicy(const zx::vmo& vmo) {
  zx_info_vmo_t info;
  EXPECT_EQ(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  return info.cache_policy;
}

TEST(MemoryRange, CachedVsUncached) {
  // Check that the VMOs have the correct cache settings.
  {
    std::unique_ptr<MemoryRange> range =
        MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
    EXPECT_EQ(GetVmoCachePolicy(range->vmo()), ZX_CACHE_POLICY_CACHED);
  }
  {
    std::unique_ptr<MemoryRange> range =
        MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kUncached).value();
    EXPECT_EQ(GetVmoCachePolicy(range->vmo()), ZX_CACHE_POLICY_UNCACHED);
  }
}

TEST(MemoryRange, CacheOps) {
  // It is hard to reliably test that cache ops do what is written on the box,
  // so we just call them and assume the kernel is doing the operation.
  std::unique_ptr<MemoryRange> range =
      MemoryRange::Create(ZX_PAGE_SIZE, CacheMode::kCached).value();
  range->CleanCache();
  range->CleanInvalidateCache();
}

}  // namespace
}  // namespace hwstress
