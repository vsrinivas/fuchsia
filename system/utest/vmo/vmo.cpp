// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

#include "bench.h"

bool vmo_create_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo[16];

    // allocate a bunch of vmos then free them
    for (size_t i = 0; i < countof(vmo); i++) {
        status = mx_vmo_create(i * PAGE_SIZE, 0, &vmo[i]);
        EXPECT_EQ(NO_ERROR, status, "vm_object_create");
    }

    for (size_t i = 0; i < countof(vmo); i++) {
        status = mx_handle_close(vmo[i]);
        EXPECT_EQ(NO_ERROR, status, "handle_close");
    }

    END_TEST;
}

bool vmo_read_write_test() {
    BEGIN_TEST;

    mx_status_t status;
    size_t size;
    mx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE * 4;
    status = mx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(status, NO_ERROR, "vm_object_create");

    char buf[len];
    status = mx_vmo_read(vmo, buf, 0, sizeof(buf), &size);
    EXPECT_EQ(status, NO_ERROR, "vm_object_read");
    EXPECT_EQ(sizeof(buf), size, "vm_object_read");

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
    status = mx_vmo_write(vmo, buf, 0, sizeof(buf), &size);
    EXPECT_EQ(status, NO_ERROR, "vm_object_write");
    EXPECT_EQ(sizeof(buf), size, "vm_object_write");

    // map it
    uintptr_t ptr;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, len,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);
    EXPECT_EQ(NO_ERROR, status, "vm_map");
    EXPECT_NEQ(0u, ptr, "vm_map");

    // check that it matches what we last wrote into it
    EXPECT_BYTES_EQ((uint8_t*)buf, (uint8_t*)ptr, sizeof(buf), "mapped buffer");

    status = mx_vmar_unmap(mx_vmar_root_self(), ptr, len);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_map_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo;
    uintptr_t ptr[3] = {};

    // allocate a vmo
    status = mx_vmo_create(4 * PAGE_SIZE, 0, &vmo);
    EXPECT_EQ(NO_ERROR, status, "vm_object_create");

    // do a regular map
    ptr[0] = 0;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, PAGE_SIZE,
                         MX_VM_FLAG_PERM_READ, &ptr[0]);
    EXPECT_EQ(NO_ERROR, status, "map");
    EXPECT_NEQ(0u, ptr[0], "map address");
    //printf("mapped %#" PRIxPTR "\n", ptr[0]);

    // try to map something completely out of range without any fixed mapping, should succeed
    ptr[2] = UINTPTR_MAX;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, PAGE_SIZE,
                         MX_VM_FLAG_PERM_READ, &ptr[2]);
    EXPECT_EQ(NO_ERROR, status, "map");
    EXPECT_NEQ(0u, ptr[2], "map address");

    // try to map something completely out of range fixed, should fail
    uintptr_t map_addr;
    status = mx_vmar_map(mx_vmar_root_self(), UINTPTR_MAX,
                         vmo, 0, PAGE_SIZE,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_SPECIFIC, &map_addr);
    EXPECT_EQ(ERR_INVALID_ARGS, status, "map");

    // cleanup
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    for (auto p: ptr) {
        if (p) {
            status = mx_vmar_unmap(mx_vmar_root_self(), p, PAGE_SIZE);
            EXPECT_EQ(NO_ERROR, status, "unmap");
        }
    }

    END_TEST;
}

