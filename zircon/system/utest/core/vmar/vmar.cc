// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <lib/fzl/memory-probe.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdalign.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/features.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <atomic>
#include <climits>
#include <iterator>
#include <limits>
#include <thread>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

// These tests focus on the semantics of the VMARs themselves.  For heavier
// testing of the mapping permissions, see the VMO tests.

// Check that these values are consistent.
static_assert(ZX_VMO_OP_DECOMMIT == ZX_VMAR_OP_DECOMMIT);

namespace {

const char kProcessName[] = "test-proc-vmar";

const zx_vm_option_t kRwxMapPerm = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE;
const zx_vm_option_t kRwxAllocPerm =
    ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_EXECUTE;

// Helper routine for other tests.  If bit i (< *page_count*) in *bitmap* is set, then
// checks that *base* + i * zx_system_get_page_size() is mapped.  Otherwise checks that it is not
// mapped.
bool check_pages_mapped(zx_handle_t process, uintptr_t base, uint64_t bitmap, size_t page_count) {
  uint8_t buf[1];
  size_t len;

  size_t i = 0;
  while (bitmap && i < page_count) {
    zx_status_t expected = (bitmap & 1) ? ZX_OK : ZX_ERR_NO_MEMORY;
    if (zx_process_read_memory(process, base + i * zx_system_get_page_size(), buf, 1, &len) !=
        expected) {
      return false;
    }
    ++i;
    bitmap >>= 1;
  }
  return true;
}

TEST(Vmar, DestroyTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  zx_handle_t sub_vmar;
  uintptr_t sub_region_addr;
  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             1024 * zx_system_get_page_size(), &sub_vmar, &sub_region_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_destroy(sub_vmar), ZX_OK);

  zx_handle_t region;
  uintptr_t region_addr;
  EXPECT_EQ(zx_vmar_allocate(sub_vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             10 * zx_system_get_page_size(), &region, &region_addr),
            ZX_ERR_BAD_STATE);

