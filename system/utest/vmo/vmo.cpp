// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <threads.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <fbl/algorithm.h>
#include <fbl/atomic.h>
#include <fbl/function.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

#include "bench.h"

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
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr);
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
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, PAGE_SIZE,
                         ZX_VM_FLAG_PERM_READ, &ptr[0]);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr[0], "map address");
    //printf("mapped %#" PRIxPTR "\n", ptr[0]);

    // try to map something completely out of range without any fixed mapping, should succeed
    ptr[2] = UINTPTR_MAX;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, PAGE_SIZE,
                         ZX_VM_FLAG_PERM_READ, &ptr[2]);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr[2], "map address");

    // try to map something completely out of range fixed, should fail
    uintptr_t map_addr;
    status = zx_vmar_map(zx_vmar_root_self(), UINTPTR_MAX,
                         vmo, 0, PAGE_SIZE,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_SPECIFIC, &map_addr);
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
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len,
                         ZX_VM_FLAG_PERM_READ, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    auto sstatus = zx_cprng_draw_new((void*)ptr, 1);
    EXPECT_LT(sstatus, 0, "write");

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
    zx_handle_t channel[2];

    // create a channel for testing read permissions via syscall failure
    status = zx_channel_create(0, &channel[0], &channel[1]);
    EXPECT_EQ(ZX_OK, status, "vm_channel_create");

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // map it with read permissions
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len, ZX_VM_FLAG_PERM_READ, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    // protect it to no permissions
    status = zx_vmar_protect(zx_vmar_root_self(), ptr, len, 0);
    EXPECT_EQ(ZX_OK, status, "vm_protect");

    // test writing to the mapping
    status = zx_cprng_draw_new(reinterpret_cast<void*>(ptr), 1);
    EXPECT_NE(status, ZX_OK, "write");

    // test reading from the mapping
    status = zx_channel_write(channel[0], 0, reinterpret_cast<void*>(ptr), 1, nullptr, 0);
    EXPECT_NE(status, ZX_OK, "read");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    EXPECT_EQ(ZX_OK, zx_handle_close(channel[0]), "handle_close");
    EXPECT_EQ(ZX_OK, zx_handle_close(channel[1]), "handle_close");

    END_TEST;
}

bool vmo_no_perm_protect_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;
    zx_handle_t channel[2];

    // create a channel for testing read permissions via syscall failure
    status = zx_channel_create(0, &channel[0], &channel[1]);
    EXPECT_EQ(ZX_OK, status, "vm_channel_create");

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // map it with no permissions
    uintptr_t ptr;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len, 0, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(0u, ptr, "vm_map");

    // test writing to the mapping
    status = zx_cprng_draw_new(reinterpret_cast<void*>(ptr), 1);
    EXPECT_NE(status, ZX_OK, "write");

    // test reading from the mapping
    status = zx_channel_write(channel[0], 0, reinterpret_cast<void*>(ptr), 1, nullptr, 0);
    EXPECT_NE(status, ZX_OK, "read");

    // protect it to read permissions and make sure it works as expected
    status = zx_vmar_protect(zx_vmar_root_self(), ptr, len, ZX_VM_FLAG_PERM_READ);
    EXPECT_EQ(ZX_OK, status, "vm_protect");

    // test writing to the mapping
    status = zx_cprng_draw_new(reinterpret_cast<void*>(ptr), 1);
    EXPECT_NE(status, ZX_OK, "write");

    // test reading from the mapping
    status = zx_channel_write(channel[0], 0, reinterpret_cast<void*>(ptr), 1, nullptr, 0);
    EXPECT_EQ(status, ZX_OK, "read");

    status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    EXPECT_EQ(ZX_OK, zx_handle_close(channel[0]), "handle_close");
    EXPECT_EQ(ZX_OK, zx_handle_close(channel[1]), "handle_close");

    END_TEST;
}

bool vmo_resize_test() {
    BEGIN_TEST;

    zx_status_t status;
    zx_handle_t vmo;

    // allocate an object
    size_t len = PAGE_SIZE * 4;
    status = zx_vmo_create(len, 0, &vmo);
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
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len,
                         ZX_VM_FLAG_PERM_READ, &ptr);
    EXPECT_EQ(ZX_OK, status, "vm_map");
    EXPECT_NE(ptr, 0, "vm_map");

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

    // resize a vmo with a particular size and test that the resulting size is aligned on a page boundary
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(0, 0, &vmo);
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

