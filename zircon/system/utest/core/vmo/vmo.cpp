// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <lib/fzl/memory-probe.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

namespace {

bool vmo_create_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo[16];

    // allocate a bunch of vmos then free them
    for (size_t i = 0; i < fbl::count_of(vmo); i++) {
        status = zx_vmo_create(i * PAGE_SIZE, 0, &vmo[i]);
        EXPECT_EQ(ZX_OK, status, "vm_object_create");
    }

    for (size_t i = 0; i < fbl::count_of(vmo); i++) {
        status = zx_handle_close(vmo[i]);
        EXPECT_EQ(ZX_OK, status, "handle_close");
    }

    END_TEST;
}

bool vmo_read_write_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE * 4;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(status, ZX_OK, "vm_object_create");

    char buf[len];
    status = zx_vmo_read(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ(status, ZX_OK, "vm_object_read");

    // make sure it's full of zeros
    size_t count = 0;
    for (auto c: buf) {
        EXPECT_EQ(c, 0, "zero test");
        if (c != 0) {
            printf("char at offset %#zx is bad\n", count);
        }
        count++;
    }

    memset(buf, 0x99, sizeof(buf));
    status = zx_vmo_write(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ(status, ZX_OK, "vm_object_write");

    // map it
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(),
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                        len, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    // check that it matches what we last wrote into it
    EXPECT_BYTES_EQ((uint8_t*)buf, (uint8_t*)ptr, sizeof(buf), "mapped buffer");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_read_write_range_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object
    const size_t len = PAGE_SIZE * 4;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(status, ZX_OK, "vm_object_create");

    // fail to read past end
    char buf[len * 2];
    status = zx_vmo_read(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read past end");

    // Successfully read 0 bytes at end
    status = zx_vmo_read(vmo, buf, len, 0);
    EXPECT_EQ(status, ZX_OK, "vm_object_read zero at end");

    // Fail to read 0 bytes past end
    status = zx_vmo_read(vmo, buf, len + 1, 0);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read zero past end");

    // fail to write past end
    status = zx_vmo_write(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write past end");

    // Successfully write 0 bytes at end
    status = zx_vmo_write(vmo, buf, len, 0);
    EXPECT_EQ(status, ZX_OK, "vm_object_write zero at end");

    // Fail to read 0 bytes past end
    status = zx_vmo_write(vmo, buf, len + 1, 0);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write zero past end");

    // Test for unsigned wraparound
    status = zx_vmo_read(vmo, buf, UINT64_MAX - (len / 2), len);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read offset + len wraparound");
    status = zx_vmo_write(vmo, buf, UINT64_MAX - (len / 2), len);
    EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write offset + len wraparound");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_map_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;
    uintptr_t ptr[3] = {};

    // allocate a vmo
    status = zx_vmo_create(4 * PAGE_SIZE, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // do a regular map
    ptr[0] = 0;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0,
                         PAGE_SIZE, &ptr[0]);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr[0], "map address");
    //printf("mapped %#" PRIxPTR "\n", ptr[0]);

    // try to map something completely out of range without any fixed mapping, should succeed
    ptr[2] = UINTPTR_MAX;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0,
                         PAGE_SIZE, &ptr[2]);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr[2], "map address");

    // try to map something completely out of range fixed, should fail
    uintptr_t map_addr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_SPECIFIC,
                         UINTPTR_MAX, vmo, 0, PAGE_SIZE, &map_addr);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "map");

    // cleanup
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    for (auto p: ptr) {
        if (p) {
            status = zx_vmar_unmap(zx_vmar_root_self(), p, PAGE_SIZE);
            EXPECT_EQ(ZX_OK, status, "unmap");
        }
    }

    END_TEST;
}

bool vmo_read_only_map_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // map it
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0,
                         len, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    EXPECT_EQ(false, probe_for_write((void*)ptr), "write");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_no_perm_map_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // map it with read permissions
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, len, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    // protect it to no permissions
    status = zx_vmar_protect(zx_vmar_root_self(), 0, ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_protect");

    // test reading writing to the mapping
    EXPECT_EQ(false, probe_for_read(reinterpret_cast<void*>(ptr)), "read");
    EXPECT_EQ(false, probe_for_write(reinterpret_cast<void*>(ptr)), "write");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    END_TEST;
}