  EXPECT_EQ(zx_handle_close(sub_vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

TEST(Vmar, BasicAllocateTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t region1, region2;
  uintptr_t region1_addr, region2_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t region1_size = zx_system_get_page_size() * 10;
  const size_t region2_size = zx_system_get_page_size();

  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, region1_size,
                             &region1, &region1_addr),
            ZX_OK);

  ASSERT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, region2_size,
                             &region2, &region2_addr),
            ZX_OK);
  EXPECT_GE(region2_addr, region1_addr);
  EXPECT_LE(region2_addr + region2_size, region1_addr + region1_size);

  EXPECT_EQ(zx_handle_close(region1), ZX_OK);
  EXPECT_EQ(zx_handle_close(region2), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

TEST(Vmar, MapInCompactTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t region;
  uintptr_t region_addr, map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t region_size = zx_system_get_page_size() * 10;
  const size_t map_size = zx_system_get_page_size();

  ASSERT_EQ(zx_vmo_create(map_size, 0, &vmo), ZX_OK);

  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_COMPACT, 0,
                             region_size, &region, &region_addr),
            ZX_OK);

  ASSERT_EQ(zx_vmar_map(region, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, map_size, &map_addr),
            ZX_OK);
  EXPECT_GE(map_addr, region_addr);
  EXPECT_LE(map_addr + map_size, region_addr + region_size);

  // Make a second allocation
  ASSERT_EQ(zx_vmar_map(region, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, map_size, &map_addr),
            ZX_OK);
  EXPECT_GE(map_addr, region_addr);
  EXPECT_LE(map_addr + map_size, region_addr + region_size);

  EXPECT_EQ(zx_handle_close(region), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

TEST(Vmar, MapInUpperLimitTest) {
  const size_t kRegionPages = 100;
  const size_t kSubRegions = kRegionPages / 2;

  zx_handle_t process;
  zx_handle_t process_vmar;
  zx_handle_t vmo;
  zx_handle_t parent_region;
  uintptr_t parent_region_addr;
  uintptr_t map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &process_vmar),
            ZX_OK);

  const size_t region_size = zx_system_get_page_size() * kRegionPages;
  const size_t map_size = zx_system_get_page_size();

  ASSERT_EQ(zx_vmo_create(region_size, 0, &vmo), ZX_OK);

  // Allocate a region and allow mapping to a specific location to enable specifying an upper limit.
  ASSERT_EQ(zx_vmar_allocate(process_vmar,
                             ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
                             region_size, &parent_region, &parent_region_addr),
            ZX_OK);

  // Set the upper limit for all maps to the midpoint of the parent region.
  const uintptr_t upper_limit = region_size / 2;
  const zx_vm_option_t options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_OFFSET_IS_UPPER_LIMIT;

  // An upper limit beyond the end of the parent region should fail.
  ASSERT_EQ(zx_vmar_map(parent_region, options, region_size + zx_system_get_page_size(), vmo, 0,
                        zx_system_get_page_size(), &map_addr),
            ZX_ERR_INVALID_ARGS);

  // A size greater than the upper limit should fail.
  ASSERT_EQ(zx_vmar_map(parent_region, options, zx_system_get_page_size(), vmo, 0,
                        zx_system_get_page_size() * 2, &map_addr),
            ZX_ERR_INVALID_ARGS);

  // A size larger than the parent region should fail.
  ASSERT_EQ(zx_vmar_map(parent_region, options, zx_system_get_page_size(), vmo, 0,
                        region_size + zx_system_get_page_size(), &map_addr),
            ZX_ERR_INVALID_ARGS);

  // A size and upper limit equal to the parent region should succeed.
  ASSERT_EQ(zx_vmar_map(parent_region, options, region_size, vmo, 0, region_size, &map_addr),
            ZX_OK);
  ASSERT_EQ(parent_region_addr, map_addr);

  ASSERT_EQ(zx_vmar_unmap(parent_region, map_addr, region_size), ZX_OK);

  // Every map should conform to the upper limit.
  for (size_t i = 0; i < kSubRegions; i++) {
    ASSERT_EQ(zx_vmar_map(parent_region, options, upper_limit, vmo, 0, map_size, &map_addr), ZX_OK);
    EXPECT_GE(map_addr, parent_region_addr);
    EXPECT_LE(map_addr + map_size, parent_region_addr + upper_limit);
  }

  // Mapping one more time should fail now that all of the VMAR below the upper limit is consumed.
  ASSERT_EQ(zx_vmar_map(parent_region, options, upper_limit, vmo, 0, map_size, &map_addr),
            ZX_ERR_NO_RESOURCES);

  // Mapping one more time without the upper limit should succeed.
  ASSERT_EQ(zx_vmar_map(parent_region, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, map_size,
                        &map_addr),
            ZX_OK);
  EXPECT_GE(map_addr, parent_region_addr);
  EXPECT_LE(map_addr + map_size, parent_region_addr + region_size);

  EXPECT_EQ(zx_handle_close(parent_region), ZX_OK);
  EXPECT_EQ(zx_handle_close(process_vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Attempt to allocate out of the region bounds
TEST(Vmar, AllocateOobTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t region1, region2;
  uintptr_t region1_addr, region2_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t region1_size = zx_system_get_page_size() * 10;

  ASSERT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
                       region1_size, &region1, &region1_addr),
      ZX_OK);

  EXPECT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             region1_size, zx_system_get_page_size(), &region2, &region2_addr),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             region1_size - zx_system_get_page_size(),
                             zx_system_get_page_size() * 2, &region2, &region2_addr),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(zx_handle_close(region1), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Attempt to make unsatisfiable allocations
TEST(Vmar, AllocateUnsatisfiableTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t region1, region2, region3;
  uintptr_t region1_addr, region2_addr, region3_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t region1_size = zx_system_get_page_size() * 10;

  ASSERT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
                       region1_size, &region1, &region1_addr),
      ZX_OK);

  // Too large to fit in the region should get ZX_ERR_INVALID_ARGS
  EXPECT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             region1_size + zx_system_get_page_size(), &region2, &region2_addr),
            ZX_ERR_INVALID_ARGS);

  // Allocate the whole range, should work
  ASSERT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, region1_size,
                             &region2, &region2_addr),
            ZX_OK);
  EXPECT_EQ(region2_addr, region1_addr);

  // Attempt to allocate a page inside of the full region
  EXPECT_EQ(zx_vmar_allocate(region1, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size(), &region3, &region3_addr),
            ZX_ERR_NO_RESOURCES);

  EXPECT_EQ(zx_handle_close(region2), ZX_OK);
  EXPECT_EQ(zx_handle_close(region1), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Test that virtual address space beginning at 0x200000 is accessible
TEST(Vmar, AllocateAtLowAddressTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);

  zx_info_vmar_t info;
  ASSERT_EQ(zx_object_get_info(vmar, ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL), ZX_OK);
  ASSERT_LE(info.base, 0x200000);

  zx_vaddr_t addr;
  ASSERT_EQ(zx_vmar_map(vmar, 0, 0x200000 - info.base, vmo, 0, zx_system_get_page_size(), &addr),
            ZX_OK);

  EXPECT_EQ(zx_handle_close(process), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
}

// Validate that when we destroy a VMAR, all operations on it
// and its children fail.
TEST(Vmar, DestroyedVmarTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t region[3] = {};
  uintptr_t region_addr[3];
  uintptr_t map_addr[2];

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);

  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             10 * zx_system_get_page_size(), &region[0], &region_addr[0]),
            ZX_OK);

  // Create a mapping in region[0], so we can try to unmap it later
  ASSERT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size(), &map_addr[0]),
            ZX_OK);

  // Create a subregion in region[0], so we can try to operate on it later
  ASSERT_EQ(zx_vmar_allocate(region[0], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size(), &region[1], &region_addr[1]),
            ZX_OK);

  // Create a mapping in region[1], so we can try to unmap it later
  ASSERT_EQ(zx_vmar_map(region[1], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size(), &map_addr[1]),
            ZX_OK);

  // Check that both mappings work
  {
    uint8_t buf = 5;
    size_t len;
    EXPECT_EQ(zx_process_write_memory(process, map_addr[0], &buf, 1, &len), ZX_OK);
    EXPECT_EQ(len, 1U);

    buf = 0;
    EXPECT_EQ(zx_process_read_memory(process, map_addr[1], &buf, 1, &len), ZX_OK);
    EXPECT_EQ(len, 1U);
    EXPECT_EQ(buf, 5U);
  }

  // Destroy region[0], which should also destroy region[1]
  ASSERT_EQ(zx_vmar_destroy(region[0]), ZX_OK);

  for (size_t i = 0; i < 2; ++i) {
    // Make sure the handles are still valid
    EXPECT_EQ(zx_object_get_info(region[i], ZX_INFO_HANDLE_VALID, NULL, 0u, NULL, NULL), ZX_OK);

    // Make sure we can't access the memory mappings anymore
    {
      uint8_t buf;
      size_t read;
      EXPECT_EQ(zx_process_read_memory(process, map_addr[i], &buf, 1, &read), ZX_ERR_NO_MEMORY);
    }

    // All operations on region[0] and region[1] should fail with ZX_ERR_BAD_STATE
    EXPECT_EQ(zx_vmar_destroy(region[i]), ZX_ERR_BAD_STATE);
    EXPECT_EQ(zx_vmar_allocate(region[i], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                               zx_system_get_page_size(), &region[1], &region_addr[2]),
              ZX_ERR_BAD_STATE);
    EXPECT_EQ(zx_vmar_unmap(region[i], map_addr[i], zx_system_get_page_size()), ZX_ERR_BAD_STATE);
    EXPECT_EQ(zx_vmar_protect(region[i], ZX_VM_PERM_READ, map_addr[i], zx_system_get_page_size()),
              ZX_ERR_BAD_STATE);
    EXPECT_EQ(
        zx_vmar_map(region[i], ZX_VM_PERM_READ, 0, vmo, 0, zx_system_get_page_size(), &map_addr[i]),
        ZX_ERR_BAD_STATE);
  }

  // Make sure we can still operate on the parent of region[0]
  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size(), &region[2], &region_addr[2]),
            ZX_OK);

  for (zx_handle_t h : region) {
    EXPECT_EQ(zx_handle_close(h), ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Create a mapping, destroy the VMAR it is in, then attempt to create a new
// mapping over it.
TEST(Vmar, MapOverDestroyedTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo, vmo2;
  zx_handle_t region[2] = {};
  uintptr_t region_addr[2];
  uintptr_t map_addr;
  size_t len;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo2), ZX_OK);

  ASSERT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
                       10 * zx_system_get_page_size(), &region[0], &region_addr[0]),
      ZX_OK);

  // Create a subregion in region[0], so we can try to operate on it later
  ASSERT_EQ(zx_vmar_allocate(region[0], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size(), &region[1], &region_addr[1]),
            ZX_OK);

  // Create a mapping in region[1], so we can try to unmap it later
  ASSERT_EQ(zx_vmar_map(region[1], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size(), &map_addr),
            ZX_OK);

  // Check that the mapping worked
  {
    uint8_t buf = 5;
    ASSERT_EQ(zx_vmo_write(vmo, &buf, 0, 1), ZX_OK);

    buf = 0;
    EXPECT_EQ(zx_process_read_memory(process, map_addr, &buf, 1, &len), ZX_OK);
    EXPECT_EQ(len, 1U);
    EXPECT_EQ(buf, 5U);
  }

  // Destroy region[1], which should unmap the VMO
  ASSERT_EQ(zx_vmar_destroy(region[1]), ZX_OK);

  // Make sure we can't access the memory mappings anymore
  {
    uint8_t buf;
    size_t read;
    EXPECT_EQ(zx_process_read_memory(process, map_addr, &buf, 1, &read), ZX_ERR_NO_MEMORY);
  }

  uintptr_t new_map_addr;
  EXPECT_EQ(
      zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  map_addr - region_addr[0], vmo2, 0, zx_system_get_page_size(), &new_map_addr),
      ZX_OK);
  EXPECT_EQ(new_map_addr, map_addr);

  // Make sure we can read, and we don't see the old memory mapping
  {
    uint8_t buf;
    size_t read;
    EXPECT_EQ(zx_process_read_memory(process, map_addr, &buf, 1, &read), ZX_OK);
    EXPECT_EQ(read, 1U);
    EXPECT_EQ(buf, 0U);
  }

  for (zx_handle_t h : region) {
    EXPECT_EQ(zx_handle_close(h), ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo2), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

struct AlignTestdata {
  zx_vm_option_t aligment;
  int zero_bits;
};

AlignTestdata align_data[]{
    {ZX_VM_ALIGN_1KB, 10},   {ZX_VM_ALIGN_2KB, 11},   {ZX_VM_ALIGN_4KB, 12},
    {ZX_VM_ALIGN_8KB, 13},   {ZX_VM_ALIGN_16KB, 14},  {ZX_VM_ALIGN_32KB, 15},
    {ZX_VM_ALIGN_64KB, 16},  {ZX_VM_ALIGN_128KB, 17}, {ZX_VM_ALIGN_256KB, 18},
    {ZX_VM_ALIGN_512KB, 19}, {ZX_VM_ALIGN_1MB, 20},   {ZX_VM_ALIGN_2MB, 21},
    {ZX_VM_ALIGN_4MB, 22},   {ZX_VM_ALIGN_8MB, 23},   {ZX_VM_ALIGN_16MB, 24},
    {ZX_VM_ALIGN_32MB, 25},  {ZX_VM_ALIGN_64MB, 26},  {ZX_VM_ALIGN_128MB, 27},
    {ZX_VM_ALIGN_256MB, 28}, {ZX_VM_ALIGN_512MB, 29}, {ZX_VM_ALIGN_1GB, 30},
    {ZX_VM_ALIGN_2GB, 31},   {ZX_VM_ALIGN_4GB, 32}};

// Create a manually aligned |vmar| to |vmar_size|, this is needed only
// needed when testing the alignment flags.
zx_status_t MakeManualAlignedVmar(size_t vmar_size, zx_handle_t* vmar) {
  zx_info_vmar_t vmar_info;
  auto status = zx_object_get_info(zx_vmar_root_self(), ZX_INFO_VMAR, &vmar_info, sizeof(vmar_info),
                                   NULL, NULL);
  if (status != ZX_OK) {
    return status;
  }

  const size_t root_vmar_end = vmar_info.base + vmar_info.len;
  size_t start = fbl::round_up(vmar_info.base, vmar_size);

  zx_vaddr_t root_addr = 0u;
  for (; start < root_vmar_end; start += vmar_size) {
    status = zx_vmar_allocate(
        zx_vmar_root_self(),
        ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_SPECIFIC,
        (start - vmar_info.base), vmar_size, vmar, &root_addr);
    if (status == ZX_OK) {
      break;
    }
  }
  return status;
}

TEST(Vmar, AlignmentVmarMapTest) {
  const size_t size = zx_system_get_page_size() * 2;
  const auto vmar_size = (8ull * 1024 * 1024 * 1024);

  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  ASSERT_EQ(MakeManualAlignedVmar(vmar_size, &vmar), ZX_OK);

  // Specific base + offset does not meet the alignment, so it fails.
  zx_vaddr_t dummy;
  EXPECT_EQ(
      zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_ALIGN_64KB | ZX_VM_SPECIFIC,
                  4096, vmo, 0, size, &dummy),
      ZX_ERR_INVALID_ARGS);

  // Specific base + offset meets alignment, it should succeed.
  EXPECT_EQ(
      zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_ALIGN_64KB | ZX_VM_SPECIFIC,
                  64 * 1024, vmo, 0, size, &dummy),
      ZX_OK);
  ASSERT_EQ(zx_vmar_unmap(vmar, dummy, 64 * 1024), ZX_OK);

  // Minimum supported alignments range is 2^10 to 2^32
  zx_vm_option_t bad_align_low = (9u << ZX_VM_ALIGN_BASE);
  zx_vm_option_t bad_align_high = (33u << ZX_VM_ALIGN_BASE);

  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | bad_align_low, 0, vmo, 0, size,
                        &dummy),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | bad_align_high, 0, vmo, 0, size,
                        &dummy),
            ZX_ERR_INVALID_ARGS);

  // Test all supported alignments.
  for (const auto& d : align_data) {
    zx_vaddr_t mapping_addr = 0u;
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | d.aligment, 0, vmo, 0, size,
                          &mapping_addr),
              ZX_OK);

    ASSERT_NE(mapping_addr, 0u);
    EXPECT_GE(__builtin_ctzll(mapping_addr), d.zero_bits);
    // touch memory and unmap.
    reinterpret_cast<uint64_t*>(mapping_addr)[1] = 0x1234321;
    ASSERT_EQ(zx_vmar_unmap(vmar, mapping_addr, size), ZX_OK);
  }

  ASSERT_EQ(zx_vmar_destroy(vmar), ZX_OK);
  ASSERT_EQ(zx_handle_close(vmar), ZX_OK);
  ASSERT_EQ(zx_handle_close(vmo), ZX_OK);
}

TEST(Vmar, AlignmentVmarAllocateTest) {
  const size_t size = zx_system_get_page_size() * 16;
  const auto vmar_size = (8ull * 1024 * 1024 * 1024);

  zx_handle_t vmar = ZX_HANDLE_INVALID;
  ASSERT_EQ(MakeManualAlignedVmar(vmar_size, &vmar), ZX_OK);

  // Specific base + offset does not meet the alignment, so it fails.
  zx_vaddr_t dummy_a;
  zx_handle_t dummy_h;
  EXPECT_EQ(zx_vmar_allocate(
                vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_64KB | ZX_VM_SPECIFIC,
                4096, size, &dummy_h, &dummy_a),
            ZX_ERR_INVALID_ARGS);

  // Specific base + offset meets alignment, it should succeed.
  EXPECT_EQ(zx_vmar_allocate(
                vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_ALIGN_64KB | ZX_VM_SPECIFIC,
                64 * 1024, size, &dummy_h, &dummy_a),
            ZX_OK);
  ASSERT_EQ(zx_vmar_destroy(dummy_h), ZX_OK);
  ASSERT_EQ(zx_handle_close(dummy_h), ZX_OK);

  // Minimum supported alignments range is 2^10 to 2^32
  const zx_vm_option_t bad_align_low = (9u << ZX_VM_ALIGN_BASE);
  const zx_vm_option_t bad_align_high = (33u << ZX_VM_ALIGN_BASE);

  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | bad_align_low, 0,
                             size, &dummy_h, &dummy_a),
            ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | bad_align_high, 0,
                             size, &dummy_h, &dummy_a),
            ZX_ERR_INVALID_ARGS);

  // Test all supported alignments.
  for (const auto& d : align_data) {
    zx_handle_t child_vmar;
    uintptr_t mapping_addr = 0u;
    ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | d.aligment, 0, size,
                               &child_vmar, &mapping_addr),
              ZX_OK);

    ASSERT_NE(mapping_addr, 0u);
    EXPECT_GE(__builtin_ctzll(mapping_addr), d.zero_bits);
    ASSERT_EQ(zx_vmar_destroy(child_vmar), ZX_OK);
    ASSERT_EQ(zx_handle_close(child_vmar), ZX_OK);
  }

  ASSERT_EQ(zx_vmar_destroy(vmar), ZX_OK);
  ASSERT_EQ(zx_handle_close(vmar), ZX_OK);
}

