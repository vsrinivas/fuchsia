// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/stdcompat/atomic.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <sys/types.h>

#include <initializer_list>

#include <zxtest/zxtest.h>

namespace {

static size_t kPageSize = zx_system_get_page_size();

class ProtectTestCase : public zxtest::Test {
 public:
  void SetUp() {
    auto root_vmar = zx::vmar::root_self();

    ASSERT_OK(zx::vmo::create(kTestPages * zx_system_get_page_size(), 0, &vmo));
    // Fully commit the VMO with 'random' data.
    for (size_t i = 0; i < kTestPages; i++) {
      uint64_t val = i + 1;
      EXPECT_OK(vmo.write(&val, i * zx_system_get_page_size(), sizeof(val)));
    }
  }

 protected:
  static constexpr size_t kTestPages = 64;
  // Define read and write flags to overlap with permission flags for simplicity of testing and
  // setting.
  static constexpr uint READ = ZX_VM_PERM_READ;
  static constexpr uint WRITE = ZX_VM_PERM_WRITE;
  static constexpr uint PERM_FLAGS = READ | WRITE;
  // Reuse other flags to indicate mapping and unmapping, this is done just to ensure the values we
  // choose don't collide with READ or WRITE, but otherwise have no relation.
  static constexpr uint NOT_MAPPED = ZX_VM_SPECIFIC_OVERWRITE;
  static constexpr uint DO_MAP = ZX_VM_MAP_RANGE;
  zx::vmo vmo;

  struct Range {
    uint page_start;
    uint page_end;
    uint flags;
  };

  void ValidateAspaceMaps(zx_vaddr_t base, std::initializer_list<Range> check_maps) {
    auto self = zx::process::self();

    std::vector<zx_info_maps_t> maps;
    size_t actual = 0;
    size_t avail = 0;

    do {
      ASSERT_OK(self->get_info(ZX_INFO_PROCESS_MAPS, maps.data(), sizeof(zx_info_maps_t) * avail,
                               &actual, &avail));
      maps.resize(avail);
    } while (actual != avail);

    for (auto& check : check_maps) {
      // Find the aspace map that contains this.
      const zx_vaddr_t check_base = base + check.page_start * kPageSize;
      const zx_vaddr_t check_end = base + check.page_end * kPageSize;

      bool checked = false;
      for (auto& map : maps) {
        if (map.type != ZX_INFO_MAPS_TYPE_MAPPING) {
          continue;
        }
        const zx_vaddr_t map_end = map.base + map.size;
        // The map either fully contains our range to check, or we have an error which will get
        // caught in the parent loop by |checked| staying false.
        if (map.base > check_base || map_end < check_end) {
          continue;
        }
        // This map contains what we want to check, now validate that it's exactly the region.
        EXPECT_EQ(check_base, map.base);
        EXPECT_EQ(check_end, map_end);
        // It's correctly a subrange, double check the VMO offset.
        EXPECT_EQ(map.u.mapping.vmo_offset, map.base - base);
        // Check the protection flags, focusing on just the read and write permissions.
        EXPECT_EQ(map.u.mapping.mmu_flags & PERM_FLAGS, check.flags & PERM_FLAGS);
        checked = true;
        break;
      }
      if (check.flags & NOT_MAPPED) {
        // We expect this to not be mapped, and so we should not have found it.
        EXPECT_FALSE(checked);
      } else {
        // Make sure we found a range in the aspace covering our range to check.
        EXPECT_TRUE(checked);
      }
    }
  }

  void ValidateAccessByTouch(zx_vaddr_t base, std::initializer_list<Range> final) {
    zx::vmo test;
    ASSERT_OK(zx::vmo::create(kPageSize, 0, &test));
    // Use ASSERT instead of EXPECT just to avoid massive log spam since once a page is detected as
    // wrong *lots* of pages in that range can be expected to be wrong.
    for (auto& range : final) {
      if (range.flags & NOT_MAPPED) {
        continue;
      }
      for (size_t page = range.page_start; page < range.page_end; page++) {
        uint64_t val;
        // To see if we can access the page without having to deal with spinning up threads and
        // crash reports, we will just ask other kernel syscalls to read/write from it.
        // Note that *writing* to the test VMO will cause it to have to *read* from our target, and
        // its the target whose permissions we are trying to test.
        zx_status_t read_result = test.write((void*)(base + page * kPageSize), 0, sizeof(val));
        if (range.flags & READ) {
          ASSERT_OK(read_result);
          ASSERT_OK(test.read(&val, 0, sizeof(val)));
          ASSERT_EQ(page + 1, val);
        } else {
          ASSERT_EQ(ZX_ERR_ACCESS_DENIED, read_result);
        }
        val = page + 1;
        ASSERT_OK(test.write(&val, 0, sizeof(val)));
        zx_status_t write_result = test.read((void*)(base + page * kPageSize), 0, sizeof(val));
        if (range.flags & WRITE) {
          ASSERT_OK(write_result);
        } else {
          ASSERT_EQ(ZX_ERR_ACCESS_DENIED, write_result);
        }
      }
    }
  }

