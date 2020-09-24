// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/root_resource_filter_internal.h>
#include <lib/unittest/unittest.h>

#include <fbl/alloc_checker.h>
#include <ktl/limits.h>
#include <ktl/unique_ptr.h>
#include <object/handle.h>
#include <object/resource.h>
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
                                       "ste-disp1", &storage),
            ZX_OK, "Creating the exclusive resource failed.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);
  // Creating the exclusive resource should fail
  flags = ZX_RSRC_FLAG_EXCLUSIVE;
  EXPECT_EQ(ResourceDispatcher::Create(&handle2, &rights, ZX_RSRC_KIND_MMIO, base, size, flags,
                                       "ste-disp2", &storage),
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

static bool root_resource_filter() {
  BEGIN_TEST;
  // Instantiate our own filter so we can test it in isolation from the
  // global singleton filter used by validate_ranged_resource.  It will start
  // life approving any request for resources.
  RootResourceFilter filter;

  // Start with asking for access to all of the various resource range kinds.
  // None of these requests should be denied, not even the ones which have no
  // meaningful concept of "range" associated with this.  Unless explicitly
  // disallowed, all requests should default to OK.
  ktl::array kResourceKinds = {ZX_RSRC_KIND_MMIO,       ZX_RSRC_KIND_IRQ,  ZX_RSRC_KIND_IOPORT,
                               ZX_RSRC_KIND_HYPERVISOR, ZX_RSRC_KIND_ROOT, ZX_RSRC_KIND_VMEX,
                               ZX_RSRC_KIND_SMC};

  // make sure that if someone adds new resource type, that someone comes back
  // here and adds it to this test.
  static_assert(kResourceKinds.size() == ZX_RSRC_KIND_COUNT,
                "The set of resource kinds has changed and this test needs to be updated.");

  for (const auto kind : kResourceKinds) {
    EXPECT_TRUE(filter.IsRegionAllowed(0, 1, kind));
  }

  // Now manually add some ranges to the set of ranges to be denied.  Test both
  // before and after to make sure that the ranges are allowed before they have
  // been added to the filter, and are properly denied afterwards.
  constexpr size_t kRangeSize = 128;
  struct TestVector {
    uintptr_t base;
    size_t size;
    zx_rsrc_kind_t kind;
  };
  std::array kTestVectors = {TestVector{0x0AFF000000003400, kRangeSize, ZX_RSRC_KIND_MMIO},
                             TestVector{0x0AFF000000007abd, kRangeSize, ZX_RSRC_KIND_MMIO},
                             TestVector{0x0AFF000000004000, kRangeSize, ZX_RSRC_KIND_MMIO},
                             TestVector{0x0040, kRangeSize, ZX_RSRC_KIND_IOPORT},
                             TestVector{0x01c0, kRangeSize, ZX_RSRC_KIND_IOPORT},
                             TestVector{0x70ef, kRangeSize, ZX_RSRC_KIND_IOPORT}};

  for (uint32_t pass = 0; pass < 2; ++pass) {
    constexpr size_t kTestSize = 16;
    static_assert(
        (kTestSize * 2) < kRangeSize,
        "test range size must be at least twice as small as the test vector deny-range size.");

    for (const auto& v : kTestVectors) {
      // Entirely before and entirely after ranges should always pass.
      EXPECT_TRUE(filter.IsRegionAllowed(v.base - kTestSize, kTestSize / 2, v.kind));
      EXPECT_TRUE(filter.IsRegionAllowed(v.base + kRangeSize, kTestSize / 2, v.kind));

      // Now check ranges which overlap the start, overlap the end, and are
      // entirely contained within the deny ranges.  These should succeed on the
      // first pass, but fail on the second (after we have added the deny-ranges
      // to the filter), or if the kind of range is IOPORT (currently the
      // deny list does not yet apply to the IOPORT domain).
      bool expected = (pass == 0) || (v.kind == ZX_RSRC_KIND_IOPORT);
      EXPECT_EQ(expected, filter.IsRegionAllowed(v.base - (kTestSize / 2), kTestSize, v.kind));
      EXPECT_EQ(expected, filter.IsRegionAllowed(v.base + kTestSize, kTestSize, v.kind));
      EXPECT_EQ(expected,
                filter.IsRegionAllowed(v.base + kRangeSize - (kTestSize / 2), kTestSize, v.kind));
    }

    // If this was the first pass, add in our deny ranges.
    if (pass == 0) {
      for (const auto& v : kTestVectors) {
        filter.AddDenyRegion(v.base, v.size, v.kind);
      }
    }
  }

  END_TEST;
}

static bool create_root_ranged() {
  BEGIN_TEST;

  ResourceDispatcher::ResourceStorage storage;
  KernelHandle<ResourceDispatcher> handle;
  zx_rights_t rights;
  ASSERT_EQ(ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT32_MAX - 1, &storage),
            ZX_OK);
  // Creating a root resource should fail.
  EXPECT_EQ(ResourceDispatcher::CreateRangedRoot(&handle, &rights, ZX_RSRC_KIND_ROOT, "crr-disp1",
                                                 &storage),
            ZX_ERR_WRONG_TYPE, "Creating a root resource succeeded.");
  // Creating the shared resource will succeed.
  EXPECT_EQ(ResourceDispatcher::CreateRangedRoot(&handle, &rights, ZX_RSRC_KIND_MMIO, "crr-disp2",
                                                 &storage),
            ZX_OK, "Creating the shared resource failed.");

  EXPECT_EQ(storage.resource_list.size_slow(), 1u);
  END_TEST;
}

UNITTEST_START_TESTCASE(resources)
UNITTEST("test unconfigured allocators", unconfigured)
UNITTEST("test setting up allocators", allocators_configured)
UNITTEST("test exclusive then shared overlap", exclusive_then_shared)
UNITTEST("test shared then exclusive overlap", shared_then_exclusive)
UNITTEST("test allocating out of range", out_of_allocator_range)
UNITTEST("test root_resource_filter", root_resource_filter)
UNITTEST("test root ranged resource creation", create_root_ranged)
UNITTEST_END_TESTCASE(resources, "resource", "Tests for Resource bookkeeping")
