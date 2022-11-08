// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <pthread.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <array>
#include <atomic>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif

#include "exit-hook-test-helper.h"

namespace {

#if __has_feature(address_sanitizer)

constexpr size_t kMaxVmos = 8192;
zx_info_vmo vmos[kMaxVmos];

constexpr size_t kMaxMaps = 8192;
zx_info_maps maps[kMaxMaps];

// Returns the koid of the ASAN Shadow vmo.
zx_status_t GetAsanShadowVmoKoid(zx_koid_t* vmo_koid) {
  if (vmo_koid == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  uintptr_t shadow_offset = __sanitizer_shadow_bounds().shadow_base;

  size_t actual, available;
  zx_status_t res =
      zx::process::self()->get_info(ZX_INFO_PROCESS_MAPS, maps, sizeof(maps), &actual, &available);
  if (res != ZX_OK)
    return res;
  if (available > kMaxMaps)
    return ZX_ERR_NO_RESOURCES;

  for (size_t i = 0; i < actual; i++) {
    if (maps[i].type != ZX_INFO_MAPS_TYPE_MAPPING)
      continue;
    if (shadow_offset >= maps[i].base && shadow_offset < maps[i].base + maps[i].size) {
      *vmo_koid = maps[i].u.mapping.vmo_koid;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t GetStats(zx_koid_t vmo_koid, uint64_t* committed_bytes,
                     uint64_t* committed_change_events) {
  size_t actual, available;
  zx_status_t res =
      zx::process::self()->get_info(ZX_INFO_PROCESS_VMOS, vmos, sizeof(vmos), &actual, &available);
  if (res != ZX_OK)
    return res;
  if (available > kMaxVmos)
    return ZX_ERR_NO_RESOURCES;

  for (size_t i = 0; i < actual; i++) {
    if (vmos[i].koid == vmo_koid) {
      if (committed_bytes)
        *committed_bytes = vmos[i].committed_bytes;
      if (committed_change_events)
        *committed_change_events = vmos[i].committed_change_events;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t GetMemoryUsage(zx_koid_t vmo_koid, uint64_t* committed_bytes) {
  return GetStats(vmo_koid, committed_bytes, /* Committed Change Events */ nullptr);
}

zx_status_t GetCommitChangeEvents(zx_koid_t vmo_koid, uint64_t* committed_change_events) {
  return GetStats(vmo_koid, /* Memory Usage */ nullptr, committed_change_events);
}

// c++ complains if we try to do PAGE_SIZE << shadow_scale.
const size_t kPageSize = zx_system_get_page_size();

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

TEST(SanitizerUtilsTest, FillShadow) {
  zx_koid_t shadow_koid;
  ASSERT_OK(GetAsanShadowVmoKoid(&shadow_koid));

  uint64_t start_events, end_events;
  uint64_t init_mem_use;
  uint64_t alloc_mem_use;
  uint64_t memset_mem_use;
  uint64_t fill_shadow_mem_use;

  const size_t len = 32 * kPageSize;

  // We are testing that the shadow decommit operation works.
  // A previous test could have left the shadow in an uncommitted state.
  // By creating an aligned vmar, and decommitting its shadow before the test
  // starts, we guarantee that all the vmos we map inside it will have a
  // decommitted shadow as well.
  zx::vmar vmar;
  uintptr_t vmar_addr;
  ASSERT_OK(zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_64KB, 0, len, &vmar, &vmar_addr));
  auto cleanup = fit::defer([&vmar]() { vmar.destroy(); });

  __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);

  do {
    ASSERT_OK(GetCommitChangeEvents(shadow_koid, &start_events));

    PrefaultStackPages();

    ASSERT_OK(GetMemoryUsage(shadow_koid, &init_mem_use));

    // Allocate some memory...
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(0, 0, &vmo));
    uintptr_t addr;
    ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, len, &addr));

    ASSERT_OK(GetMemoryUsage(shadow_koid, &alloc_mem_use));

    // ..and poison it.
    ASAN_POISON_MEMORY_REGION((void*)addr, len);

    // Snapshot the memory use after the allocation.
    ASSERT_OK(GetMemoryUsage(shadow_koid, &memset_mem_use));

    // Unpoison the shadow.
    __sanitizer_fill_shadow(addr, len, /* value */ 0, /* threshold */ 0);

    ASSERT_OK(GetMemoryUsage(shadow_koid, &fill_shadow_mem_use));
    ASSERT_OK(GetCommitChangeEvents(shadow_koid, &end_events));

    // Deallocate the memory.
    ASSERT_OK(vmar.unmap(addr, len));
    __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);
  } while (end_events != start_events);
  EXPECT_GE(alloc_mem_use, init_mem_use, "");
  EXPECT_GT(memset_mem_use, alloc_mem_use, "");
  EXPECT_LT(fill_shadow_mem_use, memset_mem_use, "");
}

TEST(SanitizerUtilsTest, FillShadowSmall) {
  zx_koid_t shadow_koid;
  ASSERT_OK(GetAsanShadowVmoKoid(&shadow_koid));

  size_t shadow_scale;
  __asan_get_shadow_mapping(&shadow_scale, nullptr);

  // This tests that unpoisoning less than 1 shadow page of memory works.
  // This size ends up being three shadow pages, that way we can guarantee to
  // always have an address that is aligned to a shadow page.
  const size_t len = (kPageSize << shadow_scale) * 3;

  // We are testing that the shadow decommit operation works.
  // A previous test could have left the shadow in an uncommitted state.
  // By creating an aligned vmar, and decommitting its shadow before the test
  // starts, we guarantee that all the vmos we map inside it will have a
  // decommitted shadow as well.
  zx::vmar vmar;
  uintptr_t vmar_addr;
  ASSERT_OK(zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_64KB, 0, len, &vmar, &vmar_addr));
  auto cleanup = fit::defer([&vmar]() { vmar.destroy(); });

  __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(0, 0, &vmo));