  void ProtectRanges(zx_vaddr_t map_base, std::initializer_list<Range> ranges) {
    auto root_vmar = zx::vmar::root_self();
    for (auto& range : ranges) {
      ASSERT_EQ((range.flags & (READ | WRITE)), range.flags);
      EXPECT_OK(root_vmar->protect(range.flags, map_base + (range.page_start * kPageSize),
                                   (range.page_end - range.page_start) * kPageSize));
    }
  }

  void TestOps(std::initializer_list<Range> ops, std::initializer_list<Range> final,
               zx_vm_option_t options, bool start_mapped) {
    // Create a VMAR that we will map/unmap into.
    auto root_vmar = zx::vmar::root_self();

    zx::vmar vmar;
    zx_vaddr_t base;
    ASSERT_OK(root_vmar->allocate(ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE,
                                  0, kPageSize * kTestPages, &vmar, &base));
    auto unmap = fit::defer([&vmar] { vmar.destroy(); });

    if (start_mapped) {
      ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | options, 0, vmo, 0,
                         kTestPages * kPageSize, &base));
    }

    // Perform all the protect operations
    for (auto& range : ops) {
      if (range.flags & NOT_MAPPED) {
        // Unmap
        EXPECT_OK(vmar.unmap(base + kPageSize * range.page_start,
                             kPageSize * (range.page_end - range.page_start)));
      } else {
        if (range.flags & DO_MAP) {
          // First map in the range with full permissions, then let it get protected down. This is
          // done so that mappings have the same flags and can get merged together.
          zx_vaddr_t offset;
          EXPECT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | options,
                             range.page_start * kPageSize, vmo, range.page_start * kPageSize,
                             (range.page_end - range.page_start) * kPageSize, &offset));
          EXPECT_EQ(base + range.page_start * kPageSize, offset);
        }
        EXPECT_OK(vmar.protect(range.flags & PERM_FLAGS, base + (range.page_start * kPageSize),
                               (range.page_end - range.page_start) * kPageSize));
      }
    }

    // Validate the final ranges by checking the reported aspace maps, and by actually read/writing.
    ValidateAspaceMaps(base, final);
    ValidateAccessByTouch(base, final);
  }

  void TestOps(std::initializer_list<Range> ops, std::initializer_list<Range> final,
               bool start_mapped) {
    // Run each test with and without mappings hardware mappings precommitted, this validates that
    // the actual architectural mapping updates happen, in addition to the metadata updates that
    // impact future page lookups/mappings.
    TestOps(ops, final, 0, start_mapped);
    TestOps(ops, final, ZX_VM_MAP_RANGE, start_mapped);
  }

  void TestOpsMapped(std::initializer_list<Range> ops, std::initializer_list<Range> final) {
    TestOps(ops, final, true);
  }

  void TestOpsUnmapped(std::initializer_list<Range> ops, std::initializer_list<Range> final) {
    TestOps(ops, final, false);
  }
};