// Test to ensure we can map from a given VMO offset with MAP_RANGE enabled. This tests against
// a bug found when creating MmioBuffer with a provided VMO and an offset.
TEST(Vmar, VmarMapRangeOffsetTest) {
  zx::vmar vmar;
  zx::vmo vmo;
  zx::process process;
  ASSERT_EQ(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName) - 1, 0,
                                &process, &vmar),
            ZX_OK);
  ASSERT_EQ(zx::vmo::create(zx_system_get_page_size() * 4, 0, &vmo), ZX_OK);
  uintptr_t mapping;
  EXPECT_EQ(vmar.map(ZX_VM_MAP_RANGE, 0, vmo, 0x2000, 0x1000, &mapping), ZX_OK);
}

// Attempt overmapping with FLAG_SPECIFIC to ensure it fails
TEST(Vmar, OvermappingTest) {
  zx_handle_t process;
  zx_handle_t region[3] = {};
  zx_handle_t vmar;
  zx_handle_t vmo, vmo2;
  uintptr_t region_addr[3];
  uintptr_t map_addr[2];

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size() * 4, 0, &vmo2), ZX_OK);

  ASSERT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
                       10 * zx_system_get_page_size(), &region[0], &region_addr[0]),
      ZX_OK);

  // Create a mapping, and try to map on top of it
  ASSERT_EQ(
      zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  zx_system_get_page_size(), vmo, 0, 2 * zx_system_get_page_size(), &map_addr[0]),
      ZX_OK);

  // Attempt a full overmapping
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        map_addr[0] - region_addr[0], vmo2, 0, 2 * zx_system_get_page_size(),
                        &map_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Attempt a partial overmapping
  EXPECT_EQ(
      zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  map_addr[0] - region_addr[0], vmo2, 0, zx_system_get_page_size(), &map_addr[1]),
      ZX_ERR_ALREADY_EXISTS);

  // Attempt an overmapping that is larger than the original mapping
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        map_addr[0] - region_addr[0], vmo2, 0, 4 * zx_system_get_page_size(),
                        &map_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Attempt to allocate a region on top
  EXPECT_EQ(zx_vmar_allocate(region[0], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             map_addr[0] - region_addr[0], zx_system_get_page_size(), &region[1],
                             &region_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Unmap the mapping
  ASSERT_EQ(zx_vmar_unmap(region[0], map_addr[0], 2 * zx_system_get_page_size()), ZX_OK);

  // Create a region, and try to map on top of it
  ASSERT_EQ(zx_vmar_allocate(region[0], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             zx_system_get_page_size(), 2 * zx_system_get_page_size(), &region[1],
                             &region_addr[1]),
            ZX_OK);

  // Attempt a full overmapping
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        region_addr[1] - region_addr[0], vmo2, 0, 2 * zx_system_get_page_size(),
                        &map_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Attempt a partial overmapping
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        region_addr[1] - region_addr[0], vmo2, 0, zx_system_get_page_size(),
                        &map_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Attempt an overmapping that is larger than the original region
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        region_addr[1] - region_addr[0], vmo2, 0, 4 * zx_system_get_page_size(),
                        &map_addr[1]),
            ZX_ERR_ALREADY_EXISTS);

  // Attempt to allocate a region on top
  EXPECT_EQ(zx_vmar_allocate(region[0], ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             region_addr[1] - region_addr[0], zx_system_get_page_size(), &region[2],
                             &region_addr[2]),
            ZX_ERR_ALREADY_EXISTS);

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo2), ZX_OK);
  EXPECT_EQ(zx_handle_close(region[0]), ZX_OK);
  EXPECT_EQ(zx_handle_close(region[1]), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Test passing in bad arguments
TEST(Vmar, InvalidArgsTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t region;
  uintptr_t region_addr, map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  ASSERT_EQ(zx_vmo_create(4 * zx_system_get_page_size(), 0, &vmo), ZX_OK);

  // Bad handle
  EXPECT_EQ(zx_vmar_destroy(vmo), ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(zx_vmar_allocate(vmo, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             10 * zx_system_get_page_size(), &region, &region_addr),
            ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(zx_vmar_map(vmo, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, process, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(zx_vmar_unmap(vmo, 0, 0), ZX_ERR_WRONG_TYPE);
  EXPECT_EQ(zx_vmar_protect(vmo, ZX_VM_PERM_READ, 0, 0), ZX_ERR_WRONG_TYPE);

  // Allocating with non-zero offset and without FLAG_SPECIFIC or FLAG_OFFSET_IS_UPPER_LIMIT.
  EXPECT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, zx_system_get_page_size(),
                       10 * zx_system_get_page_size(), &region, &region_addr),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, zx_system_get_page_size(), vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_ERR_INVALID_ARGS);

  // Allocating with non-zero offset with both SPECIFIC* and OFFSET_IS_UPPER_LIMIT.
  EXPECT_EQ(
      zx_vmar_allocate(
          vmar,
          ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC | ZX_VM_OFFSET_IS_UPPER_LIMIT,
          zx_system_get_page_size(), 10 * zx_system_get_page_size(), &region, &region_addr),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(
      zx_vmar_map(vmar,
                  ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | ZX_VM_OFFSET_IS_UPPER_LIMIT,
                  zx_system_get_page_size(), vmo, 0, 4 * zx_system_get_page_size(), &map_addr),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_allocate(vmar,
                             ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC_OVERWRITE |
                                 ZX_VM_OFFSET_IS_UPPER_LIMIT,
                             zx_system_get_page_size(), 10 * zx_system_get_page_size(), &region,
                             &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(
      zx_vmar_map(vmar,
                  ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE |
                      ZX_VM_OFFSET_IS_UPPER_LIMIT,
                  zx_system_get_page_size(), vmo, 0, 4 * zx_system_get_page_size(), &map_addr),
      ZX_ERR_INVALID_ARGS);

  // Allocate with ZX_VM_PERM_READ.
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_PERM_READ, zx_system_get_page_size(),
                             10 * zx_system_get_page_size(), &region, &region_addr),
            ZX_ERR_INVALID_ARGS);

  // Using MAP_RANGE with SPECIFIC_OVERWRITE
  EXPECT_EQ(
      zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_SPECIFIC_OVERWRITE | ZX_VM_MAP_RANGE,
                  zx_system_get_page_size(), vmo, 0, 4 * zx_system_get_page_size(), &map_addr),
      ZX_ERR_INVALID_ARGS);

  // Bad OUT pointers
  uintptr_t* bad_addr_ptr = reinterpret_cast<uintptr_t*>(1);
  zx_handle_t* bad_handle_ptr = reinterpret_cast<zx_handle_t*>(1);
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             10 * zx_system_get_page_size(), &region, bad_addr_ptr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             10 * zx_system_get_page_size(), bad_handle_ptr, &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), bad_addr_ptr),
            ZX_ERR_INVALID_ARGS);

  // Non-page-aligned arguments
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size() - 1, &region, &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_allocate(
                vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC,
                zx_system_get_page_size() - 1, zx_system_get_page_size(), &region, &region_addr),
            ZX_ERR_INVALID_ARGS);
  // Try the invalid maps with and without ZX_VM_MAP_RANGE.
  for (size_t i = 0; i < 2; ++i) {
    const uint32_t map_range = i ? ZX_VM_MAP_RANGE : 0;
    // Specific, misaligned vmar offset
    EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | map_range,
                          zx_system_get_page_size() - 1, vmo, 0, 4 * zx_system_get_page_size(),
                          &map_addr),
              ZX_ERR_INVALID_ARGS);
    // Specific, misaligned vmo offset
    EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | map_range,
                          zx_system_get_page_size(), vmo, zx_system_get_page_size() - 1,
                          3 * zx_system_get_page_size(), &map_addr),
              ZX_ERR_INVALID_ARGS);
    // Non-specific, misaligned vmo offset
    EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | map_range, 0, vmo,
                          zx_system_get_page_size() - 1, 3 * zx_system_get_page_size(), &map_addr),
              ZX_ERR_INVALID_ARGS);
  }
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr + 1, zx_system_get_page_size()), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, map_addr + 1, zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size()), ZX_OK);

  // Overflowing vmo_offset
  EXPECT_EQ(
      zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo,
                  UINT64_MAX + 1 - zx_system_get_page_size(), zx_system_get_page_size(), &map_addr),
      ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo,
                        UINT64_MAX + 1 - 2 * zx_system_get_page_size(), zx_system_get_page_size(),
                        &map_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, zx_system_get_page_size()), ZX_OK);

  // size=0
  EXPECT_EQ(
      zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, 0, &region, &region_addr),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, 0, &map_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 0), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, map_addr, 0), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size()), ZX_OK);

  // size rounds up to 0
  const size_t bad_size = std::numeric_limits<size_t>::max() - zx_system_get_page_size() + 2;
  assert(((bad_size + zx_system_get_page_size() - 1) & ~(zx_system_get_page_size() - 1)) == 0);
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, bad_size, &region,
                             &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, bad_size, &map_addr),
            ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_MAP_RANGE, 0, vmo, 0, bad_size, &map_addr),
            ZX_ERR_OUT_OF_RANGE);
  // Attempt bad protect/unmaps
  EXPECT_EQ(
      zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  zx_system_get_page_size(), vmo, 0, 4 * zx_system_get_page_size(), &map_addr),
      ZX_OK);
  for (ssize_t i = -1; i < 2; ++i) {
    EXPECT_EQ(
        zx_vmar_protect(vmar, ZX_VM_PERM_READ, map_addr + zx_system_get_page_size() * i, bad_size),
        ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(zx_vmar_unmap(vmar, map_addr + zx_system_get_page_size() * i, bad_size),
              ZX_ERR_INVALID_ARGS);
  }
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size()), ZX_OK);

  // Flags with invalid bits set
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_PERM_READ | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             4 * zx_system_get_page_size(), &region, &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | (1 << 31), 0, 4 * zx_system_get_page_size(),
                             &region, &region_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_CAN_MAP_EXECUTE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | (1 << 31), 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &map_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ | ZX_VM_CAN_MAP_WRITE, map_addr,
                            4 * zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(
      zx_vmar_protect(vmar, ZX_VM_PERM_READ | (1 << 31), map_addr, 4 * zx_system_get_page_size()),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size()), ZX_OK);

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Test passing in unaligned lens to unmap/protect
TEST(Vmar, UnalignedLenTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  ASSERT_EQ(zx_vmo_create(4 * zx_system_get_page_size(), 0, &vmo), ZX_OK);

  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ, 0, vmo, 0, 4 * zx_system_get_page_size(), &map_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, map_addr,
                            4 * zx_system_get_page_size() - 1),
            ZX_OK);
  EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size() - 1), ZX_OK);

  // Make sure we can't access the last page of the memory mappings anymore
  {
    uint8_t buf;
    size_t read;
    EXPECT_EQ(
        zx_process_read_memory(process, map_addr + 3 * zx_system_get_page_size(), &buf, 1, &read),
        ZX_ERR_NO_MEMORY);
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Test passing in unaligned lens to map
TEST(Vmar, UnalignedLenMapTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  ASSERT_EQ(zx_vmo_create(4 * zx_system_get_page_size(), 0, &vmo), ZX_OK);

  for (size_t i = 0; i < 2; ++i) {
    const uint32_t map_range = i ? ZX_VM_MAP_RANGE : 0;
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | map_range, 0, vmo, 0,
                          4 * zx_system_get_page_size() - 1, &map_addr),
              ZX_OK);

    // Make sure we can access the last page of the memory mapping
    {
      uint8_t buf;
      size_t read;
      EXPECT_EQ(
          zx_process_read_memory(process, map_addr + 3 * zx_system_get_page_size(), &buf, 1, &read),
          ZX_OK);
    }

    EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, 4 * zx_system_get_page_size() - 1), ZX_OK);
    // Make sure we can't access the last page of the memory mappings anymore
    {
      uint8_t buf;
      size_t read;
      EXPECT_EQ(
          zx_process_read_memory(process, map_addr + 3 * zx_system_get_page_size(), &buf, 1, &read),
          ZX_ERR_NO_MEMORY);
    }
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Validate that dropping vmar handle rights affects mapping privileges
TEST(Vmar, RightsDropTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t region;
  uintptr_t map_addr;
  uintptr_t region_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo), ZX_OK);

  const uint32_t test_rights[][3] = {
      {ZX_RIGHT_READ, ZX_VM_PERM_READ},
      {ZX_RIGHT_READ | ZX_RIGHT_WRITE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE},
      {ZX_RIGHT_READ | ZX_RIGHT_EXECUTE, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE},
  };
  for (size_t i = 0; i < std::size(test_rights); ++i) {
    uint32_t right = test_rights[i][0];
    uint32_t perm = test_rights[i][1];

    zx_handle_t new_h;
    ASSERT_EQ(zx_handle_duplicate(vmar, right, &new_h), ZX_OK);

    // Try to create a mapping with permissions we don't have
    EXPECT_EQ(zx_vmar_map(new_h, kRwxMapPerm, 0, vmo, 0, zx_system_get_page_size(), &map_addr),
              ZX_ERR_ACCESS_DENIED);

    // Try to create a mapping with permissions we do have
    ASSERT_EQ(zx_vmar_map(new_h, perm, 0, vmo, 0, zx_system_get_page_size(), &map_addr), ZX_OK);

    // Attempt to use protect to increase privileges
    EXPECT_EQ(zx_vmar_protect(new_h, kRwxMapPerm, map_addr, zx_system_get_page_size()),
              ZX_ERR_ACCESS_DENIED);

    EXPECT_EQ(zx_vmar_unmap(new_h, map_addr, zx_system_get_page_size()), ZX_OK);

    // Attempt to create a region that can map write (this would allow us to
    // then make writeable mappings inside of it).
    EXPECT_EQ(zx_vmar_allocate(new_h, kRwxAllocPerm, 0, 10 * zx_system_get_page_size(), &region,
                               &region_addr),
              ZX_ERR_ACCESS_DENIED);

    EXPECT_EQ(zx_handle_close(new_h), ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Validate that protect can't be used to escalate mapping privileges beyond
// the VMAR handle's and the original VMO handle's
TEST(Vmar, ProtectTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo), ZX_OK);

  const uint32_t test_rights[][3] = {
      {ZX_RIGHT_READ, ZX_VM_PERM_READ},
      {ZX_RIGHT_READ | ZX_RIGHT_WRITE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE},
      {ZX_RIGHT_READ | ZX_RIGHT_EXECUTE, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE},
  };
  for (size_t i = 0; i < std::size(test_rights); ++i) {
    uint32_t right = test_rights[i][0];
    zx_vm_option_t perm = test_rights[i][1];

    zx_handle_t new_h;
    ASSERT_EQ(zx_handle_duplicate(vmo, right | ZX_RIGHT_MAP, &new_h), ZX_OK);

    // Try to create a mapping with permissions we don't have
    EXPECT_EQ(zx_vmar_map(vmar, kRwxMapPerm, 0, new_h, 0, zx_system_get_page_size(), &map_addr),
              ZX_ERR_ACCESS_DENIED);

    // Try to create a mapping with permissions we do have
    ASSERT_EQ(zx_vmar_map(vmar, perm, 0, new_h, 0, zx_system_get_page_size(), &map_addr), ZX_OK);

    // Attempt to use protect to increase privileges to a level allowed by
    // the VMAR but not by the VMO handle
    EXPECT_EQ(zx_vmar_protect(vmar, kRwxMapPerm, map_addr, zx_system_get_page_size()),
              ZX_ERR_ACCESS_DENIED);

    EXPECT_EQ(zx_handle_close(new_h), ZX_OK);

    // Try again now that we closed the VMO handle
    EXPECT_EQ(zx_vmar_protect(vmar, kRwxMapPerm, map_addr, zx_system_get_page_size()),
              ZX_ERR_ACCESS_DENIED);

    EXPECT_EQ(zx_vmar_unmap(vmar, map_addr, zx_system_get_page_size()), ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Validate that a region can't be created with higher RWX privileges than its
// parent.
TEST(Vmar, NestedRegionPermsTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t region[2] = {};
  uintptr_t region_addr[2];
  uintptr_t map_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo), ZX_OK);

  // List of pairs of alloc/map perms to try to exclude
  const zx_vm_option_t test_perm[][2] = {
      {ZX_VM_CAN_MAP_READ, ZX_VM_PERM_READ},
      {ZX_VM_CAN_MAP_WRITE, ZX_VM_PERM_WRITE},
      {ZX_VM_CAN_MAP_EXECUTE, ZX_VM_PERM_EXECUTE},
  };

  for (size_t i = 0; i < std::size(test_perm); ++i) {
    const zx_vm_option_t excluded_alloc_perm = test_perm[i][0];
    const zx_vm_option_t excluded_map_perm = test_perm[i][1];

    ASSERT_EQ(zx_vmar_allocate(vmar, kRwxAllocPerm ^ excluded_alloc_perm, 0,
                               10 * zx_system_get_page_size(), &region[0], &region_addr[0]),
              ZX_OK);

    // Should fail since region[0] does not have the right perms
    EXPECT_EQ(zx_vmar_allocate(region[0], kRwxAllocPerm, 0, zx_system_get_page_size(), &region[1],
                               &region_addr[1]),
              ZX_ERR_ACCESS_DENIED);

    // Try to create a mapping in region[0] with the dropped rights
    EXPECT_EQ(zx_vmar_map(region[0], kRwxMapPerm, 0, vmo, 0, zx_system_get_page_size(), &map_addr),
              ZX_ERR_ACCESS_DENIED);

    // Successfully create a mapping in region[0] (skip if we excluded READ,
    // since all mappings must be readable on most CPUs)
    if (excluded_map_perm != ZX_VM_PERM_READ) {
      EXPECT_EQ(zx_vmar_map(region[0], kRwxMapPerm ^ excluded_map_perm, 0, vmo, 0,
                            zx_system_get_page_size(), &map_addr),
                ZX_OK);
      EXPECT_EQ(zx_vmar_unmap(region[0], map_addr, zx_system_get_page_size()), ZX_OK);
    }

    // Successfully create a subregion in region[0]
    EXPECT_EQ(zx_vmar_allocate(region[0], kRwxAllocPerm ^ excluded_alloc_perm, 0,
                               zx_system_get_page_size(), &region[1], &region_addr[1]),
              ZX_OK);
    EXPECT_EQ(zx_vmar_destroy(region[1]), ZX_OK);
    EXPECT_EQ(zx_handle_close(region[1]), ZX_OK);

    EXPECT_EQ(zx_vmar_destroy(region[0]), ZX_OK);
    EXPECT_EQ(zx_handle_close(region[0]), ZX_OK);
  }

  // Make sure we can't use SPECIFIC in a region without CAN_MAP_SPECIFIC
  ASSERT_EQ(zx_vmar_allocate(vmar, kRwxAllocPerm, 0, 10 * zx_system_get_page_size(), &region[0],
                             &region_addr[0]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_SPECIFIC | ZX_VM_PERM_READ, zx_system_get_page_size(), vmo,
                        0, zx_system_get_page_size(), &map_addr),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(zx_vmar_map(region[0], ZX_VM_SPECIFIC_OVERWRITE | ZX_VM_PERM_READ,
                        zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(), &map_addr),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(zx_vmar_destroy(region[0]), ZX_OK);
  EXPECT_EQ(zx_handle_close(region[0]), ZX_OK);

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

TEST(Vmar, ObjectInfoTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t region;
  uintptr_t region_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t region_size = zx_system_get_page_size() * 10;

  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, region_size,
                             &region, &region_addr),
            ZX_OK);

  zx_info_vmar_t info;
  ASSERT_EQ(zx_object_get_info(region, ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL), ZX_OK);
  EXPECT_EQ(info.base, region_addr);
  EXPECT_EQ(info.len, region_size);

  EXPECT_EQ(zx_handle_close(region), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can split a single mapping with an unmap call
TEST(Vmar, UnmapSplitTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t mapping_addr[3];

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(4 * zx_system_get_page_size(), 0, &vmo), ZX_OK);

  // Set up mappings to test on
  for (uintptr_t& addr : mapping_addr) {
    EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                          4 * zx_system_get_page_size(), &addr),
              ZX_OK);
  }

  // Unmap from the left
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 2 * zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1100, 4));
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0] + 2 * zx_system_get_page_size(),
                          2 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000, 4));

  // Unmap from the right
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1] + 2 * zx_system_get_page_size(),
                          2 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[1], 0b0011, 4));
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1], 2 * zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[1], 0b0000, 4));

  // Unmap from the center
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[2] + zx_system_get_page_size(),
                          2 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[2], 0b1001, 4));
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[2], zx_system_get_page_size()), ZX_OK);
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[2] + 3 * zx_system_get_page_size(),
                          zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[2], 0b0000, 4));

  zx_info_vmar_t info;
  ASSERT_EQ(zx_object_get_info(vmar, ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL), ZX_OK);

  // Make sure we can map over these again
  for (uintptr_t addr : mapping_addr) {
    const size_t offset = addr - info.base;
    EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, offset, vmo, 0,
                          4 * zx_system_get_page_size(), &addr),
              ZX_OK);
    EXPECT_TRUE(check_pages_mapped(process, addr, 0b1111, 4));
    EXPECT_EQ(zx_vmar_unmap(vmar, addr, 4 * zx_system_get_page_size()), ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can unmap multiple ranges simultaneously
TEST(Vmar, UnmapMultipleTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  zx_handle_t subregion;
  uintptr_t mapping_addr[3];
  uintptr_t subregion_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t mapping_size = 4 * zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(mapping_size, 0, &vmo), ZX_OK);

  // Create two mappings
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          i * mapping_size, vmo, 0, mapping_size, &mapping_addr[i]),
              ZX_OK);
  }
  EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[1]);
  // Unmap from the right of the first and the left of the second
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0] + 2 * zx_system_get_page_size(),
                          3 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0011, 8), "");
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 2 * zx_system_get_page_size()), ZX_OK, "");
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1] + 1 * zx_system_get_page_size(),
                          3 * zx_system_get_page_size()),
            ZX_OK, "");
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000, 8));

  // Create two mappings with a gap, and verify we can unmap them
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          2 * i * mapping_size, vmo, 0, mapping_size, &mapping_addr[i]),
              ZX_OK);
  }
  EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[1]);
  // Unmap all of the left one and some of the right one
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 2 * mapping_size + zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0000'0000, 12));
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1] + 1 * zx_system_get_page_size(),
                          3 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));

  // Create two mappings with a subregion between, should be able to unmap
  // them (and destroy the subregion in the process).
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          2 * i * mapping_size, vmo, 0, mapping_size, &mapping_addr[i]),
              ZX_OK);
  }
  ASSERT_EQ(
      zx_vmar_allocate(
          vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_SPECIFIC,
          mapping_size, mapping_size, &subregion, &subregion_addr),
      ZX_OK);
  ASSERT_EQ(zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        zx_system_get_page_size(), &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[1]);
  EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[2]);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0001'1111, 12));
  // Unmap all of the left one and some of the right one
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 2 * mapping_size + zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0000'0000, 12));
  // Try to map in the subregion again, should fail due to being destroyed
  ASSERT_EQ(
      zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(), &mapping_addr[2]),
      ZX_ERR_BAD_STATE);
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1] + 1 * zx_system_get_page_size(),
                          3 * zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));
  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  // Create two mappings with a subregion after.  Partial unmap of the
  // subregion should fail, full unmap should succeed.
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          i * mapping_size, vmo, 0, mapping_size, &mapping_addr[i]),
              ZX_OK);
  }
  ASSERT_EQ(
      zx_vmar_allocate(
          vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_SPECIFIC,
          2 * mapping_size, mapping_size, &subregion, &subregion_addr),
      ZX_OK);
  ASSERT_EQ(zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        zx_system_get_page_size(), &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[1]);
  EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[2]);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0001'1111'1111, 12));
  // Unmap some of the left one through to all but the last page of the subregion
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0] + zx_system_get_page_size(),
                          3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0001'1111'1111, 12));
  // Try again, but unmapping all of the subregion
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0] + zx_system_get_page_size(),
                          3 * mapping_size - zx_system_get_page_size()),
            ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0001, 12));
  // Try to map in the subregion again, should fail due to being destroyed
  ASSERT_EQ(
      zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(), &mapping_addr[2]),
      ZX_ERR_BAD_STATE);
  // Unmap the rest
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));
  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  // Create two mappings with a subregion before.  Partial unmap of the
  // subregion should fail, full unmap should succeed.
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          (i + 1) * mapping_size, vmo, 0, mapping_size, &mapping_addr[i]),
              ZX_OK);
  }
  ASSERT_EQ(
      zx_vmar_allocate(
          vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_SPECIFIC,
          0, mapping_size, &subregion, &subregion_addr),
      ZX_OK);
  ASSERT_EQ(zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        mapping_size - zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(),
                        &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(subregion_addr + mapping_size, mapping_addr[0]);
  EXPECT_EQ(subregion_addr + 2 * mapping_size, mapping_addr[1]);
  EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b1111'1111'1000, 12));
  // Try to unmap everything except the first page of the subregion
  EXPECT_EQ(zx_vmar_unmap(vmar, subregion_addr + zx_system_get_page_size(),
                          3 * mapping_size - zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b1111'1111'1000, 12));
  // Try again, but unmapping all of the subregion
  EXPECT_EQ(zx_vmar_unmap(vmar, subregion_addr, 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b0000'0000'0000, 12));
  // Try to map in the subregion again, should fail due to being destroyed
  ASSERT_EQ(
      zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                  zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(), &mapping_addr[2]),
      ZX_ERR_BAD_STATE);
  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can unmap multiple ranges simultaneously