bool vmo_read_only_map_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    status = mx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(NO_ERROR, status, "vm_object_create");

    // map it
    uintptr_t ptr;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, len,
                         MX_VM_FLAG_PERM_READ, &ptr);
    EXPECT_EQ(NO_ERROR, status, "vm_map");
    EXPECT_NEQ(0u, ptr, "vm_map");

    size_t sz;
    auto sstatus = mx_cprng_draw((void*)ptr, 1, &sz);
    EXPECT_LT(sstatus, 0, "write");

    status = mx_vmar_unmap(mx_vmar_root_self(), ptr, len);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_resize_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo;

    // allocate an object
    size_t len = PAGE_SIZE * 4;
    status = mx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(NO_ERROR, status, "vm_object_create");

    // get the size that we set it to
    uint64_t size = 0x99999999;
    status = mx_vmo_get_size(vmo, &size);
    EXPECT_EQ(NO_ERROR, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

    // try to resize it
    len += PAGE_SIZE;
    status = mx_vmo_set_size(vmo, len);
    EXPECT_EQ(NO_ERROR, status, "vm_object_set_size");

    // get the size again
    size = 0x99999999;
    status = mx_vmo_get_size(vmo, &size);
    EXPECT_EQ(NO_ERROR, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

    // try to resize it to a ludicrous size
    status = mx_vmo_set_size(vmo, UINT64_MAX);
    EXPECT_EQ(ERR_OUT_OF_RANGE, status, "vm_object_set_size too big");

    // map it
    uintptr_t ptr;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, len,
                         MX_VM_FLAG_PERM_READ, &ptr);
    EXPECT_EQ(NO_ERROR, status, "vm_map");
    EXPECT_NONNULL(ptr, "vm_map");

    // resize it with it mapped
    status = mx_vmo_set_size(vmo, size);
    EXPECT_EQ(NO_ERROR, status, "vm_object_set_size");

    // unmap it
    status = mx_vmar_unmap(mx_vmar_root_self(), ptr, len);
    EXPECT_EQ(NO_ERROR, status, "unmap");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

static bool rights_test_map_helper(mx_handle_t vmo, size_t len, uint32_t flags, bool expect_success, mx_status_t fail_err_code, const char *msg) {
    uintptr_t ptr;

    mx_status_t r = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, len, flags,
                                &ptr);
    if (expect_success) {
        EXPECT_EQ(0, r, msg);

        r = mx_vmar_unmap(mx_vmar_root_self(), ptr, len);
        EXPECT_EQ(0, r, "unmap");
    } else {
        EXPECT_EQ(fail_err_code, r, msg);
    }

    return true;
}

// Returns zero on failure.
static mx_rights_t get_handle_rights(mx_handle_t h) {
    mx_info_handle_basic_t info;
    mx_status_t s = mx_object_get_info(h, MX_INFO_HANDLE_BASIC, &info,
                                       sizeof(info), nullptr, nullptr);
    if (s != NO_ERROR) {
        EXPECT_EQ(s, NO_ERROR, "");  // Poison the test
        return 0;
    }
    return info.rights;
}

