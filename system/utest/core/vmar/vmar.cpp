// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>
#include <unittest/unittest.h>
#include <sys/mman.h>

// These tests focus on the semantics of the VMARs themselves.  For heavier
// testing of the mapping permissions, see the VMO tests.

namespace {

const char kProcessName[] = "test-proc-vmar";

const uint32_t kRwxMapPerm =
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE;
const uint32_t kRwxAllocPerm =
        MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE | MX_VM_FLAG_CAN_MAP_EXECUTE;


// Helper routine for other tests.  If bit i (< *page_count*) in *bitmap* is set, then
// checks that *base* + i * PAGE_SIZE is mapped.  Otherwise checks that it is not mapped.
bool check_pages_mapped(mx_handle_t process, uintptr_t base, uint64_t bitmap, size_t page_count) {
    uint8_t buf[1];
    size_t len;

    size_t i = 0;
    while (bitmap && i < page_count) {
        mx_status_t expected = (bitmap & 1) ? NO_ERROR : ERR_NO_MEMORY;
        if (mx_process_read_memory(process, base + i * PAGE_SIZE, buf, 1, &len) != expected) {
            return false;
        }
        ++i;
        bitmap >>= 1;
    }
    return true;
}

// Thread run by test_local_address, used to attempt an access to memory
void test_write_address_thread(uintptr_t address, bool* success) {
    auto p = reinterpret_cast<volatile uint8_t*>(address);
    *p = 5;
    CF;
    *success = true;

    mx_thread_exit();
}
// Thread run by test_local_address, used to attempt an access to memory
void test_read_address_thread(uintptr_t address, bool* success) {
    auto p = reinterpret_cast<volatile uint8_t*>(address);
    *p;
    CF;
    *success = true;

    mx_thread_exit();
}

// Helper routine for testing via direct access whether or not an address in the
// test process's address space is accessible.
mx_status_t test_local_address(uintptr_t address, bool write, bool* success) {
    *success = false;

    alignas(16) static uint8_t thread_stack[PAGE_SIZE];

    mx_exception_packet_t packet;
    bool saw_page_fault = false;

    mx_handle_t thread = MX_HANDLE_INVALID;
    mx_handle_t port = MX_HANDLE_INVALID;
    uintptr_t entry = reinterpret_cast<uintptr_t>(write ? test_write_address_thread :
                                                          test_read_address_thread);
    uintptr_t stack = reinterpret_cast<uintptr_t>(thread_stack + sizeof(thread_stack));

    mx_status_t status = mx_thread_create(mx_process_self(), "vmar_test_addr", 14, 0, &thread);
    if (status != NO_ERROR) {
        goto err;
    }

    // Create an exception port and bind it to the thread to prevent the
    // thread's illegal access from killing the process.
    status = mx_port_create(0, &port);
    if (status != NO_ERROR) {
        goto err;
    }
    status = mx_task_bind_exception_port(thread, port, 0, 0);
    if (status != NO_ERROR) {
        goto err;
    }

    status = mx_thread_start(thread, entry, stack,
                             address, reinterpret_cast<uintptr_t>(success));
    if (status != NO_ERROR) {
        goto err;
    }

    // Wait for the thread to exit and identify its cause of death.
    // Keep looping until the thread is gone so that crashlogger doesn't
    // see the page fault.
    do {
        mx_status_t s;

        s = mx_port_wait(port, MX_TIME_INFINITE, &packet, sizeof(packet));
        if (s != NO_ERROR && status != NO_ERROR) {
            status = s;
            break;
        }
        if (packet.hdr.type != MX_PORT_PKT_TYPE_EXCEPTION) {
            status = ERR_BAD_STATE;
            break;
        }
        if (packet.report.header.type == MX_EXCP_FATAL_PAGE_FAULT) {
            mx_task_kill(thread);
            saw_page_fault = true;
            // Leave status as is.
        }
        else if (packet.report.header.type == MX_EXCP_GONE) {
            // Leave status as is.
        }
        else {
            mx_task_kill(thread);
            if (status != NO_ERROR)
                status = ERR_BAD_STATE;
        }
    } while (packet.report.header.type != MX_EXCP_GONE);

    if (status == NO_ERROR && !saw_page_fault)
        *success = true;

    // fallthrough to cleanup
err:
    if (thread != MX_HANDLE_INVALID)
        mx_task_bind_exception_port(thread, MX_HANDLE_INVALID, 0, 0);
    mx_handle_close(port);
    mx_handle_close(thread);
    return status;
}

bool destroy_root_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

bool map_in_compact_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t region;
    uintptr_t region_addr, map_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t region_size = PAGE_SIZE * 10;
    const size_t map_size = PAGE_SIZE;