bool vmo_clone_size_align_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(0, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // create clones with different sizes, make sure the created size is a multiple of a page size
    for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
        zx_handle_t clone_vmo;
        EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, s, &clone_vmo), "vm_clone");

        // should be the size rounded up to the nearest page boundary
        uint64_t size = 0x99999999;
        zx_status_t status = zx_vmo_get_size(clone_vmo, &size);
        EXPECT_EQ(ZX_OK, status, "vm_object_get_size");
        EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");

        // close the handle
        EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo), "handle_close");
    }

    // close the handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    END_TEST;
}

static bool rights_test_map_helper(zx_handle_t vmo, size_t len, uint32_t flags, bool expect_success, zx_status_t fail_err_code, const char *msg) {
    uintptr_t ptr;

    zx_status_t r = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len, flags,
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

// Returns zero on failure.
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
        ZX_RIGHT_EXECUTE |
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

    // full perm test
    if (!rights_test_map_helper(vmo, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, true, 0, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, true, 0, "map_readexec")) return false;

    // try most of the permuations of mapping a vmo with various rights dropped
    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, false, ZX_ERR_ACCESS_DENIED, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, false, ZX_ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, false, ZX_ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, false, ZX_ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, true, 0, "map_readexec")) return false;
    zx_handle_close(vmo2);

    vmo2 = ZX_HANDLE_INVALID;
    zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, 0, true, 0, "map_noperms")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ, true, 0, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_WRITE, false, ZX_ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, true, 0, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_PERM_EXECUTE, true, 0, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, true, 0, "map_readexec")) return false;
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
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                         ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(ptr, 0, "map address");

    // second mapping with an offset
    ptr2 = 0;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, PAGE_SIZE, size,
                         ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr2);
    EXPECT_EQ(ZX_OK, status, "map2");
    EXPECT_NE(ptr2, 0, "map address2");

    // third mapping with a totally non-overlapping offset
    ptr3 = 0;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, size * 2, size,
                         ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr3);
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
                zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &p),
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

// test set 1: create a few clones, close them
bool vmo_clone_test_1() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo[3];

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");
    EXPECT_EQ(ZX_OK, zx_object_set_property(vmo, ZX_PROP_NAME, "test1", 5), "zx_object_set_property");

    // clone it
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");
    char name[ZX_MAX_NAME_LEN];
    EXPECT_EQ(ZX_OK, zx_object_get_property(clone_vmo[0], ZX_PROP_NAME, name, ZX_MAX_NAME_LEN), "zx_object_get_property");
    EXPECT_TRUE(!strcmp(name, "test1"), "get_name");

    // clone it a second time
    clone_vmo[1] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[1]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[1], "vm_clone_handle");

    // clone the clone
    clone_vmo[2] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(clone_vmo[1], ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[2]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[2], "vm_clone_handle");

    // close the original handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    // close the clone handles
    for (auto h: clone_vmo)
        EXPECT_EQ(ZX_OK, zx_handle_close(h), "handle_close");

    END_TEST;
}

// test set 2: create a clone, verify that it COWs via the read/write interface
bool vmo_clone_test_2() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo[1];
    //uintptr_t ptr;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // fill the original with stuff
    for (size_t off = 0; off < size; off += sizeof(off)) {
        zx_vmo_write(vmo, &off, off, sizeof(off));
    }

    // clone it
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");

    // verify that the clone reads back as the same
    for (size_t off = 0; off < size; off += sizeof(off)) {
        size_t val;

        zx_vmo_read(clone_vmo[0], &val, off, sizeof(val));

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back");
            break;
        }
    }

    // write to part of the clone
    size_t val = 99;
    zx_vmo_write(clone_vmo[0], &val, 0, sizeof(val));

    // verify the clone was written to
    EXPECT_EQ(ZX_OK, zx_vmo_read(clone_vmo[0], &val, 0, sizeof(val)), "writing to clone");

    // verify it was written to
    EXPECT_EQ(99, val, "reading back from clone");

    // verify that the rest of the page it was written two was cloned
    for (size_t off = sizeof(val); off < PAGE_SIZE; off += sizeof(off)) {
        zx_vmo_read(clone_vmo[0], &val, off, sizeof(val));

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back");
            break;
        }
    }

    // verify that it didn't trash the original
    for (size_t off = 0; off < size; off += sizeof(off)) {
        zx_vmo_read(vmo, &val, off, sizeof(val));

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back of original");
            break;
        }
    }

    // write to the original in the part that is still visible to the clone
    val = 99;
    uint64_t offset = PAGE_SIZE * 2;
    EXPECT_EQ(ZX_OK, zx_vmo_write(vmo, &val, offset, sizeof(val)), "writing to original");
    EXPECT_EQ(ZX_OK, zx_vmo_read(clone_vmo[0], &val, offset, sizeof(val)), "reading back original from clone");
    EXPECT_EQ(99, val, "checking value");

    // close the clone handles
    for (auto h: clone_vmo)
        EXPECT_EQ(ZX_OK, zx_handle_close(h), "handle_close");

    // close the original handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    END_TEST;
}