bool vmo_no_perm_protect_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // map it with no permissions
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), 0, 0, vmo, 0, len, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    // test writing to the mapping
    EXPECT_EQ(false, probe_for_write(reinterpret_cast<void*>(ptr)), "write");
    // test reading to the mapping
    EXPECT_EQ(false, probe_for_read(reinterpret_cast<void*>(ptr)), "read");

    // protect it to read permissions and make sure it works as expected
    status = zx_vmar_protect(zx_vmar_root_self(), ZX_VM_PERM_READ, ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_protect");

    // test writing to the mapping
    EXPECT_EQ(false, probe_for_write(reinterpret_cast<void*>(ptr)), "write");

    // test reading from the mapping
    EXPECT_EQ(true, probe_for_read(reinterpret_cast<void*>(ptr)), "read");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    END_TEST;
}

bool vmo_resize_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object
    size_t len = PAGE_SIZE * 4;
    status = zx_vmo_create(len, ZX_VMO_RESIZABLE, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // get the size that we set it to
    uint64_t size = 0x99999999;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

    // try to resize it
    len += PAGE_SIZE;
    status = zx_vmo_set_size(vmo, len);
    EXPECT_EQ(ZX_OK, status, "vm_object_set_size");

    // get the size again
    size = 0x99999999;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

    // try to resize it to a ludicrous size
    status = zx_vmo_set_size(vmo, UINT64_MAX);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "vm_object_set_size too big");

    // resize it to a non aligned size
    status = zx_vmo_set_size(vmo, len + 1);
    EXPECT_EQ(ZX_OK, status, "vm_object_set_size");

    // size should be rounded up to the next page boundary
    size = 0x99999999;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
    EXPECT_EQ(fbl::round_up(len + 1u, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");
    len = fbl::round_up(len + 1u, static_cast<size_t>(PAGE_SIZE));

    // map it
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0,
                         len, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(ptr, 0, "vm_map");

    // attempt to map expecting an non resizable vmo.
    uintptr_t ptr2;
    status = zx_vmar_map(
        zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_REQUIRE_NON_RESIZABLE, 0,
        vmo, 0, len, &ptr2);
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status, "vm_map");

    // resize it with it mapped
    status = zx_vmo_set_size(vmo, size);
    EXPECT_EQ(ZX_OK, status, "vm_object_set_size");

    // unmap it
    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "unmap");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

// Check that non-resizable VMOs cannot get resized.
bool vmo_no_resize_test() {
    BEGIN_TEST;

    const size_t len = PAGE_SIZE * 4;
    zx_handle_t vmo = ZX_HANDLE_INVALID;

    zx_vmo_create(len, ZX_VMO_NON_RESIZABLE, &vmo);

    EXPECT_NE(vmo, ZX_HANDLE_INVALID);

    zx_status_t status;
    status = zx_vmo_set_size(vmo, len + PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "vm_object_set_size");

    status = zx_vmo_set_size(vmo, len - PAGE_SIZE);
    EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "vm_object_set_size");

    size_t size;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

    uintptr_t ptr;
    status = zx_vmar_map(
        zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE,
        0, vmo, 0, len,
        &ptr);
    ASSERT_EQ(ZX_OK, status, "vm_map");
    ASSERT_NE(ptr, 0, "vm_map");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "unmap");

    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_info_test() {
    size_t len = PAGE_SIZE * 4;
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_info_vmo_t info;
    zx_status_t status;

    // Create a non-resizeable VMO, query the INFO on it
    // and dump it.
    status = zx_vmo_create(len, ZX_VMO_NON_RESIZABLE, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: vmo_create");

    status = zx_object_get_info(vmo, ZX_INFO_VMO, &info,
                                sizeof(info), nullptr, nullptr);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: info_vmo");

    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: handle_close");

    EXPECT_EQ(info.size_bytes, len, "vm_info_test: info_vmo.size_bytes");
    EXPECT_NE(info.create_options, (1u << 0),
              "vm_info_test: info_vmo.create_options");
    EXPECT_EQ(info.cache_policy,
              ZX_CACHE_POLICY_CACHED,
              "vm_info_test: info_vmo.cache_policy");
//    printf("NON_Resizeable VMO, size = %lu, create_options = %ux\n",
//           info.size_bytes, info.create_options);

    // Create a resizeable uncached VMO, query the INFO on it and dump it.
    len = PAGE_SIZE * 8;
    zx_vmo_create(len, ZX_VMO_RESIZABLE, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: vmo_create");
    zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED);

    status = zx_object_get_info(vmo, ZX_INFO_VMO, &info,
                                sizeof(info), nullptr, nullptr);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: info_vmo");

    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "vm_info_test: handle_close");

    EXPECT_EQ(info.size_bytes, len, "vm_info_test: info_vmo.size_bytes");
    EXPECT_EQ(info.create_options, (1u << 0),
              "vm_info_test: info_vmo.create_options");
    EXPECT_EQ(info.cache_policy,
              ZX_CACHE_POLICY_UNCACHED,
              "vm_info_test: info_vmo.cache_policy");
//    printf("Resizeable VMO, size = %lu, create_options = %ux\n",
//           info.size_bytes, info.create_options);

    END_TEST;
}