    ASSERT_EQ(mx_vmo_create(map_size, 0, &vmo), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, region_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE | MX_VM_FLAG_COMPACT,
                               &region, &region_addr),
              NO_ERROR, "");

    ASSERT_EQ(mx_vmar_map(region, 0, vmo, 0, map_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr),
              NO_ERROR, "");
    EXPECT_GE(map_addr, region_addr, "");
    EXPECT_LE(map_addr + map_size, region_addr + region_size, "");

    // Make a second allocation
    ASSERT_EQ(mx_vmar_map(region, 0, vmo, 0, map_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &map_addr),
              NO_ERROR, "");
    EXPECT_GE(map_addr, region_addr, "");
    EXPECT_LE(map_addr + map_size, region_addr + region_size, "");

    EXPECT_EQ(mx_handle_close(region), NO_ERROR, "");
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

        // All operations on region[0] and region[1] should fail with ERR_BAD_STATE
        EXPECT_EQ(mx_vmar_destroy(region[i]), ERR_BAD_STATE, "");
        EXPECT_EQ(mx_vmar_allocate(region[i], 0, PAGE_SIZE,
                                   MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                                   &region[1], &region_addr[2]),
                  ERR_BAD_STATE, "");
        EXPECT_EQ(mx_vmar_unmap(region[i], map_addr[i], PAGE_SIZE),
                  ERR_BAD_STATE, "");
        EXPECT_EQ(mx_vmar_protect(region[i], map_addr[i], PAGE_SIZE, MX_VM_FLAG_PERM_READ),
                  ERR_BAD_STATE, "");
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE * 4, 0, &vmo2), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, 10 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &region[0], &region_addr[0]),
              NO_ERROR, "");

    // Create a mapping, and try to map on top of it
    ASSERT_EQ(mx_vmar_map(region[0], PAGE_SIZE, vmo, 0, 2 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &map_addr[0]),
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
    ASSERT_EQ(mx_vmar_allocate(region[0], PAGE_SIZE, 2 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo, PAGE_SIZE - 1,
                          3 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr + 1, PAGE_SIZE), ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr + 1, PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE), NO_ERROR, "");

    // Overflowing vmo_offset
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, UINT64_MAX + 1 - PAGE_SIZE,
                          PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, UINT64_MAX + 1 - 2 * PAGE_SIZE,
                          PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, PAGE_SIZE), NO_ERROR, "");

    // size=0
    EXPECT_EQ(mx_vmar_allocate(vmar, 0, 0,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 0, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0,
                          4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 0), ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, 0, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE), NO_ERROR, "");

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

