// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
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

#include "exit-hook-test-helper.h"

namespace {

#if __has_feature(address_sanitizer)

// TODO(fxbug.dev/52653): These tests are flaky as they rely on the OS not decommitting
//              memory automatically.
#if 0

// c++ complains if we try to do PAGE_SIZE << shadow_scale.
constexpr size_t kPageSize = PAGE_SIZE;

// Touch every page in the region to make sure it's been COW'd.
__attribute__((no_sanitize("all"))) static void PrefaultPages(uintptr_t start, uintptr_t end) {
  while (start < end) {
    auto ptr = reinterpret_cast<volatile uintptr_t*>(start);
    *ptr = *ptr;
    start += kPageSize;
  }
}

// Calls PrefaultPages on every page in the current thread's stack.
static void PrefaultStackPages() {
  pthread_attr_t attr;
  ASSERT_EQ(pthread_getattr_np(pthread_self(), &attr), 0);

  void* stackaddr;
  size_t stacksize;
  ASSERT_EQ(pthread_attr_getstack(&attr, &stackaddr, &stacksize), 0);

  uintptr_t stackstart = reinterpret_cast<uintptr_t>(stackaddr);
  uintptr_t stackend = reinterpret_cast<uintptr_t>(stackaddr) + stacksize;

  // Prefault all stack pages to make sure this doesn't happen later while
  // collecting samples.
  PrefaultPages(stackstart, stackend);
  // We also need to prefault all stack shadow pages.
  size_t shadow_scale;
  size_t shadow_offset;
  __asan_get_shadow_mapping(&shadow_scale, &shadow_offset);
  PrefaultPages((stackstart >> shadow_scale) + shadow_offset,
                (stackend >> shadow_scale) + shadow_offset);
}

static void GetMemoryUsage(size_t* usage) {
  zx_info_task_stats_t task_stats;
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_TASK_STATS, &task_stats,
                                          sizeof(zx_info_task_stats_t), nullptr, nullptr));
  *usage = task_stats.mem_private_bytes;
}

TEST(SanitizerUtilsTest, FillShadow) {
  PrefaultStackPages();

  constexpr size_t len = 32 * kPageSize;
  size_t init_mem_use;
  GetMemoryUsage(&init_mem_use);

  // Allocate some memory...
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  uintptr_t addr;
  ASSERT_OK(zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));

  size_t alloc_mem_use;
  GetMemoryUsage(&alloc_mem_use);
  EXPECT_GE(alloc_mem_use, init_mem_use, "");

  // ..and poison it.
  ASAN_POISON_MEMORY_REGION((void*)addr, len);

  // Snapshot the memory use after the allocation.
  size_t memset_mem_use;
  GetMemoryUsage(&memset_mem_use);

  // We expect the memory use to go up.
  EXPECT_GT(memset_mem_use, alloc_mem_use, "");

  // Unpoison the shadow.
  __sanitizer_fill_shadow(addr, len, 0, 0);

  // Snapshot the memory use after unpoisoning.
  size_t fill_shadow_mem_use;
  GetMemoryUsage(&fill_shadow_mem_use);

  // We expect the memory use to decrease.
  EXPECT_LT(fill_shadow_mem_use, memset_mem_use, "");

  // Deallocate the memory.
  ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
}

TEST(SanitizerUtilsTest, FillShadowSmall) {
  PrefaultStackPages();

  size_t shadow_scale;
  __asan_get_shadow_mapping(&shadow_scale, nullptr);

  // This tests that unpoisoning less than 1 shadow page of memory works.
  // This size ends up being three shadow pages, that way we can guarantee to
  // always have an address that is aligned to a shadow page.
  const size_t len = (kPageSize << shadow_scale) * 3;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));
  uintptr_t addr;

  std::array<size_t, 4> sizes = {kPageSize << shadow_scale, (kPageSize / 2) << shadow_scale,
                                 (kPageSize + 1) << shadow_scale, kPageSize};

  std::array<ssize_t, 3> offsets = {-(1 << shadow_scale), 0, (1 << shadow_scale)};

  for (const auto size : sizes) {
    for (const auto offset : offsets) {
      ASSERT_OK(
          zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));
      // Align base to the next shadow page, leaving one shadow page to its left.
      uintptr_t base = (addr + (kPageSize << shadow_scale)) & -(kPageSize << shadow_scale);

      size_t init_mem_use;
      GetMemoryUsage(&init_mem_use);

      // Poison the shadow.
      ASAN_POISON_MEMORY_REGION((void*)(base + offset), size);

      // Unpoison it.
      __sanitizer_fill_shadow(base + offset, size, 0 /* val */, 0 /* threshold */);

      size_t final_mem_use;
      GetMemoryUsage(&final_mem_use);

      // At most we are leaving 2 ASAN shadow pages committed.
      EXPECT_LE(init_mem_use, final_mem_use, "");
      EXPECT_LE(final_mem_use - init_mem_use, kPageSize * 2, "");

      // Deallocate the memory.
      ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
    }
  }
}

TEST(SanitizerUtilsTest, FillShadowPartialPages) {
  PrefaultStackPages();

  size_t shadow_scale;
  __asan_get_shadow_mapping(&shadow_scale, nullptr);
  const size_t len = (kPageSize << shadow_scale) * 5;

  // Snapshot the memory use at the beginning.
  size_t init_mem_use;
  GetMemoryUsage(&init_mem_use);

  for (int i = 0; i < 100; i++) {
    // Allocate memory...
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(0, 0, &vmo));
    uintptr_t addr;
    ASSERT_OK(
        zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr));
    // ..and poison it.
    ASAN_POISON_MEMORY_REGION((void*)addr, len);
    // Unpoison the shadow.
    __sanitizer_fill_shadow(addr, len, 0, 0);
    // Deallocate the memory.
    ASSERT_OK(zx::vmar::root_self()->unmap(addr, len));
  }

  // Snapshot the memory use after unpoisoning.
  size_t final_mem_use;
  GetMemoryUsage(&final_mem_use);

  // We expect the memory use to stay the same.
  EXPECT_EQ(final_mem_use, init_mem_use, "");
}
#endif  // 0

#endif

TEST(SanitizerUtilsTest, ProcessExitHook) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (!root_dir) {
    root_dir = "";
  }
  std::string file(root_dir);
  file += "/bin/sanitizer-exit-hook-test-helper";

  zx::process child;
  const char* argv[] = {file.c_str(), nullptr};
  ASSERT_OK(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                       child.reset_and_get_address()));

  zx_signals_t signals;
  ASSERT_OK(child.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);

  zx_info_process_t info;
  ASSERT_OK(child.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));

  EXPECT_EQ(info.return_code, kHookStatus);
}

}  // namespace