bool vmo_size_align_test() {
    BEGIN_TEST;

    for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
        zx_handle_t vmo;

        // create a new object with nonstandard size
        zx_status_t status = zx_vmo_create(s, 0, &vmo);
        EXPECT_EQ(ZX_OK, status, "vm_object_create");

        // should be the size rounded up to the nearest page boundary
        uint64_t size = 0x99999999;
        status = zx_vmo_get_size(vmo, &size);
        EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
        EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");

        // close the handle
        EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    }

    END_TEST;
}

bool vmo_resize_align_test() {
    BEGIN_TEST;

    // resize a vmo with a particular size and test that the resulting size is aligned on a page
    // boundary.
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(0, ZX_VMO_RESIZABLE, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
        // set the size of the object
        zx_status_t status = zx_vmo_set_size(vmo, s);
        EXPECT_EQ(ZX_OK, status, "vm_object_create");

        // should be the size rounded up to the nearest page boundary
        uint64_t size = 0x99999999;
        status = zx_vmo_get_size(vmo, &size);
        EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
        EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");
    }

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    END_TEST;
}

static bool rights_test_map_helper(
    zx_handle_t vmo, size_t len, uint32_t flags,
    bool expect_success, zx_status_t fail_err_code, const char *msg) {
    uintptr_t ptr;

    zx_status_t r = zx_vmar_map(zx_vmar_root_self(), flags, 0, vmo, 0, len,
                                &ptr);
    if (expect_success) {
        EXPECT_EQ(ZX_OK, r, msg);

        r = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
        EXPECT_EQ(ZX_OK, r, "unmap");
    } else {
        EXPECT_EQ(fail_err_code, r, msg);
    }

    return true;
}

static zx_rights_t get_handle_rights(zx_handle_t h) {
    zx_info_handle_basic_t info;
    zx_status_t s = zx_object_get_info(h, ZX_INFO_HANDLE_BASIC, &info,
                                       sizeof(info), nullptr, nullptr);
    if (s != ZX_OK) {
        EXPECT_EQ(s, ZX_OK);  // Poison the test
        return 0;
    }
    return info.rights;
}