// test set 3: test COW via a mapping
bool vmo_clone_test_3() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo[1];
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile uint32_t *p;
    volatile uint32_t *cp;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, clone_vmo[0], 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NE(clone_ptr, 0, "map address");
    cp = (volatile uint32_t *)clone_ptr;

    // read zeros from both
    for (size_t off = 0; off < size / sizeof(off); off++) {
        size_t val = p[off];

        if (val != 0) {
            EXPECT_EQ(0, val, "reading zeros from original");
            break;
        }
    }
    for (size_t off = 0; off < size / sizeof(off); off++) {
        size_t val = cp[off];

        if (val != 0) {
            EXPECT_EQ(0, val, "reading zeros from original");
            break;
        }
    }

    // write to both sides and make sure it does a COW
    p[0] = 99;
    EXPECT_EQ(99, p[0], "wrote to original");
    EXPECT_EQ(99, cp[0], "read back from clone");
    cp[0] = 100;
    EXPECT_EQ(100, cp[0], "read back from clone");
    EXPECT_EQ(99, p[0], "read back from original");

    // close the original handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    // close the clone handle
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo[0]), "handle_close");

    // unmap
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), clone_ptr, size), "unmap");

    END_TEST;
}

// verify that the parent is visible through decommited pages
bool vmo_clone_decommit_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo;
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile uint32_t *p;
    volatile uint32_t *cp;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, clone_vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NE(clone_ptr, 0, "map address");
    cp = (volatile uint32_t *)clone_ptr;

    // write to parent and make sure clone sees it
    p[0] = 99;
    EXPECT_EQ(99, p[0], "wrote to original");
    EXPECT_EQ(99, cp[0], "read back from clone");

    // write to clone to get a different state
    cp[0] = 100;
    EXPECT_EQ(100, cp[0], "read back from clone");
    EXPECT_EQ(99, p[0], "read back from original");

    EXPECT_EQ(ZX_OK, zx_vmo_op_range(clone_vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, NULL, 0));

    // make sure that clone reverted to original, and that parent is unaffected
    // by the decommit
    EXPECT_EQ(99, cp[0], "read back from clone");
    EXPECT_EQ(99, p[0], "read back from original");

    // make sure the decommited page still has COW semantics
    cp[0] = 100;
    EXPECT_EQ(100, cp[0], "read back from clone");
    EXPECT_EQ(99, p[0], "read back from original");

    // close the original handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    // close the clone handle
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo), "handle_close");

    // unmap
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), clone_ptr, size), "unmap");

    END_TEST;
}