  std::array<size_t, 4> sizes = {kPageSize << shadow_scale, (kPageSize / 2) << shadow_scale,
                                 (kPageSize + 1) << shadow_scale, kPageSize};

  std::array<ssize_t, 3> offsets = {-(1 << shadow_scale), 0, (1 << shadow_scale)};

  for (const auto size : sizes) {
    for (const auto offset : offsets) {
      uint64_t start_events, end_events;
      uint64_t init_mem_use;
      uint64_t final_mem_use;

      do {
        ASSERT_OK(GetCommitChangeEvents(shadow_koid, &start_events));

        PrefaultStackPages();

        uintptr_t addr;
        ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, len, &addr));
        // Align base to the next shadow page, leaving one shadow page to its left.
        uintptr_t base = (addr + (kPageSize << shadow_scale)) & -(kPageSize << shadow_scale);

        ASSERT_OK(GetMemoryUsage(shadow_koid, &init_mem_use));

        // Poison the shadow.
        ASAN_POISON_MEMORY_REGION((void*)(base + offset), size);

        // Unpoison it.
        __sanitizer_fill_shadow(base + offset, size, 0 /* val */, 0 /* threshold */);

        ASSERT_OK(GetMemoryUsage(shadow_koid, &final_mem_use));

        ASSERT_OK(GetCommitChangeEvents(shadow_koid, &end_events));

        // Deallocate the memory.
        ASSERT_OK(vmar.unmap(addr, len));
        __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);
      } while (start_events != end_events);

      // At most we are leaving 2 ASAN shadow pages committed.
      EXPECT_LE(init_mem_use, final_mem_use, "");
      EXPECT_LE(final_mem_use - init_mem_use, kPageSize * 2, "");
    }
  }
}

TEST(SanitizerUtilsTest, FillShadowPartialPages) {
  zx_koid_t shadow_koid;
  ASSERT_OK(GetAsanShadowVmoKoid(&shadow_koid));

  size_t shadow_scale;
  __asan_get_shadow_mapping(&shadow_scale, nullptr);
  const size_t len = (kPageSize << shadow_scale) * 7;
  const size_t shadow_granule = (1 << shadow_scale);

  // We are testing that the shadow decommit operation works.
  // A previous test could have left the shadow in an uncommitted state.
  // By creating an aligned vmar, and decommitting its shadow before the test
  // starts, we guarantee that all the vmos we map inside it will have a
  // decommitted shadow as well.
  zx::vmar vmar;
  uintptr_t vmar_addr;
  ASSERT_OK(zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_64KB, 0, len, &vmar, &vmar_addr));
  auto cleanup = fit::defer([&vmar]() { vmar.destroy(); });

  __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);

  std::array<size_t, 4> paddings = {1, kPageSize, 127, (kPageSize) + 16};

  for (size_t padding : paddings) {
    // __sanitizer_fill_shadow works with sizes aligned to shadow_granule.
    padding = fbl::round_up(padding, shadow_granule);
    uint64_t start_events, end_events;
    uint64_t init_mem_use;
    uint64_t final_mem_use;
    do {
      ASSERT_OK(GetCommitChangeEvents(shadow_koid, &start_events));

      PrefaultStackPages();

      // Allocate memory...
      zx::vmo vmo;
      ASSERT_OK(zx::vmo::create(0, 0, &vmo));
      uintptr_t addr;
      ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, len, &addr));

      // Leave the first and last shadow pages unpoisoned.
      uintptr_t poison_base = addr + (kPageSize << shadow_scale);
      size_t poison_len = len - (kPageSize << shadow_scale) * 2;

      // Partially poison some of the memory.
      poison_base += padding;
      poison_len -= padding * 2;

      ASSERT_OK(GetMemoryUsage(shadow_koid, &init_mem_use));

      ASAN_POISON_MEMORY_REGION((void*)poison_base, poison_len);

      // Unpoison the shadow.
      __sanitizer_fill_shadow(poison_base, poison_len, /* value */ 0, /* threshold */ 0);

      ASSERT_OK(GetMemoryUsage(shadow_koid, &final_mem_use));

      ASSERT_OK(GetCommitChangeEvents(shadow_koid, &end_events));

      // Deallocate the memory.
      ASSERT_OK(vmar.unmap(addr, len));
      __sanitizer_fill_shadow(vmar_addr, len, /* value */ 0, /* threshold */ 0);
    } while (end_events != start_events);

    // We expect the memory use to stay the same.
    EXPECT_EQ(init_mem_use, final_mem_use, "");
  }
}

#endif

void RunExe(std::string_view path, uint32_t expected_ret) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (!root_dir) {
    root_dir = "";
  }
  std::string file(root_dir);
  file += path;

  zx::process child;
  const char* argv[] = {file.c_str(), nullptr};
  ASSERT_OK(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                       child.reset_and_get_address()));

  zx_signals_t signals;
  ASSERT_OK(child.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);

  zx_info_process_t info;
  ASSERT_OK(child.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));

  EXPECT_EQ(info.return_code, expected_ret);
}

TEST(SanitizerUtilsTest, ProcessExitHook) {
  RunExe("/bin/sanitizer-exit-hook-test-helper", kHookStatus);
}

TEST(SanitizerUtilsTest, ModuleLoadedStartup) {
  RunExe("/bin/sanitizer-module-loaded-test-helper", 0);
}

}  // namespace