bool vmo_rights_test() {
    BEGIN_TEST;

    char buf[4096];
    size_t len = PAGE_SIZE * 4;
    zx_status_t status;
    zx_handle_t vmo, vmo2;

    // allocate an object
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // Check that the handle has at least the expected rights.
    // This list should match the list in docs/syscalls/vmo_create.md.
    static const zx_rights_t kExpectedRights =
        ZX_RIGHT_DUPLICATE |
        ZX_RIGHT_TRANSFER |
        ZX_RIGHT_WAIT |
        ZX_RIGHT_READ |
        ZX_RIGHT_WRITE |
        ZX_RIGHT_MAP |
        ZX_RIGHT_GET_PROPERTY |
        ZX_RIGHT_SET_PROPERTY;
    EXPECT_EQ(kExpectedRights, kExpectedRights & get_handle_rights(vmo));

    // test that we can read/write it
    status = zx_vmo_read(vmo, buf, 0, 0);
    EXPECT_EQ(0, status, "vmo_read");
    status = zx_vmo_write(vmo, buf, 0, 0);
    EXPECT_EQ(0, status, "vmo_write");

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ, &vmo2);
    status = zx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(0, status, "vmo_read");
    status = zx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_write");
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_WRITE, &vmo2);
    status = zx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_read");
    status = zx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(0, status, "vmo_write");
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, 0, &vmo2);
    status = zx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_read");
    status = zx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_write");
    zx_handle_close(vmo2);

    status = zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo);
    EXPECT_EQ(ZX_OK, status, "vmo_replace_as_executable");
    EXPECT_EQ(kExpectedRights | ZX_RIGHT_EXECUTE, (kExpectedRights | ZX_RIGHT_EXECUTE) & get_handle_rights(vmo));

    // full perm test
    if (!rights_test_map_helper(vmo, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, true, 0, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0, "map_readexec")) return false;

    // try most of the permutations of mapping a vmo with various rights dropped
    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, false, ZX_ERR_ACCESS_DENIED, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, false, ZX_ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, false, ZX_ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, true, 0, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0, "map_readexec")) return false;
    zx_handle_close(vmo2);

    // test that we can get/set a property on it
    const char *set_name = "test vmo";
    status = zx_object_set_property(vmo, ZX_PROP_NAME, set_name, sizeof(set_name));
    EXPECT_EQ(ZX_OK, status, "set_property");
    char get_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(vmo, ZX_PROP_NAME, get_name, sizeof(get_name));
    EXPECT_EQ(ZX_OK, status, "get_property");
    EXPECT_STR_EQ(set_name, get_name, "vmo name");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    // Use wrong handle with wrong permission, and expect wrong type not
    // ZX_ERR_ACCESS_DENIED
    vmo = ZX_HANDLE_INVALID;
    vmo2 = ZX_HANDLE_INVALID;
    status = zx_port_create(0, &vmo);
    EXPECT_EQ(ZX_OK, status, "zx_port_create");
    status = zx_handle_duplicate(vmo, 0, &vmo2);
    EXPECT_EQ(ZX_OK, status, "zx_handle_duplicate");
    status = zx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(ZX_ERR_WRONG_TYPE, status, "vmo_read wrong type");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");
    status = zx_handle_close(vmo2);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_commit_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_status_t status;
    uintptr_t ptr, ptr2, ptr3;

    // create a vmo
    const size_t size = 16384;

    status = zx_vmo_create(size, 0, &vmo);
    EXPECT_EQ(0, status, "vm_object_create");

    // commit a range of it
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm commit");

    // decommit that range
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // commit a range of it
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm commit");

    // map it
    ptr = 0;
    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ|ZX_VM_PERM_WRITE,
                         0, vmo, 0, size, &ptr);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(ptr, 0, "map address");

    // second mapping with an offset
    ptr2 = 0;
    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ|ZX_VM_PERM_WRITE,
                         0, vmo, PAGE_SIZE, size, &ptr2);
    EXPECT_EQ(ZX_OK, status, "map2");
    EXPECT_NE(ptr2, 0, "map address2");

    // third mapping with a totally non-overlapping offset
    ptr3 = 0;
    status = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ|ZX_VM_PERM_WRITE,
                         0, vmo, size * 2, size, &ptr3);
    EXPECT_EQ(ZX_OK, status, "map3");
    EXPECT_NE(ptr3, 0, "map address3");

    // write into it at offset PAGE_SIZE, read it back
    volatile uint32_t *u32 = (volatile uint32_t *)(ptr + PAGE_SIZE);
    *u32 = 99;
    EXPECT_EQ(99u, (*u32), "written memory");

    // check the alias
    volatile uint32_t *u32a = (volatile uint32_t *)(ptr2);
    EXPECT_EQ(99u, (*u32a), "written memory");

    // decommit page 0
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // verify that it didn't get unmapped
    EXPECT_EQ(99u, (*u32), "written memory");
    // verify that it didn't get unmapped
    EXPECT_EQ(99u, (*u32a), "written memory2");

    // decommit page 1
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, PAGE_SIZE, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // verify that it did get unmapped
    EXPECT_EQ(0u, (*u32), "written memory");
    // verify that it did get unmapped
    EXPECT_EQ(0u, (*u32a), "written memory2");

    // unmap our vmos
    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, size);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");
    status = zx_vmar_unmap(zx_vmar_root_self(), ptr2, size);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");
    status = zx_vmar_unmap(zx_vmar_root_self(), ptr3, size);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");

    END_TEST;
}

