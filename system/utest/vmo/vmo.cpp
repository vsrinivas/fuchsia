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

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
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

    char buf[PAGE_SIZE];
    status = mx_vmo_read(vmo, buf, 0, sizeof(buf), &size);
    EXPECT_EQ(status, NO_ERROR, "vm_object_read");
    EXPECT_EQ(sizeof(buf), size, "vm_object_read");

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

    mx_info_vmar_t vmar_info;
    status = mx_object_get_info(mx_vmar_root_self(), MX_INFO_VMAR, &vmar_info,
                                sizeof(vmar_info), NULL, NULL);
    EXPECT_EQ(NO_ERROR, status, "get_info");

    // map it in a fixed spot
    const uintptr_t fixed = 0x3e000000; // arbitrary fixed spot
    status = mx_vmar_map(mx_vmar_root_self(), fixed - vmar_info.base, vmo, 0,
                         PAGE_SIZE, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_SPECIFIC,
                         &ptr[1]);
    EXPECT_EQ(NO_ERROR, status, "map");
    EXPECT_EQ(fixed, ptr[1], "map fixed address");
    //printf("mapped %#" PRIxPTR "\n", ptr[1]);

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
            status = mx_vmar_unmap(mx_vmar_root_self(), p, 0);
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
    EXPECT_EQ(ERR_OUT_OF_RANGE, status, "out of range");

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
    EXPECT_NONNULL(ptr, "map address2");

    // third mapping with a totally non-overlapping offset
    ptr3 = 0;
    status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, size * 2, size,
                         MX_VM_FLAG_PERM_READ|MX_VM_FLAG_PERM_WRITE, &ptr3);
    EXPECT_EQ(NO_ERROR, status, "map3");
    EXPECT_NONNULL(ptr, "map address3");

    // write into it at offset PAGE_SIZE, read it back
    uint32_t *u32 = (uint32_t *)(ptr + PAGE_SIZE);
    *u32 = 99;
    EXPECT_EQ(99u, (*u32), "written memory");

    uint32_t *u32a = (uint32_t *)(ptr2);
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

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST(vmo_create_test);
RUN_TEST(vmo_read_write_test);
RUN_TEST(vmo_map_test);
RUN_TEST(vmo_read_only_map_test);
RUN_TEST(vmo_resize_test);
RUN_TEST(vmo_rights_test);
RUN_TEST(vmo_lookup_test);
RUN_TEST(vmo_commit_test);
END_TEST_CASE(vmo_tests)

int main(int argc, char** argv) {
    printf("argc %d\n", argc);

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