bool vmo_rights_test() {
    BEGIN_TEST;

    char buf[4096];
    size_t len = PAGE_SIZE * 4;
    size_t r;
    mx_status_t status;
    uintptr_t ptr;
    mx_handle_t vmo, vmo2;

    // allocate an object
    status = mx_vmo_create(len, 0, &vmo);
    EXPECT_EQ(NO_ERROR, status, "vm_object_create");

    // Check that the handle has at least the expected rights.
    // This list should match the list in docs/syscalls/vmo_create.md.
    static const mx_rights_t kExpectedRights =
        MX_RIGHT_DUPLICATE |
        MX_RIGHT_TRANSFER |
        MX_RIGHT_READ |
        MX_RIGHT_WRITE |
        MX_RIGHT_EXECUTE |
        MX_RIGHT_MAP |
        MX_RIGHT_GET_PROPERTY |
        MX_RIGHT_SET_PROPERTY;
    EXPECT_EQ(kExpectedRights, kExpectedRights & get_handle_rights(vmo), "");

    // test that we can read/write it
    status = mx_vmo_read(vmo, buf, 0, 0, &r);
    EXPECT_EQ(0, status, "vmo_read");
    status = mx_vmo_write(vmo, buf, 0, 0, &r);
    EXPECT_EQ(0, status, "vmo_write");

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ, &vmo2);
    status = mx_vmo_read(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(0, status, "vmo_read");
    status = mx_vmo_write(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(ERR_ACCESS_DENIED, status, "vmo_write");
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_WRITE, &vmo2);
    status = mx_vmo_read(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(ERR_ACCESS_DENIED, status, "vmo_read");
    status = mx_vmo_write(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(0, status, "vmo_write");
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, 0, &vmo2);
    status = mx_vmo_read(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(ERR_ACCESS_DENIED, status, "vmo_read");
    status = mx_vmo_write(vmo2, buf, 0, 0, &r);
    EXPECT_EQ(ERR_ACCESS_DENIED, status, "vmo_write");
    mx_handle_close(vmo2);

    // no permission map (should fail)
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, len, 0, &ptr);
    EXPECT_EQ(ERR_INVALID_ARGS, status, "map_noperms");

    // full perm test
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, true, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readexec")) return false;

    // try most of the permuations of mapping a vmo with various rights dropped
    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, false, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_WRITE | MX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, false, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, true, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = MX_HANDLE_INVALID;
    mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE | MX_RIGHT_MAP, &vmo2);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, true, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    // test that we can get/set a property on it
    const char *set_name = "test vmo";
    status = mx_object_set_property(vmo, MX_PROP_NAME, set_name, sizeof(set_name));
    EXPECT_EQ(NO_ERROR, status, "set_property");
    char get_name[MX_MAX_NAME_LEN];
    status = mx_object_get_property(vmo, MX_PROP_NAME, get_name, sizeof(get_name));
    EXPECT_EQ(NO_ERROR, status, "get_property");
    EXPECT_STR_EQ(set_name, get_name, sizeof(set_name), "vmo name");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_lookup_test() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_status_t status;

    const size_t size = 16384;
    mx_paddr_t buf[size / PAGE_SIZE];

    status = mx_vmo_create(size, 0, &vmo);
    EXPECT_EQ(0, status, "vm_object_create");

    // do a lookup (this should fail becase the pages aren't committed)
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, size, buf, sizeof(buf));
    EXPECT_EQ(ERR_NO_MEMORY, status, "lookup on uncommitted vmo");

    // commit the memory
    status = mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, 0, size, buf, sizeof(buf));
    EXPECT_EQ(NO_ERROR, status, "committing memory");

    // do a lookup (should succeed)
    memset(buf, 0, sizeof(buf));
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, size, buf, sizeof(buf));
    EXPECT_EQ(NO_ERROR, status, "lookup on committed vmo");

    for (auto addr: buf)
        EXPECT_NEQ(0u, addr, "looked up address");

    // do a lookup with an odd offset and an end pointer that ends up at an odd offset
    memset(buf, 0, sizeof(buf));
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 1, size - PAGE_SIZE, buf, sizeof(buf));
    EXPECT_EQ(NO_ERROR, status, "lookup on committed vmo");

    for (auto addr: buf)
        EXPECT_NEQ(0u, addr, "looked up address");

    // invalid args

    // do a lookup with no size
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, 0, buf, sizeof(buf));
    EXPECT_EQ(ERR_INVALID_ARGS, status, "zero size on lookup");

    // do a lookup out of range
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, size + 1, 1, buf, sizeof(buf));
    EXPECT_EQ(ERR_OUT_OF_RANGE, status, "out of range");

    // do a lookup out of range
    status = mx_vmo_op_range(vmo, MX_VMO_OP_LOOKUP, 0, size + 1, buf, sizeof(buf));
    EXPECT_EQ(ERR_BUFFER_TOO_SMALL, status, "buffer too small");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_commit_test() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_status_t status;
    uintptr_t ptr, ptr2, ptr3;

    // create a vmo
    const size_t size = 16384;

    status = mx_vmo_create(size, 0, &vmo);
    EXPECT_EQ(0, status, "vm_object_create");

    // commit a range of it
    status = mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm commit");

    // decommit that range
    status = mx_vmo_op_range(vmo, MX_VMO_OP_DECOMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // commit a range of it
    status = mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, 0, size, nullptr, 0);
    EXPECT_EQ(0, status, "vm commit");

    // map it
    ptr = 0;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                         MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr);
    EXPECT_EQ(NO_ERROR, status, "map");
    EXPECT_NONNULL(ptr, "map address");

    // second mapping with an offset
    ptr2 = 0;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, PAGE_SIZE, size,
                         MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr2);
    EXPECT_EQ(NO_ERROR, status, "map2");
    EXPECT_NONNULL(ptr2, "map address2");

    // third mapping with a totally non-overlapping offset
    ptr3 = 0;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, size * 2, size,
                         MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr3);
    EXPECT_EQ(NO_ERROR, status, "map3");
    EXPECT_NONNULL(ptr3, "map address3");

    // write into it at offset PAGE_SIZE, read it back
    volatile uint32_t *u32 = (volatile uint32_t *)(ptr + PAGE_SIZE);
    *u32 = 99;
    EXPECT_EQ(99u, (*u32), "written memory");

    // check the alias
    volatile uint32_t *u32a = (volatile uint32_t *)(ptr2);
    EXPECT_EQ(99u, (*u32a), "written memory");

    // decommit page 0
    status = mx_vmo_op_range(vmo, MX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // verify that it didn't get unmapped
    EXPECT_EQ(99u, (*u32), "written memory");
    // verify that it didn't get unmapped
    EXPECT_EQ(99u, (*u32a), "written memory2");

    // decommit page 1
    status = mx_vmo_op_range(vmo, MX_VMO_OP_DECOMMIT, PAGE_SIZE, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(0, status, "vm decommit");

    // verify that it did get unmapped
    EXPECT_EQ(0u, (*u32), "written memory");
    // verify that it did get unmapped
    EXPECT_EQ(0u, (*u32a), "written memory2");

    // unmap our vmos
    status = mx_vmar_unmap(mx_vmar_root_self(), ptr, size);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");
    status = mx_vmar_unmap(mx_vmar_root_self(), ptr2, size);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");
    status = mx_vmar_unmap(mx_vmar_root_self(), ptr3, size);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_zero_page_test() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_status_t status;
    uintptr_t ptr[3];

    // create a vmo
    const size_t size = PAGE_SIZE * 4;

    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "vm_object_create");

    // make a few mappings of the vmo
    for (auto &p: ptr) {
        EXPECT_EQ(NO_ERROR,
                mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &p),
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
    size_t written;
    status = mx_vmo_write(vmo, &v, PAGE_SIZE, sizeof(v), &written);
    EXPECT_EQ(NO_ERROR, status, "writing to vmo");

    // expect it to read back the new value
    EXPECT_EQ(100, *val, "read 100 from former zero page");

    // read fault in zeros on the third page
    val = (volatile uint32_t *)(ptr[0] + PAGE_SIZE * 2);
    EXPECT_EQ(0, *val, "read zero");

    // commit this range of the vmo via a commit call
    status = mx_vmo_op_range(vmo, MX_VMO_OP_COMMIT, PAGE_SIZE * 2, PAGE_SIZE, nullptr, 0);
    EXPECT_EQ(NO_ERROR, status, "committing memory");

    // write to the third page
    status = mx_vmo_write(vmo, &v, PAGE_SIZE * 2, sizeof(v), &written);
    EXPECT_EQ(NO_ERROR, status, "writing to vmo");

    // expect it to read back the new value
    EXPECT_EQ(100, *val, "read 100 from former zero page");

    // unmap
    for (auto p: ptr)
        EXPECT_EQ(NO_ERROR, mx_vmar_unmap(mx_vmar_root_self(), p, size), "unmap");

    // close the handle
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "handle_close");

    END_TEST;
}