bool vmo_zero_page_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_status_t status;
    uintptr_t ptr[3];

    // create a vmo
    const size_t size = PAGE_SIZE * 4;

    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // make a few mappings of the vmo
    for (auto &p: ptr) {
        EXPECT_EQ(ZX_OK,
                zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, vmo, 0, size, &p),
                "map");
        EXPECT_NONNULL(ptr, "map address");
    }

    volatile uint32_t *val = (volatile uint32_t *)ptr[0];
    volatile uint32_t *val2 = (volatile uint32_t *)ptr[1];
    volatile uint32_t *val3 = (volatile uint32_t *)ptr[2];

    // read fault in the first mapping
    EXPECT_EQ(0, *val, "read zero");

    // write fault the second mapping
    *val2 = 99;
    EXPECT_EQ(99, *val2, "read back 99");

    // expect the third mapping to read fault in the new page
    EXPECT_EQ(99, *val3, "read 99");

    // expect the first mapping to have gotten updated with the new mapping
    // and no longer be mapping the zero page
    EXPECT_EQ(99, *val, "read 99 from former zero page");

    // read fault in zeros on the second page
    val = (volatile uint32_t *)(ptr[0] + PAGE_SIZE);
    EXPECT_EQ(0, *val, "read zero");

    // write to the page via a vmo_write call
    uint32_t v = 100;
    status = zx_vmo_write(vmo, &v, PAGE_SIZE, sizeof(v));
    EXPECT_EQ(ZX_OK, status, "writing to vmo");

    // expect it to read back the new value
    EXPECT_EQ(100, *val, "read 100 from former zero page");

    // read fault in zeros on the third page
    val = (volatile uint32_t *)(ptr[0] + PAGE_SIZE * 2);
    EXPECT_EQ(0, *val, "read zero");

    // commit this range of the vmo via a commit call
    status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, PAGE_SIZE * 2, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(ZX_OK, status, "committing memory");

    // write to the third page
    status = zx_vmo_write(vmo, &v, PAGE_SIZE * 2, sizeof(v));
    EXPECT_EQ(ZX_OK, status, "writing to vmo");

    // expect it to read back the new value
    EXPECT_EQ(100, *val, "read 100 from former zero page");

    // unmap
    for (auto p: ptr)
        EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), p, size), "unmap");

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    END_TEST;
}

bool vmo_cache_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    const size_t size = PAGE_SIZE;

    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "creation for cache_policy");

    // clean vmo can have all valid cache policies set
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED_DEVICE));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_WRITE_COMBINING));

    // bad cache policy
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_MASK + 1));

    // commit a page, make sure the policy doesn't set
    EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, size, nullptr, 0));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

    // map the vmo, make sure policy doesn't set
    uintptr_t ptr;
    EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &ptr));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

    // clone the vmo, make sure policy doesn't set
    zx_handle_t clone;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

    // clone the vmo, try to set policy on the clone
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(clone, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone));

    // set the policy, make sure future clones do not go through
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone));

    // set the policy, make sure vmo read/write do not work
    char c;
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_read(vmo, &c, 0, sizeof(c)));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_write(vmo, &c, 0, sizeof(c)));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_read(vmo, &c, 0, sizeof(c)));
    EXPECT_EQ(ZX_OK, zx_vmo_write(vmo, &c, 0, sizeof(c)));

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "close handle");
    END_TEST;
}

bool vmo_cache_op_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    const size_t size = 0x8000;

    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "creation for cache op");

    auto t = [vmo](uint32_t op) {
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0, 1, nullptr, 0), "0 1");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0, 1, nullptr, 0), "0 1");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 1, 1, nullptr, 0), "1 1");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0, size, nullptr, 0), "0 size");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 1, size - 1, nullptr, 0), "0 size");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0x5200, 1, nullptr, 0), "0x5200 1");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0x5200, 0x800, nullptr, 0), "0x5200 0x800");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0x5200, 0x1000, nullptr, 0), "0x5200 0x1000");
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, op, 0x5200, 0x1200, nullptr, 0), "0x5200 0x1200");

        EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_op_range(vmo, op, 0, 0, nullptr, 0), "0 0");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, 1, size, nullptr, 0), "0 size");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, size, 1, nullptr, 0), "size 1");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, size+1, 1, nullptr, 0), "size+1 1");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX-1, 1, nullptr, 0), "UINT64_MAX-1 1");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX, 1, nullptr, 0), "UINT64_MAX 1");
        EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX, UINT64_MAX, nullptr, 0), "UINT64_MAX UINT64_MAX");
    };

    t(ZX_VMO_OP_CACHE_SYNC);
    t(ZX_VMO_OP_CACHE_CLEAN);
    t(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE);
    t(ZX_VMO_OP_CACHE_INVALIDATE);

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "close handle");
    END_TEST;
}