// Test passing in unaligned lens to unmap/protect
bool unaligned_len_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    uintptr_t map_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(4 * PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE, MX_VM_FLAG_PERM_READ, &map_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, map_addr, 4 * PAGE_SIZE - 1,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, map_addr, 4 * PAGE_SIZE - 1), NO_ERROR, "");

    // Make sure we can't access the last page of the memory mappings anymore
    {
        uint8_t buf;
        size_t read;
        EXPECT_EQ(mx_process_read_memory(process, map_addr + 3 * PAGE_SIZE, &buf, 1, &read),
                  ERR_NO_MEMORY, "");
    }

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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
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
    EXPECT_EQ(mx_vmar_map(region[0], PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_SPECIFIC_OVERWRITE | MX_VM_FLAG_PERM_READ,
                          &map_addr),
              ERR_ACCESS_DENIED, "");
    EXPECT_EQ(mx_vmar_destroy(region[0]), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(region[0]), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

bool object_info_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t region;
    uintptr_t region_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t region_size = PAGE_SIZE * 10;

    ASSERT_EQ(mx_vmar_allocate(vmar, 0, region_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
                               &region, &region_addr),
              NO_ERROR, "");

    mx_info_vmar_t info;
    ASSERT_EQ(mx_object_get_info(region, MX_INFO_VMAR, &info, sizeof(info), NULL, NULL),
              NO_ERROR, "");
    EXPECT_EQ(info.base, region_addr, "");
    EXPECT_EQ(info.len, region_size, "");

    EXPECT_EQ(mx_handle_close(region), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can split a single mapping with an unmap call
bool unmap_split_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    uintptr_t mapping_addr[3];

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(4 * PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    // Set up mappings to test on
    for (uintptr_t& addr : mapping_addr) {
        EXPECT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              &addr),
                  NO_ERROR, "");
    }

    // Unmap from the left
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1100, 4), "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0] + 2 * PAGE_SIZE, 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000, 4), "");

    // Unmap from the right
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1] + 2 * PAGE_SIZE, 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[1], 0b0011, 4), "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1], 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[1], 0b0000, 4), "");

    // Unmap from the center
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[2] + PAGE_SIZE, 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[2], 0b1001, 4), "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[2], PAGE_SIZE), NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[2] + 3 * PAGE_SIZE, PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[2], 0b0000, 4), "");

    mx_info_vmar_t info;
    ASSERT_EQ(mx_object_get_info(vmar, MX_INFO_VMAR, &info, sizeof(info), NULL, NULL),
              NO_ERROR, "");

    // Make sure we can map over these again
    for (uintptr_t addr : mapping_addr) {
        const size_t offset = addr - info.base;
        EXPECT_EQ(mx_vmar_map(vmar, offset, vmo, 0, 4 * PAGE_SIZE,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &addr),
                  NO_ERROR, "");
        EXPECT_TRUE(check_pages_mapped(process, addr, 0b1111, 4), "");
        EXPECT_EQ(mx_vmar_unmap(vmar, addr, 4 * PAGE_SIZE), NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can unmap multiple ranges simultaneously
bool unmap_multiple_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    mx_handle_t subregion;
    uintptr_t mapping_addr[3];
    uintptr_t subregion_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t mapping_size = 4 * PAGE_SIZE;
    ASSERT_EQ(mx_vmo_create(mapping_size, 0, &vmo), NO_ERROR, "");

    // Create two mappings
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(mx_vmar_map(vmar, i * mapping_size, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr[i]),
                  NO_ERROR, "");
    }
    EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[1], "");
    // Unmap from the right of the first and the left of the second
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0] + 2 * PAGE_SIZE, 3 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0011, 8), "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 2 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1] + 1 * PAGE_SIZE, 3 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000, 8), "");

    // Create two mappings with a gap, and verify we can unmap them
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(mx_vmar_map(vmar, 2 * i * mapping_size, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr[i]),
                  NO_ERROR, "");
    }
    EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[1], "");
    // Unmap all of the left one and some of the right one
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 2 * mapping_size + PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0000'0000, 12), "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1] + 1 * PAGE_SIZE, 3 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");

    // Create two mappings with a subregion between, should be able to unmap
    // them (and destroy the subregion in the process).
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(mx_vmar_map(vmar, 2 * i * mapping_size, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr[i]),
                  NO_ERROR, "");
    }
    ASSERT_EQ(mx_vmar_allocate(vmar, mapping_size, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC | MX_VM_FLAG_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(subregion, 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[1], "");
    EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[2], "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0001'1111, 12), "");
    // Unmap all of the left one and some of the right one
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 2 * mapping_size + PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1110'0000'0000, 12), "");
    // Try to map in the subregion again, should fail due to being destroyed
    ASSERT_EQ(mx_vmar_map(subregion, PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              ERR_BAD_STATE, "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1] + 1 * PAGE_SIZE, 3 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");
    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    // Create two mappings with a subregion after.  Partial unmap of the
    // subregion should fail, full unmap should succeed.
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(mx_vmar_map(vmar, i * mapping_size, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr[i]),
                  NO_ERROR, "");
    }
    ASSERT_EQ(mx_vmar_allocate(vmar, 2 * mapping_size, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC | MX_VM_FLAG_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(subregion, 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mapping_addr[0] + mapping_size, mapping_addr[1], "");
    EXPECT_EQ(mapping_addr[0] + 2 * mapping_size, mapping_addr[2], "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0001'1111'1111, 12), "");
    // Unmap some of the left one through to all but the last page of the subregion
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0] + PAGE_SIZE, 3 * mapping_size - 2 * PAGE_SIZE),
              ERR_INVALID_ARGS, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0001'1111'1111, 12), "");
    // Try again, but unmapping all of the subregion
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0] + PAGE_SIZE, 3 * mapping_size - PAGE_SIZE),
              NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0001, 12), "");
    // Try to map in the subregion again, should fail due to being destroyed
    ASSERT_EQ(mx_vmar_map(subregion, PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              ERR_BAD_STATE, "");
    // Unmap the rest
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");
    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    // Create two mappings with a subregion before.  Partial unmap of the
    // subregion should fail, full unmap should succeed.
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(mx_vmar_map(vmar, (i + 1) * mapping_size, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr[i]),
                  NO_ERROR, "");
    }
    ASSERT_EQ(mx_vmar_allocate(vmar, 0, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_CAN_MAP_SPECIFIC | MX_VM_FLAG_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(subregion, mapping_size - PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(subregion_addr + mapping_size, mapping_addr[0], "");
    EXPECT_EQ(subregion_addr + 2 * mapping_size, mapping_addr[1], "");
    EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b1111'1111'1000, 12), "");
    // Try to unmap everything except the first page of the subregion
    EXPECT_EQ(mx_vmar_unmap(vmar, subregion_addr + PAGE_SIZE, 3 * mapping_size - PAGE_SIZE),
              ERR_INVALID_ARGS, "");
    EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b1111'1111'1000, 12), "");
    // Try again, but unmapping all of the subregion
    EXPECT_EQ(mx_vmar_unmap(vmar, subregion_addr, 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, subregion_addr, 0b0000'0000'0000, 12), "");
    // Try to map in the subregion again, should fail due to being destroyed
    ASSERT_EQ(mx_vmar_map(subregion, PAGE_SIZE, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              ERR_BAD_STATE, "");
    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can unmap multiple ranges simultaneously
bool unmap_base_not_mapped_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    uintptr_t mapping_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t mapping_size = 4 * PAGE_SIZE;
    ASSERT_EQ(mx_vmo_create(mapping_size, 0, &vmo), NO_ERROR, "");

    ASSERT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_unmap(vmar, mapping_addr - PAGE_SIZE, mapping_size + PAGE_SIZE),
              NO_ERROR, "");

    // Try again, but this time with a mapping below where base is
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr),
              NO_ERROR, "");
    for (size_t gap = PAGE_SIZE; gap < 3 * PAGE_SIZE; gap += PAGE_SIZE) {
        ASSERT_EQ(mx_vmar_map(vmar, mapping_size + gap, vmo, 0, mapping_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                              &mapping_addr),
                  NO_ERROR, "");
        ASSERT_EQ(mx_vmar_unmap(vmar, mapping_addr - PAGE_SIZE, mapping_size + PAGE_SIZE),
                  NO_ERROR, "");
    }

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can overwrite subranges and multiple ranges simultaneously
bool map_specific_overwrite_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo, vmo2;
    mx_handle_t subregion;
    uintptr_t mapping_addr[2];
    uintptr_t subregion_addr;
    uint8_t buf[1];
    size_t len;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    const size_t mapping_size = 4 * PAGE_SIZE;
    ASSERT_EQ(mx_vmo_create(mapping_size * 2, 0, &vmo), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_create(mapping_size * 2, 0, &vmo2), NO_ERROR, "");

    // Tag each page of the VMOs so we can identify which mappings are from
    // which.
    for (size_t i = 0; i < mapping_size / PAGE_SIZE; ++i) {
        buf[0] = 1;
        ASSERT_EQ(mx_vmo_write(vmo, buf, i * PAGE_SIZE, 1, &len), NO_ERROR, "");
        buf[0] = 2;
        ASSERT_EQ(mx_vmo_write(vmo2, buf, i * PAGE_SIZE, 1, &len), NO_ERROR, "");
    }

    // Create a single mapping and overwrite it
    ASSERT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    // Try over mapping with SPECIFIC but not SPECIFIC_OVERWRITE
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo2, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC, &mapping_addr[1]),
              ERR_NO_MEMORY, "");
    // Try again with SPECIFIC_OVERWRITE
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo2, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC_OVERWRITE, &mapping_addr[1]),
              NO_ERROR, "");
    EXPECT_EQ(mapping_addr[0], mapping_addr[1], "");
    for (size_t i = 0; i < mapping_size / PAGE_SIZE; ++i) {
        EXPECT_EQ(mx_process_read_memory(process, mapping_addr[0] + i * PAGE_SIZE, buf, 1, &len),
                  NO_ERROR, "");
        EXPECT_EQ(buf[0], 2u, "");
    }

    // Overmap the middle of it
    EXPECT_EQ(mx_vmar_map(vmar, 2 * PAGE_SIZE, vmo, 0, 2 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC_OVERWRITE, &mapping_addr[0]),
              NO_ERROR, "");
    EXPECT_EQ(mapping_addr[0], mapping_addr[1] + PAGE_SIZE, "");
    for (size_t i = 0; i < mapping_size / PAGE_SIZE; ++i) {
        EXPECT_EQ(mx_process_read_memory(process, mapping_addr[1] + i * PAGE_SIZE, buf, 1, &len),
                  NO_ERROR, "");
        EXPECT_EQ(buf[0], (i == 0 || i == 3) ? 2u : 1u, "");
    }

    // Create an adjacent sub-region, try to overmap it
    ASSERT_EQ(mx_vmar_allocate(vmar, PAGE_SIZE + mapping_size, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    EXPECT_EQ(subregion_addr, mapping_addr[1] + mapping_size, "");
    EXPECT_EQ(mx_vmar_map(vmar, PAGE_SIZE, vmo2, 0, 2 * mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                          MX_VM_FLAG_SPECIFIC_OVERWRITE, &mapping_addr[0]),
              ERR_INVALID_ARGS, "");
    // Tear it all down
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[1], 2 * mapping_size),
              NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can split a single mapping with a protect call
bool protect_split_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo;
    uintptr_t mapping_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");

    ASSERT_EQ(mx_vmo_create(4 * PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    // Protect from the left
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &mapping_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr, 2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              NO_ERROR, "");
    // TODO(teisenbe): Test to validate perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr, 4 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4), "");

    // Protect from the right
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &mapping_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr + 2 * PAGE_SIZE,
                              2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              NO_ERROR, "");
    // TODO(teisenbe): Test to validate perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr, 4 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4), "");

    // Protect from the center
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, 4 * PAGE_SIZE,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &mapping_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr + PAGE_SIZE,
                              2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              NO_ERROR, "");
    // TODO(teisenbe): Test to validate perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b1111, 4), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr, 4 * PAGE_SIZE), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr, 0b0000, 4), "");

    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Validate that protect can be used across multiple mappings.  Make sure intersecting a subregion