TEST(Vmar, UnmapBaseNotMappedTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t mapping_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t mapping_size = 4 * zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(mapping_size, 0, &vmo), ZX_OK);

  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        zx_system_get_page_size(), vmo, 0, mapping_size, &mapping_addr),
            ZX_OK);
  ASSERT_EQ(zx_vmar_unmap(vmar, mapping_addr - zx_system_get_page_size(),
                          mapping_size + zx_system_get_page_size()),
            ZX_OK);

  // Try again, but this time with a mapping below where base is
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr),
            ZX_OK);
  for (size_t gap = zx_system_get_page_size(); gap < 3 * zx_system_get_page_size();
       gap += zx_system_get_page_size()) {
    ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                          mapping_size + gap, vmo, 0, mapping_size, &mapping_addr),
              ZX_OK);
    ASSERT_EQ(zx_vmar_unmap(vmar, mapping_addr - zx_system_get_page_size(),
                            mapping_size + zx_system_get_page_size()),
              ZX_OK);
  }

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can overwrite subranges and multiple ranges simultaneously
TEST(Vmar, MapSpecificOverwriteTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo, vmo2;
  zx_handle_t subregion;
  uintptr_t mapping_addr[2];
  uintptr_t subregion_addr;
  uint8_t buf[1];
  size_t len;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  const size_t mapping_size = 4 * zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(mapping_size * 2, 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_vmo_create(mapping_size * 2, 0, &vmo2), ZX_OK);

  // Tag each page of the VMOs so we can identify which mappings are from
  // which.
  for (size_t i = 0; i < mapping_size / zx_system_get_page_size(); ++i) {
    buf[0] = 1;
    ASSERT_EQ(zx_vmo_write(vmo, buf, i * zx_system_get_page_size(), 1), ZX_OK);
    buf[0] = 2;
    ASSERT_EQ(zx_vmo_write(vmo2, buf, i * zx_system_get_page_size(), 1), ZX_OK);
  }

  // Create a single mapping and overwrite it
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        zx_system_get_page_size(), vmo, 0, mapping_size, &mapping_addr[0]),
            ZX_OK);
  // Try over mapping with SPECIFIC but not SPECIFIC_OVERWRITE
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                        zx_system_get_page_size(), vmo2, 0, mapping_size, &mapping_addr[1]),
            ZX_ERR_ALREADY_EXISTS);
  // Try again with SPECIFIC_OVERWRITE
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
                        zx_system_get_page_size(), vmo2, 0, mapping_size, &mapping_addr[1]),
            ZX_OK);
  EXPECT_EQ(mapping_addr[0], mapping_addr[1]);
  for (size_t i = 0; i < mapping_size / zx_system_get_page_size(); ++i) {
    EXPECT_EQ(zx_process_read_memory(process, mapping_addr[0] + i * zx_system_get_page_size(), buf,
                                     1, &len),
              ZX_OK);
    EXPECT_EQ(buf[0], 2u);
  }

  // Overmap the middle of it
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
                        2 * zx_system_get_page_size(), vmo, 0, 2 * zx_system_get_page_size(),
                        &mapping_addr[0]),
            ZX_OK);
  EXPECT_EQ(mapping_addr[0], mapping_addr[1] + zx_system_get_page_size());
  for (size_t i = 0; i < mapping_size / zx_system_get_page_size(); ++i) {
    EXPECT_EQ(zx_process_read_memory(process, mapping_addr[1] + i * zx_system_get_page_size(), buf,
                                     1, &len),
              ZX_OK);
    EXPECT_EQ(buf[0], (i == 0 || i == 3) ? 2u : 1u);
  }

  // Create an adjacent sub-region, try to overmap it
  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             zx_system_get_page_size() + mapping_size, mapping_size, &subregion,
                             &subregion_addr),
            ZX_OK);
  EXPECT_EQ(subregion_addr, mapping_addr[1] + mapping_size);
  EXPECT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
                        zx_system_get_page_size(), vmo2, 0, 2 * mapping_size, &mapping_addr[0]),
            ZX_ERR_INVALID_ARGS);
  // Tear it all down
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[1], 2 * mapping_size), ZX_OK);

  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo2), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can split a single mapping with a protect call