bool vmo_cache_flush_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    const size_t size = 0x8000;

    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "creation for cache op");

    uintptr_t ptr_ro;
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &ptr_ro),
            "map");
    EXPECT_NE(ptr_ro, 0, "map address");
    void *pro = (void*)ptr_ro;

    uintptr_t ptr_rw;
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr_rw),
            "map");
    EXPECT_NE(ptr_rw, 0, "map address");
    void *prw = (void*)ptr_rw;

    zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, NULL, 0);

    EXPECT_EQ(ZX_OK, zx_cache_flush(prw, size, ZX_CACHE_FLUSH_INSN), "rw flush insn");
    EXPECT_EQ(ZX_OK, zx_cache_flush(prw, size, ZX_CACHE_FLUSH_DATA), "rw clean");
    EXPECT_EQ(ZX_OK, zx_cache_flush(prw, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE), "rw clean/invalidate");

    EXPECT_EQ(ZX_OK, zx_cache_flush(pro, size, ZX_CACHE_FLUSH_INSN), "ro flush insn");
    EXPECT_EQ(ZX_OK, zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA), "ro clean");
    EXPECT_EQ(ZX_OK, zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE), "ro clean/invalidate");

    zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size);
    zx_vmar_unmap(zx_vmar_root_self(), ptr_ro, size);
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "close handle");
    END_TEST;
}

bool vmo_decommit_misaligned_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    EXPECT_EQ(ZX_OK, zx_vmo_create(PAGE_SIZE * 2, 0, &vmo), "creation for decommit test");

    zx_status_t status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0x10, 0x100, NULL, 0);
    EXPECT_EQ(ZX_OK, status, "decommitting uncommitted memory");

    status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0x10, 0x100, NULL, 0);
    EXPECT_EQ(ZX_OK, status, "committing memory");

    status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0x10, 0x100, NULL, 0);
    EXPECT_EQ(ZX_OK, status, "decommitting memory");

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "close handle");
    END_TEST;
}

// Resizing a regular mapped VMO causes a fault.
bool vmo_resize_hazard() {
    BEGIN_TEST;

    const size_t size = PAGE_SIZE * 2;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(size, ZX_VMO_RESIZABLE, &vmo), ZX_OK);

    uintptr_t ptr_rw;
    EXPECT_EQ(ZX_OK, zx_vmar_map(
            zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
            0, vmo, 0, size, &ptr_rw), "map");

    auto int_arr = reinterpret_cast<int*>(ptr_rw);
    EXPECT_EQ(int_arr[1], 0);

    EXPECT_EQ(ZX_OK, zx_vmo_set_size(vmo, 0u));

    EXPECT_EQ(false, probe_for_read(&int_arr[1]), "read probe");
    EXPECT_EQ(false, probe_for_write(&int_arr[1]), "write probe");

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo));
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size), "unmap");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST(vmo_create_test);
RUN_TEST(vmo_read_write_test);
RUN_TEST(vmo_read_write_range_test);
RUN_TEST(vmo_map_test);
RUN_TEST(vmo_read_only_map_test);
RUN_TEST(vmo_no_perm_map_test);
RUN_TEST(vmo_no_perm_protect_test);
RUN_TEST(vmo_resize_test);
RUN_TEST(vmo_no_resize_test);
RUN_TEST(vmo_size_align_test);
RUN_TEST(vmo_resize_align_test);
RUN_TEST(vmo_rights_test);
RUN_TEST(vmo_commit_test);
RUN_TEST(vmo_decommit_misaligned_test);
RUN_TEST(vmo_cache_test);
RUN_TEST(vmo_cache_op_test);
RUN_TEST(vmo_cache_flush_test);
RUN_TEST(vmo_zero_page_test);
RUN_TEST(vmo_resize_hazard);
RUN_TEST(vmo_info_test);
END_TEST_CASE(vmo_tests)