// verify the affect of commit on a clone
bool vmo_clone_commit_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo;
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile uint32_t *p;
    volatile uint32_t *cp;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, clone_vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NE(clone_ptr, 0, "map address");
    cp = (volatile uint32_t *)clone_ptr;

    // write to parent and make sure clone sees it
    memset((void*)p, 0x99, PAGE_SIZE);
    EXPECT_EQ(0x99999999, p[0], "wrote to original");
    EXPECT_EQ(0x99999999, cp[0], "read back from clone");

    EXPECT_EQ(ZX_OK, zx_vmo_op_range(clone_vmo, ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, NULL, 0));

    // make sure that clone has the same contents as the parent
    for (size_t i = 0; i < PAGE_SIZE / sizeof(*p); ++i) {
        EXPECT_EQ(0x99999999, cp[i], "read new page");
    }
    EXPECT_EQ(0x99999999, p[0], "read back from original");

    // write to clone and make sure parent doesn't see it
    cp[0] = 0;
    EXPECT_EQ(0, cp[0], "wrote to clone");
    EXPECT_EQ(0x99999999, p[0], "read back from original");

    EXPECT_EQ(ZX_OK, zx_vmo_op_range(clone_vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, NULL, 0));

    EXPECT_EQ(0x99999999, cp[0], "clone should match orig again");
    EXPECT_EQ(0x99999999, p[0], "read back from original");

    // close the original handle
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");

    // close the clone handle
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo), "handle_close");

    // unmap
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), clone_ptr, size), "unmap");

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
    EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ, &ptr));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

    // clone the vmo, make sure policy doesn't set
    zx_handle_t clone;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

    // clone the vmo, try to set policy on the clone
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(clone, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone));

    // set the policy, make sure future clones do not go through
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
    EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone));
    EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone));
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

bool vmo_cache_map_test() {
    BEGIN_TEST;

    auto maptest = [](uint32_t policy, const char *type) {
        zx_handle_t vmo;
        const size_t size = 256*1024; // 256K

        EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo));

        // set the cache policy
        EXPECT_EQ(ZX_OK, zx_vmo_set_cache_policy(vmo, policy));

        // commit it
        EXPECT_EQ(ZX_OK, zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0));

        // map it
        uintptr_t ptr;
        EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE, &ptr));

        volatile uint32_t *buf = (volatile uint32_t *)ptr;

        // write it once, priming the cache
        for (size_t i = 0; i < size / 4; i++)
            buf[i] = 0;

        // write to it
        zx_time_t wt = zx_clock_get_monotonic();
        for (size_t i = 0; i < size / 4; i++)
            buf[i] = 0;
        wt = zx_clock_get_monotonic() - wt;

        // read from it
        zx_time_t rt = zx_clock_get_monotonic();
        for (size_t i = 0; i < size / 4; i++)
            __UNUSED uint32_t hole = buf[i];
        rt = zx_clock_get_monotonic() - rt;

        printf("took %" PRIu64 " nsec to write %s memory\n", wt, type);
        printf("took %" PRIu64 " nsec to read %s memory\n", rt, type);

        EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size));
        EXPECT_EQ(ZX_OK, zx_handle_close(vmo));
    };

    printf("\n");
    maptest(ZX_CACHE_POLICY_CACHED, "cached");
    maptest(ZX_CACHE_POLICY_UNCACHED, "uncached");
    maptest(ZX_CACHE_POLICY_UNCACHED_DEVICE, "uncached device");
    maptest(ZX_CACHE_POLICY_WRITE_COMBINING, "write combining");

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
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ, &ptr_ro),
            "map");
    EXPECT_NE(ptr_ro, 0, "map address");
    void *pro = (void*)ptr_ro;

    uintptr_t ptr_rw;
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &ptr_rw),
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