// or gap fails
bool protect_multiple_test() {
    BEGIN_TEST;

    mx_handle_t process;
    mx_handle_t vmar;
    mx_handle_t vmo, vmo2;
    mx_handle_t subregion;
    uintptr_t mapping_addr[3];
    uintptr_t subregion_addr;

    ASSERT_EQ(mx_process_create(mx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar), NO_ERROR, "");
    const size_t mapping_size = 4 * PAGE_SIZE;
    ASSERT_EQ(mx_vmo_create(mapping_size, 0, &vmo), NO_ERROR, "");
    ASSERT_EQ(mx_handle_duplicate(vmo, MX_RIGHT_MAP | MX_RIGHT_READ, &vmo2), NO_ERROR, "");

    // Protect from the right on the first mapping, all of the second mapping,
    // and from the left on the third mapping.
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[1]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, 2 * mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr[0] + PAGE_SIZE,
                              3 * mapping_size - 2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              NO_ERROR, "");
    // TODO(teisenbe): Test to validate perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");

    // Same thing, but map middle region with a VMO without the WRITE right
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, mapping_size, vmo2, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[1]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, 2 * mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr[0] + PAGE_SIZE,
                              3 * mapping_size - 2 * PAGE_SIZE,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE),
              ERR_ACCESS_DENIED, "");
    // TODO(teisenbe): Test to validate no perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");

    // Try to protect across a gap
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, 2 * mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr[0] + PAGE_SIZE,
                              3 * mapping_size - 2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              ERR_NOT_FOUND, "");
    // TODO(teisenbe): Test to validate no perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0000'1111, 12), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");

    // Try to protect across an empty subregion
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_allocate(vmar, mapping_size, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, 2 * mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr[0] + PAGE_SIZE,
                              3 * mapping_size - 2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    // TODO(teisenbe): Test to validate no perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'0000'1111, 12), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");
    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    // Try to protect across a subregion filled with mappings
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[0]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_allocate(vmar, mapping_size, mapping_size,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                               MX_VM_FLAG_SPECIFIC | MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &subregion, &subregion_addr),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(subregion, 0, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[1]),
              NO_ERROR, "");
    ASSERT_EQ(mx_vmar_map(vmar, 2 * mapping_size, vmo, 0, mapping_size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &mapping_addr[2]),
              NO_ERROR, "");
    EXPECT_EQ(mx_vmar_protect(vmar, mapping_addr[0] + PAGE_SIZE,
                              3 * mapping_size - 2 * PAGE_SIZE, MX_VM_FLAG_PERM_READ),
              ERR_INVALID_ARGS, "");
    // TODO(teisenbe): Test to validate no perms changed, need to export more debug
    // info
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b1111'1111'1111, 12), "");
    EXPECT_EQ(mx_vmar_unmap(vmar, mapping_addr[0], 3 * mapping_size), NO_ERROR, "");
    EXPECT_TRUE(check_pages_mapped(process, mapping_addr[0], 0b0000'0000'0000, 12), "");
    EXPECT_EQ(mx_handle_close(subregion), NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo2), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmar), NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(process), NO_ERROR, "");

    END_TEST;
}