TEST(Vmar, ProtectSplitTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo;
  uintptr_t mapping_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);

  ASSERT_EQ(zx_vmo_create(4 * zx_system_get_page_size(), 0, &vmo), ZX_OK);

  // Protect from the left
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr, 2 * zx_system_get_page_size()),
            ZX_OK);
  // TODO(teisenbe): Test to validate perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr, 4 * zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4));

  // Protect from the right
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr + 2 * zx_system_get_page_size(),
                            2 * zx_system_get_page_size()),
            ZX_OK);
  // TODO(teisenbe): Test to validate perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr, 4 * zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4));

  // Protect from the center
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        4 * zx_system_get_page_size(), &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr + zx_system_get_page_size(),
                            2 * zx_system_get_page_size()),
            ZX_OK);
  // TODO(teisenbe): Test to validate perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr, 4 * zx_system_get_page_size()), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4));

  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Validate that protect can be used across multiple mappings.  Make sure intersecting a subregion
// or gap fails
TEST(Vmar, ProtectMultipleTest) {
  zx_handle_t process;
  zx_handle_t vmar;
  zx_handle_t vmo, vmo2;
  zx_handle_t subregion;
  uintptr_t mapping_addr[3];
  uintptr_t subregion_addr;

  ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1, 0, &process,
                              &vmar),
            ZX_OK);
  const size_t mapping_size = 4 * zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(mapping_size, 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ, &vmo2), ZX_OK);

  // Protect from the right on the first mapping, all of the second mapping,
  // and from the left on the third mapping.
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[0]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[1]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 2 * mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr[0] + zx_system_get_page_size(),
                            3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_OK);
  // TODO(teisenbe): Test to validate perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));

  // Same thing, but map middle region with a VMO without the WRITE right
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[0]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_SPECIFIC, mapping_size, vmo2, 0, mapping_size,
                        &mapping_addr[1]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 2 * mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                            mapping_addr[0] + zx_system_get_page_size(),
                            3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_ERR_ACCESS_DENIED);
  // TODO(teisenbe): Test to validate no perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));

  // Try to protect across a gap
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[0]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 2 * mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr[0] + zx_system_get_page_size(),
                            3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_ERR_NOT_FOUND);
  // TODO(teisenbe): Test to validate no perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0000'1111, 12));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));

  // Try to protect across an empty subregion
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[0]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_allocate(vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
                             mapping_size, mapping_size, &subregion, &subregion_addr),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 2 * mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr[0] + zx_system_get_page_size(),
                            3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  // TODO(teisenbe): Test to validate no perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0000'1111, 12));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));
  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  // Try to protect across a subregion filled with mappings
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[0]),
            ZX_OK);
  ASSERT_EQ(
      zx_vmar_allocate(
          vmar, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC | ZX_VM_CAN_MAP_SPECIFIC,
          mapping_size, mapping_size, &subregion, &subregion_addr),
      ZX_OK);
  ASSERT_EQ(zx_vmar_map(subregion, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 0, vmo, 0,
                        mapping_size, &mapping_addr[1]),
            ZX_OK);
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, 2 * mapping_size,
                        vmo, 0, mapping_size, &mapping_addr[2]),
            ZX_OK);
  EXPECT_EQ(zx_vmar_protect(vmar, ZX_VM_PERM_READ, mapping_addr[0] + zx_system_get_page_size(),
                            3 * mapping_size - 2 * zx_system_get_page_size()),
            ZX_ERR_INVALID_ARGS);
  // TODO(teisenbe): Test to validate no perms changed, need to export more debug
  // info
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12));
  EXPECT_EQ(zx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), ZX_OK);
  EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12));
  EXPECT_EQ(zx_handle_close(subregion), ZX_OK);

  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo2), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(process), ZX_OK);
}