// test set 4: deal with clones with nonzero offsets and offsets that extend beyond the original
bool vmo_clone_test_4() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_handle_t clone_vmo[1];
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile size_t *p;
    volatile size_t *cp;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile size_t *)ptr;

    // fill it with stuff
    for (size_t off = 0; off < size / sizeof(off); off++)
        p[off] = off;

    // make sure that non page aligned clones do not work
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, 1, size, &clone_vmo[0]), "vm_clone");

    // create a clone that extends beyond the parent by one page
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_clone(vmo, ZX_VMO_CLONE_COPY_ON_WRITE, PAGE_SIZE, size, &clone_vmo[0]), "vm_clone");

    // map the clone
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), 0, clone_vmo[0], 0, size, ZX_VM_FLAG_PERM_READ|ZX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NE(clone_ptr, 0, "map address");
    cp = (volatile size_t *)clone_ptr;

    // verify that it seems to be mapping the original at an offset
    for (size_t off = 0; off < (size - PAGE_SIZE) / sizeof(off); off++) {
        if (cp[off] != off + PAGE_SIZE / sizeof(off)) {
            EXPECT_EQ(cp[off], off + PAGE_SIZE / sizeof(off), "reading from clone");
            break;
        }
    }

    // verify that the last page we have mapped is beyond the original and should return zeros
    for (size_t off = (size - PAGE_SIZE) / sizeof(off); off < size / sizeof(off); off++) {
        if (cp[off] != 0) {
            EXPECT_EQ(cp[off], 0, "reading from clone");
            break;
        }
    }

    // resize the original
    EXPECT_EQ(ZX_OK, zx_vmo_set_size(vmo, size + PAGE_SIZE), "extend the vmo");

    // verify that the last page we have mapped still returns zeros
    for (size_t off = (size - PAGE_SIZE) / sizeof(off); off < size / sizeof(off); off++) {
        if (cp[off] != 0) {
            EXPECT_EQ(cp[off], 0, "reading from clone");
            break;
        }
    }

    // write to the new part of the original
    size_t val = 99;
    EXPECT_EQ(ZX_OK, zx_vmo_write(vmo, &val, size, sizeof(val)), "writing to original after extending");

    // verify that it is reflected in the clone
    EXPECT_EQ(99, cp[(size - PAGE_SIZE) / sizeof(*cp)], "modified newly exposed part of cow clone");

    // resize the original again, completely extending it beyond he clone
    EXPECT_EQ(ZX_OK, zx_vmo_set_size(vmo, size + PAGE_SIZE * 2), "extend the vmo");

    // resize the original to zero
    EXPECT_EQ(ZX_OK, zx_vmo_set_size(vmo, 0), "truncate the vmo");

    // verify that the clone now reads completely zeros, since it never COWed
    for (size_t off = 0; off < size / sizeof(off); off++) {
        if (cp[off] != 0) {
            EXPECT_EQ(cp[off], 0, "reading zeros from clone");
            break;
        }
    }

    // close and unmap
    EXPECT_EQ(ZX_OK, zx_handle_close(vmo), "handle_close");
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo[0]), "handle_close");
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), clone_ptr, size), "unmap");

    END_TEST;
}

bool vmo_clone_rights_test() {
    BEGIN_TEST;

    static const char kOldVmoName[] = "original";
    static const char kNewVmoName[] = "clone";

    static const zx_rights_t kOldVmoRights =
        ZX_RIGHT_READ | ZX_RIGHT_DUPLICATE;
    static const zx_rights_t kNewVmoRights =
        kOldVmoRights | ZX_RIGHT_WRITE |
        ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY;

    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &vmo),
              ZX_OK);
    ASSERT_EQ(zx_object_set_property(vmo, ZX_PROP_NAME,
                                     kOldVmoName, sizeof(kOldVmoName)),
              ZX_OK);
    ASSERT_EQ(get_handle_rights(vmo) & kOldVmoRights, kOldVmoRights);

    zx_handle_t reduced_rights_vmo;
    ASSERT_EQ(zx_handle_duplicate(vmo, kOldVmoRights, &reduced_rights_vmo),
              ZX_OK);
    EXPECT_EQ(get_handle_rights(reduced_rights_vmo), kOldVmoRights);

    zx_handle_t clone;
    ASSERT_EQ(zx_vmo_clone(reduced_rights_vmo, ZX_VMO_CLONE_COPY_ON_WRITE,
                           0, PAGE_SIZE, &clone),
              ZX_OK);

    EXPECT_EQ(zx_handle_close(reduced_rights_vmo), ZX_OK);

    ASSERT_EQ(zx_object_set_property(clone, ZX_PROP_NAME,
                                     kNewVmoName, sizeof(kNewVmoName)),
              ZX_OK);

    char oldname[ZX_MAX_NAME_LEN] = "bad";
    EXPECT_EQ(zx_object_get_property(vmo, ZX_PROP_NAME,
                                     oldname, sizeof(oldname)),
              ZX_OK);
    EXPECT_STR_EQ(oldname, kOldVmoName, "original VMO name");

    char newname[ZX_MAX_NAME_LEN] = "bad";
    EXPECT_EQ(zx_object_get_property(clone, ZX_PROP_NAME,
                                     newname, sizeof(newname)),
              ZX_OK);
    EXPECT_STR_EQ(newname, kNewVmoName, "clone VMO name");

    EXPECT_EQ(zx_handle_close(vmo), ZX_OK);
    EXPECT_EQ(get_handle_rights(clone), kNewVmoRights);
    EXPECT_EQ(zx_handle_close(clone), ZX_OK);

    END_TEST;
}

