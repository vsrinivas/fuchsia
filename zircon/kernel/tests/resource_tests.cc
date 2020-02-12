// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <ktl/unique_ptr.h>
#include <object/handle.h>
#include <object/resource_dispatcher.h>

#include "tests.h"

static bool unconfigured() {
  BEGIN_TEST;
  ResourceDispatcher::ResourceStorage storage;
  zx_rights_t rights;

  KernelHandle<ResourceDispatcher> handle1, handle2;
  EXPECT_EQ(ResourceDispatcher::Create(&handle1, &rights, ZX_RSRC_KIND_MMIO, 0, PAGE_SIZE, 0,
                                       nullptr, &storage),
            ZX_ERR_BAD_STATE, "MMIO GetRegion should return ERR_BAD_STATE");
  EXPECT_EQ(ResourceDispatcher::Create(&handle2, &rights, ZX_RSRC_KIND_IRQ, 0, PAGE_SIZE, 0,
                                       nullptr, &storage),
            ZX_ERR_BAD_STATE, "IRQ GetRegion should return ERR_BAD_STATE");
  // Nothing should be in the lists.
  ASSERT_EQ(storage.resource_list.size_slow(), 0u);
  END_TEST;
}

static bool allocators_configured() {
  BEGIN_TEST;

  ResourceDispatcher::ResourceStorage storage;
  // Allocate/Populate the region allocators
  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT64_MAX - 1, &storage),
            ZX_OK, "Failed first MMIO initialization");

  // Ensure that a double initialization is a bad state
  EXPECT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT32_MAX - 1, &storage),
            ZX_ERR_BAD_STATE, "Wrong value trying to double initialize MMIO allocator");

  // IRQ should initialize fine.
  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IRQ, 0, 256, &storage), ZX_OK,
            "Failed to initialize IRQ allocator");

  END_TEST;
}

// Test that shared and exclusive regions do
static bool exclusive_then_shared() {
  BEGIN_TEST;
  ResourceDispatcher::ResourceStorage storage;
  KernelHandle<ResourceDispatcher> handle1, handle2;
  zx_rights_t rights;
  uint64_t base = 0;
  uint64_t size = PAGE_SIZE;
  uint32_t flags = ZX_RSRC_FLAG_EXCLUSIVE;
  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT32_MAX - 1, &storage),
            ZX_OK);
  // Creating the exclusive resource will succeed.
  EXPECT_EQ(ResourceDispatcher::Create(&handle1, &rights, ZX_RSRC_KIND_MMIO, base, size, flags,
                                       "ets-disp1", &storage),
            ZX_OK, "Creating the exclusive resource failed.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);
  // Creating the shared resource should fail
  flags = 0;
  EXPECT_EQ(ResourceDispatcher::Create(&handle2, &rights, ZX_RSRC_KIND_MMIO, base, size, flags,
                                       "ets-disp2", &storage),
            ZX_ERR_NOT_FOUND, "Creating the shared resource succeeded.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);

  END_TEST;
}

static bool shared_then_exclusive() {
  BEGIN_TEST;

  ResourceDispatcher::ResourceStorage storage;
  KernelHandle<ResourceDispatcher> handle1, handle2;
  zx_rights_t rights;
  uint64_t base = 0;
  uint64_t size = PAGE_SIZE;
  uint32_t flags = 0;
  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT32_MAX - 1, &storage),
            ZX_OK);
  // Creating the shared resource will succeed.
  EXPECT_EQ(ResourceDispatcher::Create(&handle1, &rights, ZX_RSRC_KIND_MMIO, base, size, flags,
                                       "ets-disp1", &storage),
            ZX_OK, "Creating the exclusive resource failed.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);
  // Creating the exclusive resource should fail
  flags = ZX_RSRC_FLAG_EXCLUSIVE;
  EXPECT_EQ(ResourceDispatcher::Create(&handle2, &rights, ZX_RSRC_KIND_MMIO, base, size, flags,
                                       "ets-disp2", &storage),
            ZX_ERR_NOT_FOUND, "Creating the shared resource succeeded.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);

  END_TEST;
}

static bool out_of_allocator_range() {
  BEGIN_TEST;

  ResourceDispatcher::ResourceStorage storage;
  KernelHandle<ResourceDispatcher> handle1;
  zx_rights_t rights;
  uint64_t size = 0xFFFF;

  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, size, &storage), ZX_OK);
  // Overlap near the end
  EXPECT_EQ(ResourceDispatcher::Create(&handle1, &rights, ZX_RSRC_KIND_MMIO, size - 0xFF, 0xFFF, 0,
                                       "ooar-disp1", &storage),
            ZX_ERR_NOT_FOUND);

  // Pick a chunk outside the range entirely
  EXPECT_EQ(ResourceDispatcher::Create(&handle1, &rights, ZX_RSRC_KIND_MMIO, size + size, size, 0,
                                       "ooar-disp1", &storage),
            ZX_ERR_NOT_FOUND);

  END_TEST;
}

UNITTEST_START_TESTCASE(resources)
UNITTEST("test unconfigured allocators", unconfigured)
UNITTEST("test setting up allocators", allocators_configured)
UNITTEST("test exclusive then shared overlap", exclusive_then_shared)
UNITTEST("test shared then exclusive overlap", shared_then_exclusive)
UNITTEST("test allocating out of range", out_of_allocator_range)
UNITTEST_END_TESTCASE(resources, "resource", "Tests for Resource bookkeeping")