// Verify that we can change protections on a demand paged mapping successfully.
TEST(Vmar, ProtectOverDemandPagedTest) {
  zx_handle_t vmo;
  const size_t size = 100 * zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

  // TODO(teisenbe): Move this into a separate process; currently we don't
  // have an easy way to run a small test routine in another process.
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  std::atomic_uint8_t* target = reinterpret_cast<std::atomic_uint8_t*>(mapping_addr);
  target[0].store(5);
  target[size / 2].store(6);
  target[size - 1].store(7);

  ASSERT_EQ(zx_vmar_protect(zx_vmar_root_self(), ZX_VM_PERM_READ, mapping_addr, size), ZX_OK);

  // Attempt to write to the mapping again
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 4)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 2)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size - 1)),
               "mapping should no longer be writeable");

  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, size), ZX_OK);
}

// Verify that we can change protections on unmapped pages successfully.
TEST(Vmar, ProtectLargeUncomittedTest) {
  zx_handle_t vmo;
  // Create a 1GB VMO
  const size_t size = 1ull << 30;
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

  // TODO(teisenbe): Move this into a separate process; currently we don't
  // have an easy way to run a small test routine in another process.
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  // Make sure some pages exist
  std::atomic_uint8_t* target = reinterpret_cast<std::atomic_uint8_t*>(mapping_addr);
  target[0].store(5);
  target[size / 2].store(6);
  target[size - 1].store(7);

  // Ensure we're misaligned relative to a larger paging structure level.
  // TODO(teisenbe): Would be nice for this to be more arch aware.
  const uintptr_t base =
      ZX_ROUNDUP(mapping_addr, 512 * zx_system_get_page_size()) + zx_system_get_page_size();
  const size_t protect_size = mapping_addr + size - base;
  ASSERT_EQ(zx_vmar_protect(zx_vmar_root_self(), ZX_VM_PERM_READ, base, protect_size), ZX_OK);

  // Attempt to write to the mapping again
  EXPECT_TRUE(probe_for_write(reinterpret_cast<void*>(mapping_addr)),
              "mapping should still be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 4)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 2)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size - 1)),
               "mapping should no longer be writeable");

  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, size), ZX_OK);
}

// Verify vmar_op_range() commit/decommit of mapped VMO pages.
TEST(Vmar, RangeOpCommitVmoPages) {
  // Create a VMO to map parts of into a VMAR.
  const size_t kVmoSize = zx_system_get_page_size() * 5;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_vmo_create(kVmoSize, 0, &vmo), ZX_OK);

  // Create a VMAR to guarantee some pages remain unmapped.
  zx_vaddr_t vmar_base = 0u;
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_vmar_allocate(zx_vmar_root_self(),
                             ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             kVmoSize, &vmar, &vmar_base),
            ZX_OK);

  zx_vaddr_t mapping_addr = 0u;
  // Map one writable page to the VMO.
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size() * 2, &mapping_addr),
            ZX_OK);
  ASSERT_EQ(vmar_base, mapping_addr);

  // Map second page to a different part of the VMO.
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                        zx_system_get_page_size() * 2, vmo, zx_system_get_page_size() * 3,
                        zx_system_get_page_size(), &mapping_addr),
            ZX_OK);

  // Map fourth page read-only.
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ, zx_system_get_page_size() * 4, vmo,
                        zx_system_get_page_size(), zx_system_get_page_size(), &mapping_addr),
            ZX_OK);

  // Verify decommit of only part of a mapping.
  std::atomic_uint8_t* target = reinterpret_cast<std::atomic_uint8_t*>(vmar_base);
  target[0].store(5);
  EXPECT_EQ(
      zx_vmar_op_range(vmar, ZX_VMO_OP_DECOMMIT, vmar_base, zx_system_get_page_size(), nullptr, 0u),
      ZX_OK);
  EXPECT_EQ(target[0].load(), 0u);
  target[zx_system_get_page_size()].store(7);
  EXPECT_EQ(zx_vmar_op_range(vmar, ZX_VMO_OP_DECOMMIT, vmar_base + zx_system_get_page_size(),
                             zx_system_get_page_size(), nullptr, 0u),
            ZX_OK);
  EXPECT_EQ(target[zx_system_get_page_size()].load(), 0u);

  // Verify decommit across two adjacent mappings.
  target[zx_system_get_page_size()].store(5);
  target[zx_system_get_page_size() * 2].store(6);
  EXPECT_EQ(target[zx_system_get_page_size() * 4].load(), 5u);
  EXPECT_EQ(zx_vmar_op_range(vmar, ZX_VMO_OP_DECOMMIT, vmar_base + zx_system_get_page_size(),
                             zx_system_get_page_size() * 2, nullptr, 0u),
            ZX_OK);
  EXPECT_EQ(target[zx_system_get_page_size()].load(), 0u);
  EXPECT_EQ(target[zx_system_get_page_size() * 2].load(), 0u);
  EXPECT_EQ(target[zx_system_get_page_size() * 4].load(), 0u);

  // Verify decommit including an unmapped region fails.
  EXPECT_EQ(zx_vmar_op_range(vmar, ZX_VMO_OP_DECOMMIT, vmar_base + zx_system_get_page_size(),
                             zx_system_get_page_size() * 3, nullptr, 0u),
            ZX_ERR_BAD_STATE);

  // Decommit of a non-writable mapping succeeds if the mapping can be made
  // writable by the caller, i.e. it was created with a writable VMO handle.
  EXPECT_EQ(zx_vmar_op_range(vmar, ZX_VMO_OP_DECOMMIT, vmar_base + (zx_system_get_page_size() * 4),
                             zx_system_get_page_size(), nullptr, 0u),
            ZX_OK);

  // Decommit of a non-writable mapping fails if the caller cannot make the
  // mapping writable, i.e. it was created from a read-only VMO handle.
  zx_handle_t readonly_vmo = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ, &readonly_vmo), ZX_OK);
  zx_vaddr_t readonly_mapping_addr = 0u;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, readonly_vmo, 0,
                        zx_system_get_page_size(), &readonly_mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_vmar_op_range(zx_vmar_root_self(), ZX_VMO_OP_DECOMMIT, readonly_mapping_addr,
                             zx_system_get_page_size(), nullptr, 0u),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), readonly_mapping_addr, zx_system_get_page_size()),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(readonly_vmo), ZX_OK);

  // Clean up the test VMAR and VMO.
  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), vmar_base, kVmoSize), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
}