// Test most of the paths through VmMapping::Protect
TEST_F(ProtectTestCase, SingleMapping) {
  // Baseline case, no protection operations done
  TestOpsMapped({}, {{0, 64, READ | WRITE}});

  // Create single protects anchored at either end or hanging in the middle
  TestOpsMapped({{0, 24, READ}}, {{0, 24, READ}, {24, 64, READ | WRITE}});
  TestOpsMapped({{37, 64, READ}}, {{0, 37, READ | WRITE}, {37, 64, READ}});
  TestOpsMapped({{24, 37, READ}}, {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});

  // Rewrite the whole range with a new protection
  TestOpsMapped({{0, 64, READ}}, {{0, 64, READ}});
  TestOpsMapped({{24, 37, READ}, {0, 64, READ}}, {{0, 64, READ}});
  TestOpsMapped({{24, 37, READ}, {0, 64, READ | WRITE}}, {{0, 64, READ | WRITE}});

  // Protect sub ranges of various kinds, including at either ends, with the same permissions
  TestOpsMapped({{24, 37, READ | WRITE}}, {{0, 64, READ | WRITE}});
  TestOpsMapped({{0, 24, READ | WRITE}}, {{0, 64, READ | WRITE}});
  TestOpsMapped({{37, 64, READ | WRITE}}, {{0, 64, READ | WRITE}});
  // Within the first range
  TestOpsMapped({{24, 37, READ}, {0, 24, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {4, 24, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {0, 20, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {4, 20, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  // Within the last range
  TestOpsMapped({{24, 37, READ}, {37, 64, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {41, 64, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {41, 60, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  TestOpsMapped({{24, 37, READ}, {37, 60, READ | WRITE}},
                {{0, 24, READ | WRITE}, {24, 37, READ}, {37, 64, READ | WRITE}});
  // In the middle of a sub range.
  TestOpsMapped({{10, 30, READ}, {10, 30, READ}},
                {{0, 10, READ | WRITE}, {10, 30, READ}, {30, 64, READ | WRITE}});
  TestOpsMapped({{10, 30, READ}, {10, 25, READ}},
                {{0, 10, READ | WRITE}, {10, 30, READ}, {30, 64, READ | WRITE}});
  TestOpsMapped({{10, 30, READ}, {15, 30, READ}},
                {{0, 10, READ | WRITE}, {10, 30, READ}, {30, 64, READ | WRITE}});
  TestOpsMapped({{10, 30, READ}, {15, 25, READ}},
                {{0, 10, READ | WRITE}, {10, 30, READ}, {30, 64, READ | WRITE}});

  // Fill in a gap between two protection domains in different overlapping ways that should result
  // in one large protection domain being formed.
  // Try all variations of starting and ending at or between protection boundaries.
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {20, 30, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {10, 40, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {10, 30, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {20, 40, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {15, 40, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {15, 30, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {15, 35, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {10, 35, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{10, 20, READ}, {30, 40, READ}, {20, 35, READ}},
                {{0, 10, READ | WRITE}, {10, 40, READ}, {40, 64, READ | WRITE}});
  // Now in way that merges into the start
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {10, 20, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {5, 20, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {0, 20, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {5, 25, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {10, 25, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {0, 30, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {5, 30, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 64, READ}, {10, 30, READ}},
                {{0, 30, READ}, {30, 40, READ | WRITE}, {40, 64, READ}});
  // Now merging into the end.
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {40, 50, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {40, 55, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {40, 64, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {35, 50, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {35, 55, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {35, 64, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {30, 50, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {30, 55, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
  TestOpsMapped({{30, 40, READ}, {50, 64, READ}, {30, 64, READ}},
                {{0, 30, READ | WRITE}, {30, 64, READ}});
}

// Validate that protection ranges are correctly iterated when creating a cow-clone.
TEST_F(ProtectTestCase, CreateCowClone) {
  auto root_vmar = zx::vmar::root_self();
  zx_vaddr_t base;
  ASSERT_OK(root_vmar->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, 0, vmo, 0,
                           kTestPages * kPageSize, &base));
  auto umap = fit::defer([&base, &root_vmar] { root_vmar->unmap(base, kPageSize * kTestPages); });

  // Create some readable and writable mappings that need to be traversed.
  ProtectRanges(base, {{0, 10, READ}, {20, 30, READ}, {40, 50, READ}});

  // Although we did ZX_VM_MAP_RANGE, run the validation process to touch all the pages to make sure
  // that there are hardware mappings for them.
  ValidateAccessByTouch(base, {{0, 10, READ},
                               {10, 20, READ | WRITE},
                               {20, 30, READ},
                               {30, 40, READ | WRITE},
                               {40, 50, READ},
                               {50, 64, READ | WRITE}});

  // Create a clone and write to the writable portions and validate they cause a fork to happen.
  zx::vmo clone;
  EXPECT_OK(vmo.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, kTestPages * kPageSize, &clone));
  // Test a selection of pages in the writable ranges.
  for (auto page : {10, 15, 19, 35, 50, 55, 63}) {
    // Write to the memory. This should have had its write permission temporarily removed so that
    // the kernel traps this and forks the page.
    cpp20::atomic_ref(*reinterpret_cast<uint64_t*>(base + page * kPageSize)).store(page * 100);
    // Read directly from the VMO and validate the write happened.
    uint64_t val;
    EXPECT_OK(vmo.read(&val, page * kPageSize, sizeof(val)));
    EXPECT_EQ(val, page * 100);
    // Validate that the clone doesn't see the write.
    EXPECT_OK(clone.read(&val, page * kPageSize, sizeof(val)));
    EXPECT_EQ(val, page + 1);
  }
}

// Test that if there are protection regions and an actual unmap occurs (not a protect to none) that
// any remaining mapping(s) get the correct permissions.
TEST_F(ProtectTestCase, Unmap) {
  // Canary test regular unmap cases without any different protections
  TestOpsMapped({{20, 40, NOT_MAPPED}},
                {{0, 20, READ | WRITE}, {20, 40, NOT_MAPPED}, {40, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, NOT_MAPPED}}, {{0, 20, NOT_MAPPED}, {20, 64, READ | WRITE}});
  TestOpsMapped({{20, 64, NOT_MAPPED}}, {{0, 20, READ | WRITE}, {20, 64, NOT_MAPPED}});

  // Have a single protection change in a mapping, and unmap it completely.
  TestOpsMapped({{0, 20, READ}, {0, 20, NOT_MAPPED}},
                {{0, 20, NOT_MAPPED}, {20, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, READ}, {20, 64, NOT_MAPPED}}, {{0, 20, READ}, {20, 64, NOT_MAPPED}});
  TestOpsMapped({{20, 30, READ}, {20, 30, NOT_MAPPED}},
                {{0, 20, READ | WRITE}, {20, 30, NOT_MAPPED}, {30, 64, READ | WRITE}});

  // Have a single protection change but unmap less than it.
  TestOpsMapped({{0, 20, READ}, {0, 15, NOT_MAPPED}},
                {{0, 15, NOT_MAPPED}, {15, 20, READ}, {20, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, READ}, {5, 15, NOT_MAPPED}},
                {{0, 5, READ}, {5, 15, NOT_MAPPED}, {15, 20, READ}, {20, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, READ}, {5, 20, NOT_MAPPED}},
                {{0, 5, READ}, {5, 20, NOT_MAPPED}, {20, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, READ}, {20, 60, NOT_MAPPED}},
                {{0, 20, READ}, {20, 60, NOT_MAPPED}, {60, 64, READ | WRITE}});
  TestOpsMapped(
      {{0, 20, READ}, {25, 60, NOT_MAPPED}},
      {{0, 20, READ}, {20, 25, READ | WRITE}, {25, 60, NOT_MAPPED}, {60, 64, READ | WRITE}});
  TestOpsMapped({{0, 20, READ}, {25, 64, NOT_MAPPED}},
                {{0, 20, READ}, {20, 25, READ | WRITE}, {20, 64, NOT_MAPPED}});
  TestOpsMapped(
      {{20, 40, READ}, {20, 35, NOT_MAPPED}},
      {{0, 20, READ | WRITE}, {20, 35, NOT_MAPPED}, {35, 40, READ}, {40, 64, READ | WRITE}});
  TestOpsMapped({{20, 40, READ}, {25, 35, NOT_MAPPED}}, {{0, 20, READ | WRITE},
                                                         {20, 25, READ},
                                                         {25, 35, NOT_MAPPED},
                                                         {35, 40, READ},
                                                         {40, 64, READ | WRITE}});
  TestOpsMapped(
      {{20, 40, READ}, {25, 40, NOT_MAPPED}},
      {{0, 20, READ | WRITE}, {20, 25, READ}, {25, 40, NOT_MAPPED}, {40, 64, READ | WRITE}});

  // Single protection change but unmapping across it.
  TestOpsMapped({{0, 30, READ}, {0, 35, NOT_MAPPED}},
                {{0, 35, NOT_MAPPED}, {35, 64, READ | WRITE}});
  TestOpsMapped({{0, 30, READ}, {25, 64, NOT_MAPPED}}, {{0, 25, READ}, {25, 64, NOT_MAPPED}});
  TestOpsMapped({{0, 30, READ}, {25, 35, NOT_MAPPED}},
                {{0, 25, READ}, {25, 35, NOT_MAPPED}, {35, 64, READ | WRITE}});

  // Multiple protections, unmapping from the sides.
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {15, 64, NOT_MAPPED}},
                {{0, 10, READ}, {10, 15, READ | WRITE}, {15, 64, NOT_MAPPED}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {20, 64, NOT_MAPPED}},
                {{0, 10, READ}, {10, 20, READ | WRITE}, {20, 64, NOT_MAPPED}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {0, 40, NOT_MAPPED}},
                {{0, 40, NOT_MAPPED}, {40, 50, READ}, {50, 64, READ | WRITE}});
  TestOpsMapped(
      {{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {0, 35, NOT_MAPPED}},
      {{0, 35, NOT_MAPPED}, {35, 40, READ | WRITE}, {40, 50, READ}, {50, 64, READ | WRITE}});

  // Multiple protections unmapping from the middle.
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {10, 40, NOT_MAPPED}},
                {{0, 10, READ}, {10, 40, NOT_MAPPED}, {40, 50, READ}, {50, 64, READ | WRITE}});
  TestOpsMapped(
      {{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {15, 50, NOT_MAPPED}},
      {{0, 10, READ}, {10, 15, READ | WRITE}, {15, 50, NOT_MAPPED}, {50, 64, READ | WRITE}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {0, 40, NOT_MAPPED}},
                {{0, 40, NOT_MAPPED}, {40, 50, READ}, {50, 64, READ | WRITE}});
  TestOpsMapped({{0, 10, READ}, {20, 30, READ}, {40, 50, READ}, {15, 45, NOT_MAPPED}},
                {{0, 10, READ},
                 {10, 15, READ | WRITE},
                 {15, 45, NOT_MAPPED},
                 {45, 50, READ},
                 {50, 64, READ | WRITE}});
}

// Tests for adding a mapping such that a merge occurs due to the new mapping that is virtually and
// object contiguous. This merge could cause one or two mappings, that have multiple protection
// ranges, to get joined together.
TEST_F(ProtectTestCase, MergeMappings) {
  // Single protection regions, joining left or write.
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP}, {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 20, READ | WRITE}});
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP}, {10, 20, READ | DO_MAP}},
                  {{0, 10, READ | WRITE}, {10, 20, READ}});
  TestOpsUnmapped({{10, 20, READ | WRITE | DO_MAP}, {0, 10, READ | WRITE | DO_MAP}},
                  {{0, 20, READ | WRITE}});
  TestOpsUnmapped({{10, 20, READ | DO_MAP}, {0, 10, READ | WRITE | DO_MAP}},
                  {{0, 10, READ | WRITE}, {10, 20, READ}});

  // Single protection regions, joining left and right.
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP},
                   {20, 30, READ | WRITE | DO_MAP},
                   {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 30, READ | WRITE}});
  TestOpsUnmapped(
      {{0, 10, READ | DO_MAP}, {20, 30, READ | WRITE | DO_MAP}, {10, 20, READ | WRITE | DO_MAP}},
      {{0, 10, READ}, {10, 30, READ | WRITE}});
  TestOpsUnmapped(
      {{0, 10, READ | WRITE | DO_MAP}, {20, 30, READ | DO_MAP}, {10, 20, READ | WRITE | DO_MAP}},
      {{0, 20, READ | WRITE}, {20, 30, READ}});
  TestOpsUnmapped(
      {{0, 10, READ | DO_MAP}, {20, 30, READ | DO_MAP}, {10, 20, READ | WRITE | DO_MAP}},
      {{0, 10, READ}, {10, 20, READ | WRITE}, {20, 30, READ}});

  // Multiple protections, joining on one side with either same or different permissions.
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP}, {2, 4, READ}, {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 2, READ | WRITE}, {2, 4, READ}, {4, 20, READ | WRITE}});
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP},
                   {2, 4, READ},
                   {6, 10, READ},
                   {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 2, READ | WRITE},
                   {2, 4, READ},
                   {4, 6, READ | WRITE},
                   {6, 10, READ},
                   {10, 20, READ | WRITE}});
  TestOpsUnmapped({{10, 20, READ | WRITE | DO_MAP}, {12, 15, READ}, {0, 10, READ | WRITE | DO_MAP}},
                  {{0, 12, READ | WRITE}, {12, 15, READ}, {15, 20, READ | WRITE}});
  TestOpsUnmapped({{10, 20, READ | DO_MAP}, {12, 15, READ | WRITE}, {0, 10, READ | WRITE | DO_MAP}},
                  {{0, 10, READ | WRITE}, {10, 12, READ}, {12, 15, READ | WRITE}, {15, 20, READ}});

  // Multiple protections, joining in the middle.
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP},
                   {20, 30, READ | WRITE | DO_MAP},
                   {25, 30, READ},
                   {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 25, READ | WRITE}, {25, 30, READ}});
  TestOpsUnmapped({{0, 10, READ | WRITE | DO_MAP},
                   {0, 5, READ},
                   {20, 30, READ | DO_MAP},
                   {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 5, READ}, {5, 20, READ | WRITE}, {20, 30, READ}});
  TestOpsUnmapped({{0, 10, READ | DO_MAP},
                   {20, 30, READ | WRITE | DO_MAP},
                   {25, 30, READ},
                   {10, 20, READ | WRITE | DO_MAP}},
                  {{0, 10, READ}, {10, 25, READ | WRITE}, {25, 30, READ}});
}

}  // namespace