// test set 1: create a few clones, close them
bool vmo_clone_test_1() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_handle_t clone_vmo[3];

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "vm_object_create");

    // clone it
    clone_vmo[0] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NEQ(MX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");

    // clone it a second time
    clone_vmo[1] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[1]), "vm_clone");
    EXPECT_NEQ(MX_HANDLE_INVALID, clone_vmo[1], "vm_clone_handle");

    // clone the clone
    clone_vmo[2] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(clone_vmo[1], MX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[2]), "vm_clone");
    EXPECT_NEQ(MX_HANDLE_INVALID, clone_vmo[2], "vm_clone_handle");

    // close the original handle
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "handle_close");

    // close the clone handles
    for (auto h: clone_vmo)
        EXPECT_EQ(NO_ERROR, mx_handle_close(h), "handle_close");

    END_TEST;
}

// test set 2: create a clone, verify that it COWs via the read/write interface
bool vmo_clone_test_2() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_handle_t clone_vmo[1];
    //uintptr_t ptr;
    size_t bytes_handled;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "vm_object_create");

    // fill the original with stuff
    for (size_t off = 0; off < size; off += sizeof(off)) {
        mx_vmo_write(vmo, &off, off, sizeof(off), &bytes_handled);
    }

    // clone it
    clone_vmo[0] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NEQ(MX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");

    // verify that the clone reads back as the same
    for (size_t off = 0; off < size; off += sizeof(off)) {
        size_t val;

        mx_vmo_read(clone_vmo[0], &val, off, sizeof(val), &bytes_handled);

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back");
            break;
        }
    }

    // write to part of the clone
    size_t val = 99;
    mx_vmo_write(clone_vmo[0], &val, 0, sizeof(val), &bytes_handled);

    // verify the clone was written to
    EXPECT_EQ(NO_ERROR, mx_vmo_read(clone_vmo[0], &val, 0, sizeof(val), &bytes_handled), "writing to clone");

    // verify it was written to
    EXPECT_EQ(99, val, "reading back from clone");

    // verify that the rest of the page it was written two was cloned
    for (size_t off = sizeof(val); off < PAGE_SIZE; off += sizeof(off)) {
        mx_vmo_read(clone_vmo[0], &val, off, sizeof(val), &bytes_handled);

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back");
            break;
        }
    }

    // verify that it didn't trash the original
    for (size_t off = 0; off < size; off += sizeof(off)) {
        mx_vmo_read(vmo, &val, off, sizeof(val), &bytes_handled);

        if (val != off) {
            EXPECT_EQ(val, off, "vm_clone read back of original");
            break;
        }
    }

    // write to the original in the part that is still visible to the clone
    val = 99;
    uint64_t offset = PAGE_SIZE * 2;
    EXPECT_EQ(NO_ERROR, mx_vmo_write(vmo, &val, offset, sizeof(val), &bytes_handled), "writing to original");
    EXPECT_EQ(NO_ERROR, mx_vmo_read(clone_vmo[0], &val, offset, sizeof(val), &bytes_handled), "reading back original from clone");
    EXPECT_EQ(99, val, "checking value");

    // close the clone handles
    for (auto h: clone_vmo)
        EXPECT_EQ(NO_ERROR, mx_handle_close(h), "handle_close");

    // close the original handle
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "handle_close");

    END_TEST;
}