// Verify vmar_range_op map range of committed mapped VMO pages.
TEST(Vmar, RangeOpMapRange) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  const size_t vmo_size = zx_system_get_page_size() * 4;

  ASSERT_EQ(zx_vmo_create(vmo_size, 0, &vmo), ZX_OK);

  zx_handle_t vmar = ZX_HANDLE_INVALID;
  zx_vaddr_t vmar_base = 0;

  ASSERT_EQ(zx_vmar_allocate(zx_vmar_root_self(), ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             vmo_size, &vmar, &vmar_base),
            ZX_OK);

  zx_vaddr_t map_base = 0;

  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, vmo_size, &map_base),
            ZX_OK);

  // Verify the ZX_VMAR_OP_MAP_RANGE op with zx_vmar_op_range.

  // Attempting to map range uncommitted pages should succeed.
  ASSERT_EQ(zx_vmar_op_range(vmar, ZX_VMAR_OP_MAP_RANGE, map_base, vmo_size, nullptr, 0), ZX_OK);

  // Commit the first page in the VMO.
  ASSERT_EQ(zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, zx_system_get_page_size(), nullptr, 0),
            ZX_OK);

  // Attempting to map range partially committed contiguous pages should succeed.
  ASSERT_EQ(zx_vmar_op_range(vmar, ZX_VMAR_OP_MAP_RANGE, map_base, vmo_size, nullptr, 0), ZX_OK);

  // Commit the second and last page in the VMO, leaving a discontiguous hole.
  ASSERT_EQ(zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, zx_system_get_page_size(),
                            zx_system_get_page_size(), nullptr, 0),
            ZX_OK);
  ASSERT_EQ(zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, zx_system_get_page_size() * 3,
                            zx_system_get_page_size(), nullptr, 0),
            ZX_OK);

  // Attempting to map range partially committed dicontiguous pages should succeed.
  ASSERT_EQ(zx_vmar_op_range(vmar, ZX_VMAR_OP_MAP_RANGE, map_base + zx_system_get_page_size(),
                             vmo_size - zx_system_get_page_size(), nullptr, 0),
            ZX_OK);

  // Commit all of the pages in the VMO.
  ASSERT_EQ(zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, vmo_size, nullptr, 0), ZX_OK);

  // Attempting to map range the hole should succeed.
  ASSERT_EQ(zx_vmar_op_range(vmar, ZX_VMAR_OP_MAP_RANGE, vmar_base + zx_system_get_page_size() * 2,
                             zx_system_get_page_size(), nullptr, 0),
            ZX_OK);

  EXPECT_EQ(zx_vmar_unmap(vmar, map_base, vmo_size), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmar), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
}

// Attempt to unmap a large mostly uncommitted VMO
TEST(Vmar, UnmapLargeUncommittedTest) {
  zx_handle_t vmo;
  // Create a 1GB VMO
  const size_t size = 1ull << 30;
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

  // TODO(teisenbe): Move this into a separate process; currently we don't
  // have an easy way to run a small test routine in another process.
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  // Make sure some pages exist
  std::atomic_uint8_t* target = reinterpret_cast<std::atomic_uint8_t*>(mapping_addr);
  target[0].store(5);
  target[size / 2].store(6);
  target[size - 1].store(7);

  // Ensure we're misaligned relative to a larger paging structure level.
  // TODO(teisenbe): Would be nice for this to be more arch aware.
  const uintptr_t base =
      ZX_ROUNDUP(mapping_addr, 512 * zx_system_get_page_size()) + zx_system_get_page_size();
  const size_t unmap_size = mapping_addr + size - base;
  ASSERT_EQ(zx_vmar_unmap(zx_vmar_root_self(), base, unmap_size), ZX_OK);

  // Attempt to write to the mapping again
  EXPECT_TRUE(probe_for_write(reinterpret_cast<void*>(mapping_addr)),
              "mapping should still be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 4)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size / 2)),
               "mapping should no longer be writeable");
  EXPECT_FALSE(probe_for_write(reinterpret_cast<void*>(mapping_addr + size - 1)),
               "mapping should no longer be writeable");

  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, size), ZX_OK);
}

TEST(Vmar, PartialUnmapAndRead) {
  // Map a two-page VMO.
  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size() * 2, 0, &vmo), ZX_OK);
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size() * 2, &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  char* ptr = (char*)mapping_addr;
  memset(ptr, 0, zx_system_get_page_size() * 2);

  // Unmap the second page.
  zx_vmar_unmap(zx_vmar_root_self(), mapping_addr + zx_system_get_page_size(),
                zx_system_get_page_size());

  char buffer[zx_system_get_page_size() * 2];
  size_t actual_read;

  // First page succeeds.
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr, buffer,
                                   zx_system_get_page_size(), &actual_read),
            ZX_OK);
  EXPECT_EQ(actual_read, zx_system_get_page_size());

  // Second page fails.
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr + zx_system_get_page_size(),
                                   buffer, zx_system_get_page_size(), &actual_read),
            ZX_ERR_NO_MEMORY);

  // Reading the whole region succeeds, but only reads the first page.
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr, buffer,
                                   zx_system_get_page_size() * 2, &actual_read),
            ZX_OK);
  EXPECT_EQ(actual_read, zx_system_get_page_size());

  // Read at the boundary straddling the pages.
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr + zx_system_get_page_size() - 1,
                                   buffer, 2, &actual_read),
            ZX_OK);
  EXPECT_EQ(actual_read, 1);

  // Unmap the left over first page.
  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, zx_system_get_page_size()), ZX_OK);
}

TEST(Vmar, PartialUnmapAndWrite) {
  // Map a two-page VMO.
  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size() * 2, 0, &vmo), ZX_OK);
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        zx_system_get_page_size() * 2, &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  char* ptr = (char*)mapping_addr;
  memset(ptr, 0, zx_system_get_page_size() * 2);

  // Unmap the second page.
  zx_vmar_unmap(zx_vmar_root_self(), mapping_addr + zx_system_get_page_size(),
                zx_system_get_page_size());

  char buffer[zx_system_get_page_size() * 2];
  size_t actual_written;
  memset(buffer, 0, zx_system_get_page_size() * 2);

  // First page succeeds.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr, buffer,
                                    zx_system_get_page_size(), &actual_written),
            ZX_OK);
  EXPECT_EQ(actual_written, zx_system_get_page_size());

  // Second page fails.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr + zx_system_get_page_size(),
                                    buffer, zx_system_get_page_size(), &actual_written),
            ZX_ERR_NO_MEMORY);

  // Writing to the whole region succeeds, but only writes the first page.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr, buffer,
                                    zx_system_get_page_size() * 2, &actual_written),
            ZX_OK);
  EXPECT_EQ(actual_written, zx_system_get_page_size());

  // Write at the boundary straddling the pages.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr + zx_system_get_page_size() - 1,
                                    buffer, 2, &actual_written),
            ZX_OK);
  EXPECT_EQ(actual_written, 1);

  // Unmap the left over first page.
  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, zx_system_get_page_size()), ZX_OK);
}

TEST(Vmar, PartialUnmapWithVmarOffset) {
  constexpr size_t kOffset = 0x1000;
  const size_t kVmoSize = zx_system_get_page_size() * 10;
  // Map a VMO, using an offset into the VMO.
  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(kVmoSize, 0, &vmo), ZX_OK);
  uintptr_t mapping_addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, kOffset,
                        kVmoSize - kOffset, &mapping_addr),
            ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  char* ptr = (char*)mapping_addr;
  memset(ptr, 0, kVmoSize - kOffset);

  // Make sure both reads and writes to both the beginning and the end are allowed.
  char buffer[kVmoSize - kOffset];
  size_t actual;
  EXPECT_EQ(
      zx_process_write_memory(zx_process_self(), mapping_addr, buffer, kVmoSize - kOffset, &actual),
      ZX_OK);
  EXPECT_EQ(actual, kVmoSize - kOffset);

  EXPECT_EQ(
      zx_process_read_memory(zx_process_self(), mapping_addr, buffer, kVmoSize - kOffset, &actual),
      ZX_OK);
  EXPECT_EQ(actual, kVmoSize - kOffset);

  // That reads and writes right at the end are OK.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset - 1,
                                    buffer, 1, &actual),
            ZX_OK);
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset - 1, buffer,
                                   1, &actual),
            ZX_OK);

  // That reads and writes one past the end fail.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset, buffer, 1,
                                    &actual),
            ZX_ERR_NO_MEMORY);
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset, buffer, 1,
                                   &actual),
            ZX_ERR_NO_MEMORY);

  // And crossing the boundary works as expected.
  EXPECT_EQ(zx_process_write_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset - 1,
                                    buffer, 2, &actual),
            ZX_OK);
  EXPECT_EQ(actual, 1);
  EXPECT_EQ(zx_process_read_memory(zx_process_self(), mapping_addr + kVmoSize - kOffset - 1, buffer,
                                   2, &actual),
            ZX_OK);
  EXPECT_EQ(actual, 1);
}

TEST(Vmar, AllowFaultsTest) {
  // No-op test that checks the current default behavior.
  // TODO(stevensd): Add meaningful tests once the flag is actually implemented.
  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(zx_system_get_page_size(), ZX_VMO_RESIZABLE, &vmo), ZX_OK);
  uintptr_t mapping_addr;
  ASSERT_EQ(
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_ALLOW_FAULTS, 0,
                  vmo, 0, zx_system_get_page_size(), &mapping_addr),
      ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  EXPECT_EQ(zx_vmar_unmap(zx_vmar_root_self(), mapping_addr, zx_system_get_page_size()), ZX_OK);
}

// Regression test for a scenario where process_read_memory could use a stale RefPtr<VmObject>
// This will not always detect the failure scenario, but will never false positive.
TEST(Vmar, ConcurrentUnmapReadMemory) {
  auto root_vmar = zx::vmar::root_self();

  zx::vmar child_vmar;
  uintptr_t addr;
  ASSERT_EQ(root_vmar->allocate(ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ, 0,
                                zx_system_get_page_size() * 4, &child_vmar, &addr),
            ZX_OK);

  std::atomic<bool> running = true;
  std::thread t = std::thread([addr, &running] {
    auto self = zx::process::self();
    while (running) {
      uint64_t data;
      size_t temp;
      self->read_memory(addr, &data, sizeof(data), &temp);
    }
  });

  // Iterate some number of times to attempt to hit the race condition. This is a best effort and
  // even when the bug is present it could take minutes of running to trigger it.
  for (int i = 0; i < 1000; i++) {
    uintptr_t temp;
    // vmo must be created in the loop so that it is destroyed each iteration leading to there
    // being no references to the underlying VmObject in the kernel.
    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(zx_system_get_page_size(), 0, &vmo), ZX_OK);
    ASSERT_EQ(child_vmar.map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ, 0, vmo, 0, zx_system_get_page_size(),
                             &temp),
              ZX_OK);
    ASSERT_EQ(child_vmar.unmap(addr, zx_system_get_page_size()), ZX_OK);
  }

  running = false;
  t.join();
}

