// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>
#include <sys/mman.h>

// These tests focus on the semantics of the VMARs themselves.  For heavier
// testing of the mapping permissions, see the VMO tests.

namespace {

const char kProcessName[] = "Test process";

const uint32_t kRwxMapPerm =
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE;
const uint32_t kRwxAllocPerm =
        MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE | MX_VM_FLAG_CAN_MAP_EXECUTE;

bool destroy_root_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    EXPECT_EQ(mx_vmar_destroy(vmar), NO_ERROR, "");

    mx_handle_t region;
    uintptr_t region_addr;
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_BAD_STATE, "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

bool basic_allocate_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t region1, region2;
    uintptr_t region1_addr, region2_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t region1_size = PAGE_SIZE * 10;
    const size_t region2_size = PAGE_SIZE;

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, region1_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region1, &region1_addr),
              NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(region1, 0, region2_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region2, &region2_addr),
              NO_ERROR, "");
    EXPECT_GE(region2_addr, region1_addr, "");
    EXPECT_LE(region2_addr + region2_size, region1_addr + region1_size, "");

    EXPECT_EQ(mx_handle_close(region1), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Attempt to allocate out of the region bounds
bool allocate_oob_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t region1, region2;
    uintptr_t region1_addr, region2_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t region1_size = PAGE_SIZE * 10;

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, region1_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region1, &region1_addr),
              NO_ERROR, "");

    EXPECT_EQ(mx_vmar_allocate(region1, region1_size, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC, &region2, &region2_addr),
              ERR_INVALID_ARGS, "");

    EXPECT_EQ(mx_vmar_allocate(region1, region1_size - PAGE_SIZE, PAGE_SIZE * 2,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
                               &region2, &region2_addr),
              ERR_INVALID_ARGS, "");

    EXPECT_EQ(mx_handle_close(region1), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Attempt to make unsatisfiable allocations
bool allocate_unsatisfiable_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t region1, region2, region3;
    uintptr_t region1_addr, region2_addr, region3_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t region1_size = PAGE_SIZE * 10;

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, region1_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region1, &region1_addr),
              NO_ERROR, "");

    // Too large to fit in the region should get ERR_INVALID_ARGS
    EXPECT_EQ(mx_vmar_allocate(region1, 0, region1_size + PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region2, &region2_addr),
              ERR_INVALID_ARGS, "");

    // Allocate the whole range, should work
    ASSERT_EQ(mx_vmar_allocate(region1, 0, region1_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region2, &region2_addr),
              NO_ERROR, "");
    EXPECT_EQ(region2_addr, region1_addr, "");

    // Attempt to allocate a page inside of the full region
    EXPECT_EQ(mx_vmar_allocate(region1, 0, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region3, &region3_addr),
              ERR_NO_MEMORY, "");

    EXPECT_EQ(mx_handle_close(region2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region1), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Validate that when we destroy a VMAR, all operations on it
// and its children fail.
bool destroyed_vmar_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t region[3] = {0};
    uintptr_t region_addr[3];
    uintptr_t map_addr[2];

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region[0], &region_addr[0]),
              NO_ERROR, "");

    // Create a mapping in region[0], so we can try to unmap it later
    ASSERT_EQ(mx_vmar_map(region[0], 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr[0]),
              NO_ERROR, "");

    // Create a subregion in region[0], so we can try to operate on it later
    ASSERT_EQ(mx_vmar_allocate(region[0], 0, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region[1], &region_addr[1]),
              NO_ERROR, "");

    // Create a mapping in region[1], so we can try to unmap it later
    ASSERT_EQ(mx_vmar_map(region[1], 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr[1]),
              NO_ERROR, "");

    // Check that both mappings work
    {
        uint8_t buf = 5;
        size_t len;
        EXPECT_EQ(mx_process_write_memory(process, map_addr[0], &buf, 1, &len),
                  NO_ERROR, "");
        EXPECT_EQ(len, 1U, "");

        buf = 0;
        EXPECT_EQ(mx_process_read_memory(process, map_addr[1], &buf, 1, &len),
                  NO_ERROR, "");
        EXPECT_EQ(len, 1U, "");
        EXPECT_EQ(buf, 5U, "");
    }

    // Destroy region[0], which should also destroy region[1]
    ASSERT_EQ(mx_vmar_destroy(region[0]), NO_ERROR, "");

    for (size_t i = 0; i < 2; ++i) {
        // Make sure the handles are still valid
        EXPECT_EQ(mx_object_get_info(region[i], MX_INFO_HANDLE_VALID, NULL, 0u, NULL, NULL),
                  NO_ERROR, "");

        // Make sure we can't access the memory mappings anymore
        {
            uint8_t buf;
            size_t read;
            EXPECT_EQ(mx_process_read_memory(process, map_addr[i], &buf, 1, &read),
                      ERR_NO_MEMORY, "");
        }

        // All mapping-modifying operations on region[0] and region[1] should fail with
        // ERR_NOT_FOUND, all other operations on them should fail with ERR_BAD_STATE
        EXPECT_EQ(mx_vmar_destroy(region[i]), ERR_BAD_STATE, "");
        EXPECT_EQ(mx_vmar_allocate(region[i], 0, PAGE_SIZE,
                                   MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                                   &region[1], &region_addr[2]),
                  ERR_BAD_STATE, "");
        EXPECT_EQ(mx_vmar_unmap(region[i], map_addr[i], PAGE_SIZE),
                  ERR_NOT_FOUND, "");
        EXPECT_EQ(mx_vmar_protect(region[i], map_addr[i], PAGE_SIZE, MX_VM_FLAG_PERM_READ),
                  ERR_NOT_FOUND, "");
        EXPECT_EQ(mx_vmar_map(region[i], 0, vmo, 0, PAGE_SIZE, MX_VM_FLAG_PERM_READ, &map_addr[i]),
                  ERR_BAD_STATE, "");
    }

    // Make sure we can still operate on the parent of region[0]
    ASSERT_EQ(mx_vmar_allocate(vmar, 0, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region[2], &region_addr[2]),
              NO_ERROR, "");


    for (mx_handle_t h : region) {
        EXPECT_EQ(mx_handle_close(h), NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Create a mapping, destroy the VMAR it is in, then attempt to create a new
// mapping over it.
bool map_over_destroyed_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo, vmo2;
    mx_handle_t region[2] = {0};
    uintptr_t region_addr[2];
    uintptr_t map_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo2), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region[0], &region_addr[0]),
              NO_ERROR, "");

    // Create a subregion in region[0], so we can try to operate on it later
    ASSERT_EQ(mx_vmar_allocate(region[0], 0, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region[1], &region_addr[1]),
              NO_ERROR, "");

    // Create a mapping in region[1], so we can try to unmap it later
    ASSERT_EQ(mx_vmar_map(region[1], 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr),
              NO_ERROR, "");

    // Check that the mapping worked
    {
        uint8_t buf = 5;
        size_t len;
        ASSERT_EQ(mx_vmo_write(vmo, &buf, 0, 1, &len), NO_ERROR, "");
        EXPECT_EQ(len, 1U, "");

        buf = 0;
        EXPECT_EQ(mx_process_read_memory(process, map_addr, &buf, 1, &len),
                  NO_ERROR, "");
        EXPECT_EQ(len, 1U, "");
        EXPECT_EQ(buf, 5U, "");
    }

    // Destroy region[1], which should unmap the VMO
    ASSERT_EQ(mx_vmar_destroy(region[1]), NO_ERROR, "");

    // Make sure we can't access the memory mappings anymore
    {
        uint8_t buf;
        size_t read;
        EXPECT_EQ(mx_process_read_memory(process, map_addr, &buf, 1, &read),
                  ERR_NO_MEMORY, "");
    }

    uintptr_t new_map_addr;
    EXPECT_EQ(mx_vmar_map(region[0], map_addr - region_addr[0], vmo2, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &new_map_addr),
              NO_ERROR, "");
    EXPECT_EQ(new_map_addr, map_addr, "");

    // Make sure we can read, and we don't see the old memory mapping
    {
        uint8_t buf;
        size_t read;
        EXPECT_EQ(mx_process_read_memory(process, map_addr, &buf, 1, &read),
                  NO_ERROR, "");
        EXPECT_EQ(read, 1U, "");
        EXPECT_EQ(buf, 0U, "");
    }

    for (mx_handle_t h : region) {
        EXPECT_EQ(mx_handle_close(h), NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}


// Attempt overmapping with FLAG_SPECIFIC to ensure it fails
bool overmapping_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t region[3] = {0};
    mx_handle_t vmar;
    mx_handle_t vmo, vmo2;
    uintptr_t region_addr[3];
    uintptr_t map_addr[2];

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE * 4, 0, &vmo2), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region[0], &region_addr[0]),
              NO_ERROR, "");

    // Create a mapping, and try to map on top of it
    ASSERT_EQ(mx_vmar_map(region[0], 0, vmo, 0, 2 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr[0]),
              NO_ERROR, "");

    // Attempt a full overmapping
    EXPECT_EQ(mx_vmar_map(region[0], map_addr[0] - region_addr[0], vmo2, 0, 2 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt a partial overmapping
    EXPECT_EQ(mx_vmar_map(region[0], map_addr[0] - region_addr[0], vmo2, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt an overmapping that is larger than the original mapping
    EXPECT_EQ(mx_vmar_map(region[0], map_addr[0] - region_addr[0], vmo2, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt to allocate a region on top
    EXPECT_EQ(mx_vmar_allocate(region[0], map_addr[0] - region_addr[0], PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
                               &region[1], &region_addr[1]),
              ERR_NO_MEMORY, "");

    // Unmap the mapping
    ASSERT_EQ(mx_vmar_unmap(region[0], map_addr[0], 2 * PAGE_SIZE), NO_ERROR, "");


    // Create a region, and try to map on top of it
    ASSERT_EQ(mx_vmar_allocate(region[0], 0, 2 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region[1], &region_addr[1]),
              NO_ERROR, "");

    // Attempt a full overmapping
    EXPECT_EQ(mx_vmar_map(region[0], region_addr[1] - region_addr[0], vmo2, 0, 2 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt a partial overmapping
    EXPECT_EQ(mx_vmar_map(region[0], region_addr[1] - region_addr[0], vmo2, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt an overmapping that is larger than the original region
    EXPECT_EQ(mx_vmar_map(region[0], region_addr[1] - region_addr[0], vmo2, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &map_addr[1]),
              ERR_NO_MEMORY, "");

    // Attempt to allocate a region on top
    EXPECT_EQ(mx_vmar_allocate(region[0], region_addr[1] - region_addr[0], PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
                               &region[2], &region_addr[2]),
              ERR_NO_MEMORY, "");

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region[0]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region[1]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Test passing in bad arguments
bool invalid_args_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t region;
    uintptr_t region_addr, map_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(4 * PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    // Bad handle
    EXPECT_EQ(mx_vmar_destroy(vmo), ERR_WRONG_TYPE, "");
    EXPECT_EQ(mx_vmar_allocate(vmo, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_WRONG_TYPE, "");
    EXPECT_EQ(mx_vmar_map(vmo, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_WRONG_TYPE, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, process, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_WRONG_TYPE, "");
    EXPECT_EQ(mx_vmar_unmap(vmo, 0, 0), ERR_WRONG_TYPE, "");
    EXPECT_EQ(mx_vmar_protect(vmo, 0, 0, MX_VM_FLAG_PERM_READ), ERR_WRONG_TYPE, "");

    // Allocating with non-zero offset and without FLAG_SPECIFIC
    EXPECT_EQ(mx_vmar_allocate(vmar, PAGE_SIZE, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_INVALID_ARGS, "");

    // Bad OUT pointers
    uintptr_t *bad_addr_ptr = reinterpret_cast<uintptr_t*>(1);
    mx_handle_t *bad_handle_ptr = reinterpret_cast<mx_handle_t*>(1);
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, bad_addr_ptr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               bad_handle_ptr, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          bad_addr_ptr),
              ERR_INVALID_ARGS, "");

    // Non-page-aligned arguments
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, PAGE_SIZE - 1,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_allocate(vmar, PAGE_SIZE - 1, PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE - 1, vmo, 0,
                          4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr + 1, PAGE_SIZE), ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, PAGE_SIZE - 1), ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr + 1, PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, PAGE_SIZE - 1, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE), NO_ERROR, "");

    // size=0
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 0,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 0, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    /* TODO(teisenbe): Re-enable these once we disable the unmap/protect len=0 compat feature
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 0), ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, 0, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE), ERR_INVALID_ARGS, "");
    */

    // Flags with invalid bits set
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 4 * PAGE_SIZE,
                               MX_VM_FLAG_PERM_READ | MX_VM_FLAG_CAN_MAP_READ |
                               MX_VM_FLAG_CAN_MAP_WRITE, &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 4 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | (1<<31),
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_CAN_MAP_EXECUTE,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | (1<<31),
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, 4 * PAGE_SIZE,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_CAN_MAP_WRITE),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, 4 * PAGE_SIZE,
                              MX_VM_FLAG_PERM_READ | (1<<31)),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Validate that dropping vmar handle rights affects mapping privileges
bool rights_drop_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t region;
    uintptr_t map_addr;
    uintptr_t region_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    const uint32_t test_rights[][3] = {
        { MX_RIGHT_READ, MX_VM_FLAG_PERM_READ },
        { MX_RIGHT_READ | MX_RIGHT_WRITE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE },
        { MX_RIGHT_READ | MX_RIGHT_EXECUTE, MX_VM_FLAG_PERM_READ |  MX_VM_FLAG_PERM_EXECUTE },
    };
    for (size_t i = 0; i < countof(test_rights); ++i) {
        uint32_t right = test_rights[i][0];
        uint32_t perm = test_rights[i][1];

        mx_handle_t new_h;
        ASSERT_EQ(mx_handle_duplicate(vmar, right, &new_h), NO_ERROR, "");

        // Try to create a mapping with permissions we don't have
        EXPECT_EQ(mx_vmar_map(new_h, 0, vmo, 0, PAGE_SIZE, kRwxMapPerm, &map_addr),
                  ERR_ACCESS_DENIED, "");

        // Try to create a mapping with permissions we do have
        ASSERT_EQ(mx_vmar_map(new_h, 0, vmo, 0, PAGE_SIZE, perm, &map_addr),
                  NO_ERROR, "");

        // Attempt to use protect to increase privileges
        EXPECT_EQ(mx_vmar_protect(new_h, map_addr, PAGE_SIZE, kRwxMapPerm),
                  ERR_ACCESS_DENIED, "");

        EXPECT_EQ(mx_vmar_unmap(new_h, map_addr, PAGE_SIZE), NO_ERROR, "");

        // Attempt to create a region that can map write (this would allow us to
        // then make writeable mappings inside of it).
        EXPECT_EQ(mx_vmar_allocate(new_h, 0, 10 * PAGE_SIZE, kRwxAllocPerm, &region, &region_addr),
                  ERR_ACCESS_DENIED, "");

        EXPECT_EQ(mx_handle_close(new_h), NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Validate that protect can't be used to escalate mapping privileges beyond
// the VMAR handle's and the original VMO handle's
bool protect_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    uintptr_t map_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    const uint32_t test_rights[][3] = {
        { MX_RIGHT_READ, MX_VM_FLAG_PERM_READ },
        { MX_RIGHT_READ | MX_RIGHT_WRITE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE },
        { MX_RIGHT_READ | MX_RIGHT_EXECUTE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE },
    };
    for (size_t i = 0; i < countof(test_rights); ++i) {
        uint32_t right = test_rights[i][0];
        uint32_t perm = test_rights[i][1];

        mx_handle_t new_h;
        ASSERT_EQ(mx_handle_duplicate(vmo, right | MX_RIGHT_MAP, &new_h), NO_ERROR, "");

        // Try to create a mapping with permissions we don't have
        EXPECT_EQ(mx_vmar_map(vmar, 0, new_h, 0, PAGE_SIZE, kRwxMapPerm, &map_addr),
                  ERR_ACCESS_DENIED, "");

        // Try to create a mapping with permissions we do have
        ASSERT_EQ(mx_vmar_map(vmar, 0, new_h, 0, PAGE_SIZE, perm, &map_addr),
                  NO_ERROR, "");

        // Attempt to use protect to increase privileges to a level allowed by
        // the VMAR but not by the VMO handle
        EXPECT_EQ(mx_vmar_protect(vmar, map_addr, PAGE_SIZE, kRwxMapPerm),
                  ERR_ACCESS_DENIED, "");

        EXPECT_EQ(mx_handle_close(new_h), NO_ERROR, "");

        // Try again now that we closed the VMO handle
        EXPECT_EQ(mx_vmar_protect(vmar, map_addr, PAGE_SIZE, kRwxMapPerm),
                  ERR_ACCESS_DENIED, "");

        EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, PAGE_SIZE), NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Validate that a region can't be created with higher RWX privileges than its
// parent.
bool nested_region_perms_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t region[2] = {0};
    uintptr_t region_addr[2];
    uintptr_t map_addr;

    ASSERT_EQ(mx_process_create(0, kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    // List of pairs of alloc/map perms to try to exclude
    const uint32_t test_perm[][2] = {
        { MX_VM_FLAG_CAN_MAP_READ, MX_VM_FLAG_PERM_READ },
        { MX_VM_FLAG_CAN_MAP_WRITE, MX_VM_FLAG_PERM_WRITE },
        { MX_VM_FLAG_CAN_MAP_EXECUTE, MX_VM_FLAG_PERM_EXECUTE },
    };

    for (size_t i = 0; i < countof(test_perm); ++i) {
        const uint32_t excluded_alloc_perm = test_perm[i][0];
        const uint32_t excluded_map_perm = test_perm[i][1];

        ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                                   kRwxAllocPerm ^ excluded_alloc_perm,
                                   &region[0], &region_addr[0]),
                  NO_ERROR, "");

        // Should fail since region[0] does not have the right perms
        EXPECT_EQ(mx_vmar_allocate(region[0], 0, PAGE_SIZE, kRwxAllocPerm,
                                   &region[1], &region_addr[1]),
                  ERR_ACCESS_DENIED, "");

        // Try to create a mapping in region[0] with the dropped rights
        EXPECT_EQ(mx_vmar_map(region[0], 0, vmo, 0, PAGE_SIZE, kRwxMapPerm, &map_addr),
                  ERR_ACCESS_DENIED, "");

        // Successfully create a mapping in region[0] (skip if we excluded READ,
        // since all mappings must be readable on most CPUs)
        if (excluded_map_perm != MX_VM_FLAG_PERM_READ) {
            EXPECT_EQ(mx_vmar_map(region[0], 0, vmo, 0, PAGE_SIZE,
                                  kRwxMapPerm ^ excluded_map_perm, &map_addr),
                      NO_ERROR, "");
            EXPECT_EQ(mx_vmar_unmap(region[0], map_addr, PAGE_SIZE), NO_ERROR, "");
        }

        // Successfully create a subregion in region[0]
        EXPECT_EQ(mx_vmar_allocate(region[0], 0, PAGE_SIZE,
                                   kRwxAllocPerm ^ excluded_alloc_perm,
                                   &region[1], &region_addr[1]),
                  NO_ERROR, "");
        EXPECT_EQ(mx_vmar_destroy(region[1]), NO_ERROR, "");
        EXPECT_EQ(mx_handle_close(region[1]), NO_ERROR, "");

        EXPECT_EQ(mx_vmar_destroy(region[0]), NO_ERROR, "");
        EXPECT_EQ(mx_handle_close(region[0]), NO_ERROR, "");
    }

    // Make sure we can't use SPECIFIC in a region without CAN_MAP_SPECIFIC
    ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               kRwxAllocPerm,
                               &region[0], &region_addr[0]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_map(region[0], PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_SPECIFIC | MX_VM_FLAG_PERM_READ, &map_addr),
              ERR_ACCESS_DENIED, "");
    EXPECT_EQ(mx_vmar_destroy(region[0]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region[0]), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

}

BEGIN_TEST_CASE(vmar_tests)
RUN_TEST(destroy_root_test);
RUN_TEST(basic_allocate_test);
RUN_TEST(allocate_oob_test);
RUN_TEST(allocate_unsatisfiable_test);
RUN_TEST(destroyed_vmar_test);
RUN_TEST(map_over_destroyed_test);
RUN_TEST(overmapping_test);
RUN_TEST(invalid_args_test);
RUN_TEST(rights_drop_test);
RUN_TEST(protect_test);
RUN_TEST(nested_region_perms_test);
END_TEST_CASE(vmar_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