// test set 3: test COW via a mapping
bool vmo_clone_test_3() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_handle_t clone_vmo[1];
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile uint32_t *p;
    volatile uint32_t *cp;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(NO_ERROR,
            mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NONNULL(ptr, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo[0] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NEQ(MX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");
    EXPECT_EQ(NO_ERROR,
            mx_vmar_map(mx_vmar_root_self(), 0, clone_vmo[0], 0, size, MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NONNULL(clone_ptr, "map address");
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
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "handle_close");

    // close the clone handle
    EXPECT_EQ(NO_ERROR, mx_handle_close(clone_vmo[0]), "handle_close");

    // unmap
    EXPECT_EQ(NO_ERROR, mx_vmar_unmap(mx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(NO_ERROR, mx_vmar_unmap(mx_vmar_root_self(), clone_ptr, size), "unmap");

    END_TEST;
}

bool vmo_cache_test() {
    BEGIN_TEST;

    mx_handle_t vmo;
    const size_t size = PAGE_SIZE * 4;

    // The objects returned by mx_vmo_create() are VmObjectPaged objects which
    // should not support these syscalls.
    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "creation for cache_policy");
    EXPECT_EQ(ERR_NOT_SUPPORTED, mx_vmo_set_cache_policy(vmo, MX_CACHE_POLICY_UNCACHED),
              "attempt set cache");
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "close handle");
    END_TEST;
}

// test set 4: deal with clones with nonzero offsets and offsets that extend beyond the original
bool vmo_clone_test_4() {
    BEGIN_TEST;

    mx_handle_t vmo;
    mx_handle_t clone_vmo[1];
    uintptr_t ptr;
    uintptr_t clone_ptr;
    volatile size_t *p;
    volatile size_t *cp;
    size_t handled_bytes;

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(NO_ERROR, mx_vmo_create(size, 0, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(NO_ERROR,
            mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size, MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr),
            "map");
    EXPECT_NONNULL(ptr, "map address");
    p = (volatile size_t *)ptr;

    // fill it with stuff
    for (size_t off = 0; off < size / sizeof(off); off++)
        p[off] = off;

    // make sure that non page aligned clones do not work
    clone_vmo[0] = MX_HANDLE_INVALID;
    EXPECT_EQ(ERR_INVALID_ARGS, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, 1, size, &clone_vmo[0]), "vm_clone");

    // create a clone that extends beyond the parent by one page
    clone_vmo[0] = MX_HANDLE_INVALID;
    EXPECT_EQ(NO_ERROR, mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE, PAGE_SIZE, size, &clone_vmo[0]), "vm_clone");

    // map the clone
    EXPECT_EQ(NO_ERROR,
            mx_vmar_map(mx_vmar_root_self(), 0, clone_vmo[0], 0, size, MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &clone_ptr),
            "map");
    EXPECT_NONNULL(clone_ptr, "map address");
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
    EXPECT_EQ(NO_ERROR, mx_vmo_set_size(vmo, size + PAGE_SIZE), "extend the vmo");

    // verify that the last page we have mapped still returns zeros
    for (size_t off = (size - PAGE_SIZE) / sizeof(off); off < size / sizeof(off); off++) {
        if (cp[off] != 0) {
            EXPECT_EQ(cp[off], 0, "reading from clone");
            break;
        }
    }

    // write to the new part of the original
    size_t val = 99;
    EXPECT_EQ(NO_ERROR, mx_vmo_write(vmo, &val, size, sizeof(val), &handled_bytes), "writing to original after extending");

    // verify that it is reflected in the clone
    EXPECT_EQ(99, cp[(size - PAGE_SIZE) / sizeof(*cp)], "modified newly exposed part of cow clone");

    // resize the original again, completely extending it beyond he clone
    EXPECT_EQ(NO_ERROR, mx_vmo_set_size(vmo, size + PAGE_SIZE * 2), "extend the vmo");

    // resize the original to zero
    EXPECT_EQ(NO_ERROR, mx_vmo_set_size(vmo, 0), "truncate the vmo");

    // verify that the clone now reads completely zeros, since it never COWed
    for (size_t off = 0; off < size / sizeof(off); off++) {
        if (cp[off] != 0) {
            EXPECT_EQ(cp[off], 0, "reading zeros from clone");
            break;
        }
    }

    // close and unmap
    EXPECT_EQ(NO_ERROR, mx_handle_close(vmo), "handle_close");
    EXPECT_EQ(NO_ERROR, mx_vmar_unmap(mx_vmar_root_self(), ptr, size), "unmap");
    EXPECT_EQ(NO_ERROR, mx_handle_close(clone_vmo[0]), "handle_close");
    EXPECT_EQ(NO_ERROR, mx_vmar_unmap(mx_vmar_root_self(), clone_ptr, size), "unmap");

    END_TEST;
}

bool vmo_clone_rights_test() {
    BEGIN_TEST;

    static const char kOldVmoName[] = "original";
    static const char kNewVmoName[] = "clone";

    static const mx_rights_t kOldVmoRights =
        MX_RIGHT_READ | MX_RIGHT_DUPLICATE;
    static const mx_rights_t kNewVmoRights =
        kOldVmoRights | MX_RIGHT_WRITE |
        MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo),
              NO_ERROR, "");
    ASSERT_EQ(mx_object_set_property(vmo, MX_PROP_NAME,
                                     kOldVmoName, sizeof(kOldVmoName)),
              NO_ERROR, "");
    ASSERT_EQ(get_handle_rights(vmo) & kOldVmoRights, kOldVmoRights, "");

    mx_handle_t reduced_rights_vmo;
    ASSERT_EQ(mx_handle_duplicate(vmo, kOldVmoRights, &reduced_rights_vmo),
              NO_ERROR, "");
    EXPECT_EQ(get_handle_rights(reduced_rights_vmo), kOldVmoRights, "");

    mx_handle_t clone;
    ASSERT_EQ(mx_vmo_clone(reduced_rights_vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                           0, PAGE_SIZE, &clone),
              NO_ERROR, "");

    EXPECT_EQ(mx_handle_close(reduced_rights_vmo), NO_ERROR, "");

    ASSERT_EQ(mx_object_set_property(clone, MX_PROP_NAME,
                                     kNewVmoName, sizeof(kNewVmoName)),
              NO_ERROR, "");

    char oldname[MX_MAX_NAME_LEN] = "bad";
    EXPECT_EQ(mx_object_get_property(vmo, MX_PROP_NAME,
                                     oldname, sizeof(oldname)),
              NO_ERROR, "");
    EXPECT_STR_EQ(oldname, kOldVmoName, sizeof(kOldVmoName),
                  "original VMO name");

    char newname[MX_MAX_NAME_LEN] = "bad";
    EXPECT_EQ(mx_object_get_property(clone, MX_PROP_NAME,
                                     newname, sizeof(newname)),
              NO_ERROR, "");
    EXPECT_STR_EQ(newname, kNewVmoName, sizeof(kNewVmoName),
                  "clone VMO name");

    EXPECT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    EXPECT_EQ(get_handle_rights(clone), kNewVmoRights, "");
    EXPECT_EQ(mx_handle_close(clone), NO_ERROR, "");

    END_TEST;
}

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST(vmo_create_test);
RUN_TEST(vmo_read_write_test);
RUN_TEST(vmo_map_test);
RUN_TEST(vmo_read_only_map_test);
RUN_TEST(vmo_resize_test);
RUN_TEST(vmo_rights_test);
RUN_TEST(vmo_lookup_test);
RUN_TEST(vmo_commit_test);
RUN_TEST(vmo_cache_test);
RUN_TEST(vmo_zero_page_test);
RUN_TEST(vmo_clone_test_1);
RUN_TEST(vmo_clone_test_2);
RUN_TEST(vmo_clone_test_3);
RUN_TEST(vmo_clone_test_4);
RUN_TEST(vmo_clone_rights_test);
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
