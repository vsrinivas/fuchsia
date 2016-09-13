// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

bool vmo_create_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo[16];

    // allocate a bunch of vmos then free them
    for (size_t i = 0; i < countof(vmo); i++) {
        vmo[i] = mx_vmo_create(i * PAGE_SIZE);
        EXPECT_LT(0, vmo[i], "vm_object_create");
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
    mx_ssize_t sstatus;
    mx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE * 4;
    vmo = mx_vmo_create(len);
    EXPECT_LT(0, vmo, "vm_object_create");

    char buf[PAGE_SIZE];
    sstatus = mx_vmo_read(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ((mx_ssize_t)sizeof(buf), sstatus, "vm_object_read");

    memset(buf, 0x99, sizeof(buf));
    sstatus = mx_vmo_write(vmo, buf, 0, sizeof(buf));
    EXPECT_EQ((mx_ssize_t)sizeof(buf), sstatus, "vm_object_write");

    // map it
    uintptr_t ptr;
    status = mx_process_map_vm(mx_process_self(), vmo, 0, len, &ptr,
                                     MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
    EXPECT_EQ(NO_ERROR, status, "vm_map");
    EXPECT_NEQ(0u, ptr, "vm_map");

    // check that it matches what we last wrote into it
    EXPECT_BYTES_EQ((uint8_t*)buf, (uint8_t*)ptr, sizeof(buf), "mapped buffer");

    status = mx_process_unmap_vm(mx_process_self(), ptr, 0);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

bool vmo_read_only_map_test() {
    BEGIN_TEST;

    mx_status_t status;
    mx_handle_t vmo;

    // allocate an object and read/write from it
    const size_t len = PAGE_SIZE;
    vmo = mx_vmo_create(len);
    EXPECT_LT(0, vmo, "vm_object_create");

    // map it
    uintptr_t ptr;
    status = mx_process_map_vm(mx_process_self(), vmo, 0, len, &ptr,
                                     MX_VM_FLAG_PERM_READ);
    EXPECT_EQ(NO_ERROR, status, "vm_map");
    EXPECT_NEQ(0u, ptr, "vm_map");

    auto sstatus = mx_cprng_draw((void*)ptr, 1);
    EXPECT_LT(sstatus, 0, "write");

    status = mx_process_unmap_vm(mx_process_self(), ptr, 0);
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
    vmo = mx_vmo_create(len);
    EXPECT_LT(0, vmo, "vm_object_create");

    // get the size that we set it to
    uint64_t size = 0x99999999;
    status = mx_vmo_get_size(vmo, &size);
    EXPECT_EQ(NO_ERROR, status, "vm_object_get_size");
    EXPECT_EQ(len, size, "vm_object_get_size");

// set_size is not implemented right now, so test for the failure mode
#if 0
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
    EXPECT_EQ(ERR_NO_MEMORY, status, "vm_object_set_size");
#else
    status = mx_vmo_set_size(vmo, len + PAGE_SIZE);
    EXPECT_EQ(ERR_NOT_SUPPORTED, status, "vm_object_set_size");
#endif

    // close the handle
    status = mx_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");

    END_TEST;
}

static bool rights_test_map_helper(mx_handle_t vmo, size_t len, uint32_t flags, bool expect_success, mx_status_t fail_err_code, const char *msg) {
    uintptr_t ptr;

    mx_status_t r = mx_process_map_vm(mx_process_self(), vmo, 0, len, &ptr, flags);
    if (expect_success) {
        EXPECT_EQ(0, r, msg);

        r = mx_process_unmap_vm(mx_process_self(), ptr, 0);
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
    ssize_t r;
    mx_status_t status;
    uintptr_t ptr;
    mx_handle_t vmo, vmo2;

    // allocate an object
    vmo = mx_vmo_create(len);
    EXPECT_LT(0, vmo, "vm_object_create");

    // test that we can read/write it
    r = mx_vmo_read(vmo, buf, 0, 0);
    EXPECT_EQ(0, r, "vmo_read");
    r = mx_vmo_write(vmo, buf, 0, 0);
    EXPECT_EQ(0, r, "vmo_write");

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ);
    r = mx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(0, r, "vmo_read");
    r = mx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(ERR_ACCESS_DENIED, r, "vmo_write");
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_WRITE);
    r = mx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(ERR_ACCESS_DENIED, r, "vmo_read");
    r = mx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(0, r, "vmo_write");
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, 0);
    r = mx_vmo_read(vmo2, buf, 0, 0);
    EXPECT_EQ(ERR_ACCESS_DENIED, r, "vmo_read");
    r = mx_vmo_write(vmo2, buf, 0, 0);
    EXPECT_EQ(ERR_ACCESS_DENIED, r, "vmo_write");
    mx_handle_close(vmo2);

    // no permission map (should fail for now)
    r = mx_process_map_vm(mx_process_self(), vmo, 0, len, &ptr, 0);
    EXPECT_EQ(ERR_INVALID_ARGS, r, "map_noperms");

    // full perm test
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, true, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readexec")) return false;

    // try most of the permuations of mapping a vmo with various rights dropped
    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, false, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_MAP);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_WRITE | MX_RIGHT_MAP);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, false, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_MAP);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, true, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_EXECUTE | MX_RIGHT_MAP);
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ, true, ERR_ACCESS_DENIED, "map_read")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_WRITE, false, ERR_INVALID_ARGS, "map_write")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, false, ERR_ACCESS_DENIED, "map_readwrite")) return false;
    if (!rights_test_map_helper(vmo2, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE, false, ERR_ACCESS_DENIED, "map_readwriteexec")) return false;
    if (!rights_test_map_helper(vmo, len, MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_EXECUTE, true, ERR_ACCESS_DENIED, "map_readexec")) return false;
    mx_handle_close(vmo2);

    vmo2 = mx_handle_duplicate(vmo, MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_EXECUTE | MX_RIGHT_MAP);
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

    vmo = mx_vmo_create(size);
    EXPECT_LT(0, vmo, "vm_object_create");

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

BEGIN_TEST_CASE(vmo_tests)
RUN_TEST(vmo_create_test);
RUN_TEST(vmo_read_write_test);
RUN_TEST(vmo_read_only_map_test);
RUN_TEST(vmo_resize_test);
RUN_TEST(vmo_rights_test);
RUN_TEST(vmo_lookup_test);
END_TEST_CASE(vmo_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