// Test DECOMMIT on a vmar with two non-contiguous mappings (fxbug.dev/68272)
TEST(Vmar, RangeOpCommitVmoPages2) {
  auto root_vmar = zx::vmar::root_self();

  // Create a VMO and VMAR large enough to support two multipage mappings.
  const size_t kVmoSize = zx_system_get_page_size() * 10;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_vmo_create(kVmoSize, 0, &vmo), ZX_OK);

  zx_vaddr_t vmar_base = 0u;
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  ASSERT_EQ(zx_vmar_allocate(zx_vmar_root_self(),
                             ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             kVmoSize, &vmar, &vmar_base),
            ZX_OK);

  // Create one mapping in the VMAR
  const size_t kMappingSize = 5 * zx_system_get_page_size();
  zx_vaddr_t mapping_addr = 0u;
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        kMappingSize, &mapping_addr),
            ZX_OK);
  ASSERT_EQ(vmar_base, mapping_addr);

  // Create a second mapping in the VMAR, with one unmapped page separting this from the prior
  // mapping
  const size_t kMappingSize2 = 4 * zx_system_get_page_size();
  zx_vaddr_t mapping_addr2 = 0u;
  ASSERT_EQ(zx_vmar_map(vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                        kMappingSize + zx_system_get_page_size(), vmo,
                        zx_system_get_page_size() * 5, kMappingSize2, &mapping_addr2),
            ZX_OK);
  ASSERT_NE(mapping_addr, mapping_addr2);

  memset(reinterpret_cast<void*>(mapping_addr), 0x0, kMappingSize2);
  memset(reinterpret_cast<void*>(mapping_addr2), 0x0, kMappingSize2);

  // Decommit the second mapping; the presence of the first mapping should not cause the decommit op
  // to panic or to be invoked on the wrong range.
  EXPECT_EQ(zx_vmar_op_range(vmar, ZX_VMAR_OP_DECOMMIT, mapping_addr2, kMappingSize2, nullptr, 0u),
            ZX_OK);
}

// Test that commits and decommits are not allowed through a nested vmar.
TEST(Vmar, BadRangeOpNestedVmar) {
  auto root_vmar = zx::vmar::root_self();

  // Create an intermediate vmar.

  zx::vmar intermediate_vmar;
  uintptr_t addr;
  ASSERT_EQ(root_vmar->allocate(ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
                                0, zx_system_get_page_size() * 8, &intermediate_vmar, &addr),
            ZX_OK);

  // Place mapping in the intermediate vmar.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));

  uint64_t mapping_addr;
  ASSERT_OK(intermediate_vmar.map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                  zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(),
                                  &mapping_addr));

  // Commit and decommit ops should not be allowed on the root vmar for this range.
  EXPECT_EQ(
      root_vmar->op_range(ZX_VMAR_OP_COMMIT, mapping_addr, zx_system_get_page_size(), nullptr, 0),
      ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(
      root_vmar->op_range(ZX_VMAR_OP_DECOMMIT, mapping_addr, zx_system_get_page_size(), nullptr, 0),
      ZX_ERR_INVALID_ARGS);
}

// Test zx_vmar_op_range ZX_VMAR_OP_COMMIT.
TEST(Vmar, RangeOpCommit) {
  // Create a temporary VMAR to work with.
  auto root_vmar = zx::vmar::root_self();
  zx::vmar vmar;
  zx_vaddr_t base_addr;
  const uint64_t kVmarSize = 20 * zx_system_get_page_size();
  ASSERT_OK(root_vmar->allocate(ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
                                0, kVmarSize, &vmar, &base_addr));

  // Create two sub-VMARs to hold the mappings.
  zx::vmar sub_vmar1, sub_vmar2;
  zx_vaddr_t base_addr1, base_addr2;
  const uint64_t kSubVmarSize = 8 * zx_system_get_page_size();
  ASSERT_OK(vmar.allocate(
      ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
      zx_system_get_page_size(), kSubVmarSize, &sub_vmar1, &base_addr1));
  ASSERT_EQ(base_addr1, base_addr + zx_system_get_page_size());
  ASSERT_OK(vmar.allocate(
      ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC,
      kSubVmarSize + 2 * zx_system_get_page_size(), kSubVmarSize, &sub_vmar2, &base_addr2));
  ASSERT_EQ(base_addr2, base_addr1 + kSubVmarSize + zx_system_get_page_size());

  // Create a VMO and clone it.
  zx::vmo vmo, clone;
  const uint64_t kVmoSize = 5 * zx_system_get_page_size();
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, kVmoSize, &clone));

  // Map the VMO and its clone.
  zx_vaddr_t addr1, addr2;
  ASSERT_OK(sub_vmar1.map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, kVmoSize,
                          &addr1));
  ASSERT_EQ(base_addr1, addr1);
  ASSERT_OK(sub_vmar2.map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, clone, 0,
                          kVmoSize, &addr2));
  ASSERT_EQ(base_addr2, addr2);

  // Commit pages 1 and 2 in the parent.
  ASSERT_OK(sub_vmar1.op_range(ZX_VMAR_OP_COMMIT, addr1 + zx_system_get_page_size(),
                               2 * zx_system_get_page_size(), nullptr, 0));
  // Commit pages 2 and 3 in the clone.
  ASSERT_OK(sub_vmar2.op_range(ZX_VMAR_OP_COMMIT, addr2 + 2 * zx_system_get_page_size(),
                               2 * zx_system_get_page_size(), nullptr, 0));

  // Both VMOs should now have 2 pages committed. We can query committed counts despite these pages
  // being zero because explicitly committed pages are not deduped by the zero scanner.
  zx_info_vmo_t info;
  ASSERT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(2 * zx_system_get_page_size(), info.committed_bytes);
  ASSERT_OK(clone.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(2 * zx_system_get_page_size(), info.committed_bytes);

  // Commit all pages in the clone.
  ASSERT_OK(sub_vmar2.op_range(ZX_VMAR_OP_COMMIT, addr2, kVmoSize, nullptr, 0));

  // The clone should have all pages committed, but the parent should still have only 2.
  ASSERT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(2 * zx_system_get_page_size(), info.committed_bytes);
  ASSERT_OK(clone.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(kVmoSize, info.committed_bytes);

  // Map a single page as read-only and try to commit it. The commit should fail.
  zx::vmo readonly_vmo;
  ASSERT_EQ(vmo.duplicate(ZX_RIGHT_MAP | ZX_RIGHT_READ, &readonly_vmo), ZX_OK);
  zx_vaddr_t addr;
  ASSERT_OK(vmar.map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ, kVmarSize - zx_system_get_page_size(),
                     readonly_vmo, 0, zx_system_get_page_size(), &addr));
  ASSERT_EQ(base_addr + kVmarSize - zx_system_get_page_size(), addr);
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
            vmar.op_range(ZX_VMAR_OP_COMMIT, addr, zx_system_get_page_size(), nullptr, 0));

  // The commit counts should not have changed.
  ASSERT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(2 * zx_system_get_page_size(), info.committed_bytes);
  ASSERT_OK(clone.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(kVmoSize, info.committed_bytes);

  // Some trivial failure cases.
  // Out of range.
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            vmar.op_range(ZX_VMAR_OP_COMMIT, base_addr, 2 * kVmarSize, nullptr, 0));
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, vmar.op_range(ZX_VMAR_OP_COMMIT, 0, kVmarSize, nullptr, 0));
  // Various combinations of gaps.
  ASSERT_EQ(ZX_ERR_BAD_STATE, vmar.op_range(ZX_VMAR_OP_COMMIT, base_addr, kVmarSize, nullptr, 0));
  ASSERT_EQ(ZX_ERR_BAD_STATE, vmar.op_range(ZX_VMAR_OP_COMMIT, base_addr,
                                            base_addr1 + kSubVmarSize - base_addr, nullptr, 0));
}

TEST(Vmar, ProtectCowWritable) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &vmo));

  uint64_t val = 42;
  EXPECT_OK(vmo.write(&val, 0, sizeof(uint64_t)));

  zx::vmo clone;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &clone));

  // Map the clone in read/write.
  zx_vaddr_t addr;
  ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, clone, 0,
                                       zx_system_get_page_size() * 2, &addr));

  // Protect it read-only.
  EXPECT_OK(zx::vmar::root_self()->protect(ZX_VM_PERM_READ, addr, zx_system_get_page_size() * 2));

  // Perform some reads to ensure there are mappings.
  uint64_t val2 = *reinterpret_cast<uint64_t*>(addr);
  EXPECT_OK(clone.read(&val, 0, sizeof(uint64_t)));
  EXPECT_EQ(val2, val);

  // Now protect the first page back to write.
  EXPECT_OK(zx::vmar::root_self()->protect(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, addr,
                                           zx_system_get_page_size()));

  // Write to the page.
  *reinterpret_cast<volatile uint64_t*>(addr) = 77;

  // Original vmo should be unchanged.
  EXPECT_OK(vmo.read(&val, 0, sizeof(uint64_t)));
  EXPECT_EQ(42, val);

  // Clone should have been modified.
  EXPECT_OK(clone.read(&val, 0, sizeof(uint64_t)));
  EXPECT_EQ(77, val);
}

TEST(Vmar, MapReadIfXomUnsupported) {
  zx_handle_t vmo;
  size_t size = zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

  uintptr_t addr;
  ASSERT_EQ(
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ_IF_XOM_UNSUPPORTED, 0, vmo, 0, size, &addr),
      ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  uint32_t features = 0;
  ASSERT_EQ(zx_system_get_features(ZX_FEATURE_KIND_VM, &features), ZX_OK);
  bool xomUnsupported = !(features & ZX_VM_FEATURE_CAN_MAP_XOM);

  EXPECT_EQ(probe_for_read(reinterpret_cast<void*>(addr)), xomUnsupported);
}

TEST(Vmar, ProtectReadIfXomUnsupported) {
  zx_handle_t vmo;
  size_t size = zx_system_get_page_size();
  ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

  uintptr_t addr;
  ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &addr), ZX_OK);
  EXPECT_EQ(zx_handle_close(vmo), ZX_OK);

  ASSERT_TRUE(probe_for_read(reinterpret_cast<void*>(addr)));

  ASSERT_EQ(zx_vmar_protect(zx_vmar_root_self(), ZX_VM_PERM_READ_IF_XOM_UNSUPPORTED, addr, size),
            ZX_OK);

  uint32_t features = 0;
  ASSERT_EQ(zx_system_get_features(ZX_FEATURE_KIND_VM, &features), ZX_OK);
  bool xomUnsupported = !(features & ZX_VM_FEATURE_CAN_MAP_XOM);

  EXPECT_EQ(probe_for_read(reinterpret_cast<void*>(addr)), xomUnsupported);
}

}  // namespace