// Verify that we can change protections on a demand paged mapping successfully.
bool protect_over_demand_paged_test() {
    BEGIN_TEST;

    mx_handle_t vmo;
    const size_t size = 100 * PAGE_SIZE;
    ASSERT_EQ(mx_vmo_create(size, 0, &vmo), NO_ERROR, "");

    // TODO(teisenbe): Move this into a separate process; currently we don't
    // have an easy way to run a small test routine in another process.
    uintptr_t mapping_addr;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &mapping_addr),
              NO_ERROR, "");
    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");

    volatile uint8_t* target = reinterpret_cast<volatile uint8_t*>(mapping_addr);
    target[0] = 5;
    target[size / 2] = 6;
    target[size - 1] = 7;

    ASSERT_EQ(mx_vmar_protect(mx_vmar_root_self(), mapping_addr, size,
                              MX_VM_FLAG_PERM_READ),
              NO_ERROR, "");

    // Attempt to write to the mapping again
    bool success;
    EXPECT_EQ(test_local_address(mapping_addr, true, &success), NO_ERROR, "");
    EXPECT_FALSE(success, "mapping should no longer be writeable");
    EXPECT_EQ(test_local_address(mapping_addr + size / 4, true, &success), NO_ERROR, "");
    EXPECT_FALSE(success, "mapping should no longer be writeable");
    EXPECT_EQ(test_local_address(mapping_addr + size / 2, true, &success), NO_ERROR, "");
    EXPECT_FALSE(success, "mapping should no longer be writeable");
    EXPECT_EQ(test_local_address(mapping_addr + size - 1, true, &success), NO_ERROR, "");
    EXPECT_FALSE(success, "mapping should no longer be writeable");

    EXPECT_EQ(mx_vmar_unmap(mx_vmar_root_self(), mapping_addr, size), NO_ERROR, "");

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
RUN_TEST(map_in_compact_test);
RUN_TEST(overmapping_test);
RUN_TEST(invalid_args_test);
RUN_TEST(unaligned_len_test);
RUN_TEST(rights_drop_test);
RUN_TEST(protect_test);
RUN_TEST(nested_region_perms_test);
RUN_TEST(object_info_test);
RUN_TEST(unmap_split_test);
RUN_TEST(unmap_multiple_test);
RUN_TEST(unmap_base_not_mapped_test);
RUN_TEST(map_specific_overwrite_test);
RUN_TEST(protect_split_test);
RUN_TEST(protect_multiple_test);
RUN_TEST(protect_over_demand_paged_test);
END_TEST_CASE(vmar_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
