// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <pthread.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

#include <array>
#include <atomic>

#include <zxtest/zxtest.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

namespace {

#if __has_feature(address_sanitizer)

#define ASAN_SHADOW_SHIFT 3

// Touch every page in the region to make sure it's been COW'd.
__attribute__((no_sanitize("all"))) static void PrefaultPages(uintptr_t start, uintptr_t end) {
  while (start < end) {
    auto ptr = reinterpret_cast<volatile uintptr_t*>(start);
    *ptr = *ptr;
    start += PAGE_SIZE;
  }
}

TEST(SanitzerUtilsTest, FillShadow) {
  pthread_attr_t attr;
  ASSERT_EQ(pthread_getattr_np(pthread_self(), &attr), 0);

  void* stackaddr;
  size_t stacksize;
  ASSERT_EQ(pthread_attr_getstack(&attr, &stackaddr, &stacksize), 0);

  uintptr_t stackstart = reinterpret_cast<uintptr_t>(stackaddr);
  uintptr_t stackend = reinterpret_cast<uintptr_t>(stackaddr) + stacksize;

  // Prefault all stack pages to make sure this doesn't happen later while collecting samples.
  PrefaultPages(stackstart, stackend);
  // We also need to prefault all stack shadow pages.
  size_t shadow_scale;
  size_t shadow_offset;
  __asan_get_shadow_mapping(&shadow_scale, &shadow_offset);
  PrefaultPages((stackstart >> shadow_scale) + shadow_offset,
                (stackend >> shadow_scale) + shadow_offset);

  zx_info_task_stats_t task_stats;

  // Snapshot the memory use at the beginning.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t init_mem_use = task_stats.mem_private_bytes;

  constexpr size_t len = 32 * PAGE_SIZE;

  // Allocate some memory...
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  uintptr_t addr;
  ASSERT_OK(zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));

  size_t alloc_mem_use = task_stats.mem_private_bytes;

  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  EXPECT_GE(alloc_mem_use, init_mem_use, "");

  // ..and poison it.
  ASAN_POISON_MEMORY_REGION((void*)addr, len);

  // Snapshot the memory use after the allocation.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t memset_mem_use = task_stats.mem_private_bytes;

  // We expect the memory use to go up.
  EXPECT_GT(memset_mem_use, alloc_mem_use, "");

  // Unpoison the shadow.
  __sanitizer_fill_shadow(addr, len, 0, 0);

  // Snapshot the memory use after unpoisoning.
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));

  size_t fill_shadow_mem_use = task_stats.mem_private_bytes;

  // We expect the memory use to decrease.
  EXPECT_LT(fill_shadow_mem_use, memset_mem_use, "");

  // Deallocate the memory.
  ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
}

TEST(SanitzerUtilsTest, FillShadowSmall) {
  pthread_attr_t attr;
  ASSERT_EQ(pthread_getattr_np(pthread_self(), &attr), 0);

  void* stackaddr;
  size_t stacksize;
  ASSERT_EQ(pthread_attr_getstack(&attr, &stackaddr, &stacksize), 0);

  uintptr_t stackstart = reinterpret_cast<uintptr_t>(stackaddr);
  uintptr_t stackend = reinterpret_cast<uintptr_t>(stackaddr) + stacksize;

  // Prefault all stack pages to make sure this doesn't happen later while collecting samples.
  PrefaultPages(stackstart, stackend);
  // We also need to prefault all stack shadow pages.
  size_t shadow_scale;
  size_t shadow_offset;
  __asan_get_shadow_mapping(&shadow_scale, &shadow_offset);
  PrefaultPages((stackstart >> shadow_scale) + shadow_offset,
                (stackend >> shadow_scale) + shadow_offset);

  // This tests that unpoisoning less than 1 shadow page of memory works.
  // This size ends up being three shadow pages, that way we can guarantee to
  // always have an address that is aligned to a shadow page.
  constexpr size_t len = (PAGE_SIZE << ASAN_SHADOW_SHIFT) * 3;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  uintptr_t addr;

  std::array<size_t, 4> sizes = {PAGE_SIZE << ASAN_SHADOW_SHIFT,
                                 (PAGE_SIZE / 2) << ASAN_SHADOW_SHIFT,
                                 (PAGE_SIZE + 1) << ASAN_SHADOW_SHIFT, PAGE_SIZE};

  std::array<ssize_t, 3> offsets = {-(1 << ASAN_SHADOW_SHIFT), 0, (1 << ASAN_SHADOW_SHIFT)};

  for (const auto size : sizes) {
    for (const auto offset : offsets) {
      ASSERT_OK(
          zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));
      // Align base to the next shadow page, leaving one shadow page to its left.
      uintptr_t base =
          (addr + (PAGE_SIZE << ASAN_SHADOW_SHIFT)) & -(PAGE_SIZE << ASAN_SHADOW_SHIFT);

      zx_info_task_stats_t task_stats;
      // Snapshot the memory before allocating in the shadow
      ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                              sizeof(zx_info_task_stats_t), nullptr, nullptr));
      size_t init_mem_use = task_stats.mem_private_bytes;

      // Poison the shadow.
      ASAN_POISON_MEMORY_REGION((void*)(base + offset), size);

      // Unpoison it.
      __sanitizer_fill_shadow(base + offset, size, 0 /* val */, 0 /* threshold */);

      // Measure memory again.
      ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                              sizeof(zx_info_task_stats_t), nullptr, nullptr));
      size_t final_mem_use = task_stats.mem_private_bytes;

      // At most we are leaving 2 ASAN shadow pages committed.
      EXPECT_LE(init_mem_use, final_mem_use, "");
      EXPECT_LE(final_mem_use - init_mem_use, PAGE_SIZE * 2, "");

      // Deallocate the memory.
      ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
    }
  }
}
#endif
}  // namespace