bool vmo_unmap_coherency() {
    BEGIN_TEST;

    // This is an expensive test to try to detect a multi-cpu coherency
    // problem with TLB flushing of unmap operations
    //
    // algorithm: map a relatively large committed VMO.
    // Create a worker thread that simply walks through the VMO writing to
    // each page.
    // In the main thread continually decommit the vmo with a little bit of
    // a gap between decommits to allow the worker thread to bring it all back in.
    // If the worker thread appears stuck by not making it through a loop in
    // a reasonable time, we have failed.

    // allocate a vmo
    const size_t len = 32*1024*1024;
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // do a regular map
    uintptr_t ptr = 0;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, len,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         &ptr);
    EXPECT_EQ(ZX_OK, status, "map");
    EXPECT_NE(0u, ptr, "map address");

    // create a worker thread
    struct worker_args {
        size_t len;
        uintptr_t ptr;
        fbl::atomic<bool> exit;
        fbl::atomic<bool> exited;
        fbl::atomic<size_t> count;
    } args = {};
    args.len = len;
    args.ptr = ptr;

    auto worker = [](void *_args) -> int {
        worker_args* a = (worker_args*)_args;

        unittest_printf("ptr %#" PRIxPTR " len %zu\n",
                a->ptr, a->len);

        while (!a->exit.load()) {
            // walk through the mapping, writing to every page
            for (size_t off = 0; off < a->len; off += PAGE_SIZE) {
                *(uint32_t*)(a->ptr + off) = 99;
            }

            a->count.fetch_add(1);
        }

        unittest_printf("exiting worker\n");

        a->exited.store(true);

        return 0;
    };

    thrd_t t;
    thrd_create(&t, worker, &args);

    const zx_time_t max_duration = ZX_SEC(30);
    const zx_time_t max_wait = ZX_SEC(1);
    zx_time_t start = zx_clock_get_monotonic();
    for (;;) {
        // wait for it to loop at least once
        zx_time_t t = zx_clock_get_monotonic();
        size_t last_count = args.count.load();
        while (args.count.load() <= last_count) {
            if (zx_clock_get_monotonic() - t > max_wait) {
                UNITTEST_FAIL_TRACEF("looper appears stuck!\n");
                break;
            }
        }

        // decommit the vmo
        status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, len, nullptr, 0);
        EXPECT_EQ(0, status, "vm decommit");

        if (zx_clock_get_monotonic() - start > max_duration)
            break;
    }

    // stop the thread and wait for it to exit
    args.exit.store(true);
    while (args.exited.load() == false)
        ;

    END_TEST;
}

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST(vmo_create_test);
RUN_TEST(vmo_read_write_test);
RUN_TEST(vmo_read_write_range_test);
RUN_TEST(vmo_map_test);
RUN_TEST(vmo_read_only_map_test);
RUN_TEST(vmo_no_perm_map_test);
RUN_TEST(vmo_no_perm_protect_test);
RUN_TEST(vmo_resize_test);
RUN_TEST(vmo_size_align_test);
RUN_TEST(vmo_resize_align_test);
RUN_TEST(vmo_clone_size_align_test);
RUN_TEST(vmo_rights_test);
RUN_TEST(vmo_commit_test);
RUN_TEST(vmo_decommit_misaligned_test);
RUN_TEST(vmo_cache_test);
RUN_TEST_PERFORMANCE(vmo_cache_map_test);
RUN_TEST(vmo_cache_op_test);
RUN_TEST(vmo_cache_flush_test);
RUN_TEST(vmo_zero_page_test);
RUN_TEST(vmo_clone_test_1);
RUN_TEST(vmo_clone_test_2);
RUN_TEST(vmo_clone_test_3);
RUN_TEST(vmo_clone_test_4);
RUN_TEST(vmo_clone_decommit_test);
RUN_TEST(vmo_clone_commit_test);
RUN_TEST(vmo_clone_rights_test);
RUN_TEST_LARGE(vmo_unmap_coherency);
END_TEST_CASE(vmo_tests)

int main(int argc, char** argv) {
    bool run_bench = false;
    if (argc > 1) {
        if (!strcmp(argv[1], "bench")) {
            run_bench = true;
        }
    }

    if (!run_bench) {
        bool success = unittest_run_all_tests(argc, argv);
        return success ? 0 : -1;
    } else {
        return vmo_run_benchmark();
    }
}
