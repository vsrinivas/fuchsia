// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <lib/fzl/memory-probe.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace {

bool vmo_clone_size_align_test() {
    BEGIN_TEST;

    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(0, 0, &vmo);
    EXPECT_EQ(ZX_OK, status, "vm_object_create");

    // create clones with different sizes, make sure the created size is a multiple of a page size
    for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
        zx_handle_t clone_vmo;
        EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE,
                                             0, s, &clone_vmo), "vm_clone");

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
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");
    char name[ZX_MAX_NAME_LEN];
    EXPECT_EQ(ZX_OK, zx_object_get_property(clone_vmo[0], ZX_PROP_NAME, name, ZX_MAX_NAME_LEN), "zx_object_get_property");
    EXPECT_TRUE(!strcmp(name, "test1"), "get_name");

    // clone it a second time
    clone_vmo[1] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[1]), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[1], "vm_clone_handle");

    // clone the clone
    clone_vmo[2] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(clone_vmo[1], ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[2]), "vm_clone");
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

    // create a vmo
    const size_t size = PAGE_SIZE * 4;
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, 0, &vmo), "vm_object_create");

    // fill the original with stuff
    for (size_t off = 0; off < size; off += sizeof(off)) {
        zx_vmo_write(vmo, &off, off, sizeof(off));
    }

    // clone it
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[0]), "vm_clone");
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
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, ZX_VMO_RESIZABLE, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[0]),"vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");

    // Attempt a non-resizable map fails.
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
        zx_vmar_map(zx_vmar_root_self(),
            ZX_VM_PERM_READ|ZX_VM_PERM_WRITE|ZX_VM_REQUIRE_NON_RESIZABLE,
            0, clone_vmo[0], 0, size, &clone_ptr), "map");

    // Regular resizable mapping works.
    EXPECT_EQ(ZX_OK,
        zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, clone_vmo[0], 0, size, &clone_ptr),
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

// verify that the parent is visible through decommitted pages
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
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, clone_vmo, 0, size, &clone_ptr),
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

    // make sure the decommitted page still has COW semantics
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
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile uint32_t *)ptr;

    // clone it and map that
    clone_vmo = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
    EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, clone_vmo, 0, size,  &clone_ptr),
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
    EXPECT_EQ(ZX_OK, zx_vmo_create(size, ZX_VMO_RESIZABLE, &vmo), "vm_object_create");

    // map it
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr),
            "map");
    EXPECT_NE(ptr, 0, "map address");
    p = (volatile size_t *)ptr;

    // fill it with stuff
    for (size_t off = 0; off < size / sizeof(off); off++)
        p[off] = off;

    // make sure that non page aligned clones do not work
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 1, size, &clone_vmo[0]), "vm_clone");

    // create a clone that extends beyond the parent by one page
    clone_vmo[0] = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, PAGE_SIZE, size, &clone_vmo[0]), "vm_clone");

    // map the clone
    EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ|ZX_VM_PERM_WRITE, 0, clone_vmo[0], 0, size, &clone_ptr),
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
    EXPECT_EQ(ZX_OK, zx_vmo_write(vmo, &val, size, sizeof(val)),
              "writing to original after extending");

    // verify that it is not reflected in the clone
    EXPECT_EQ(0, cp[(size - PAGE_SIZE) / sizeof(*cp)],
              "didn't modified newly exposed part of cow clone");

    // write to a page in the original vmo
    EXPECT_EQ(ZX_OK, zx_vmo_write(vmo, &val, size - PAGE_SIZE, sizeof(val)),
              "writing to original after extending");

    // verify that it is reflected in the clone
    EXPECT_EQ(99, cp[(size - 2 * PAGE_SIZE) / sizeof(*cp)],
              "modified newly exposed part of cow clone");

    // shrink and enlarge the clone
    EXPECT_EQ(ZX_OK, zx_vmo_set_size(clone_vmo[0], size - 2 * PAGE_SIZE), "shrunk the clone");
    EXPECT_EQ(ZX_OK, zx_vmo_set_size(clone_vmo[0], size), "extend the clone");

    // verify that new pages are zero-pages instead of uncovering previously visible parent pages
    EXPECT_EQ(0, cp[(size - 2 * PAGE_SIZE) / sizeof(*cp)],
              "didn't modified newly exposed part of cow clone");

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
    ASSERT_EQ(zx_vmo_create_child(reduced_rights_vmo, ZX_VMO_CHILD_COPY_ON_WRITE,
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

// Resizing a cloned VMO causes a fault.
bool vmo_clone_resize_clone_hazard() {
    BEGIN_TEST;

    const size_t size = PAGE_SIZE * 2;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(size, 0, &vmo), ZX_OK);

    zx_handle_t clone_vmo;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(
        vmo, ZX_VMO_CHILD_COPY_ON_WRITE | ZX_VMO_CHILD_RESIZABLE, 0, size, &clone_vmo), "vm_clone");

    uintptr_t ptr_rw;
    EXPECT_EQ(ZX_OK, zx_vmar_map(
            zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
            clone_vmo, 0, size, &ptr_rw), "map");

    auto int_arr = reinterpret_cast<int*>(ptr_rw);
    EXPECT_EQ(int_arr[1], 0);

    EXPECT_EQ(ZX_OK, zx_vmo_set_size(clone_vmo, 0u));

    EXPECT_EQ(false, probe_for_read(&int_arr[1]), "read probe");
    EXPECT_EQ(false, probe_for_write(&int_arr[1]), "write probe");

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo));
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size), "unmap");
    END_TEST;
}

// Resizing the parent VMO and accessing via a mapped VMO is ok.
bool vmo_clone_resize_parent_ok() {
    BEGIN_TEST;

    const size_t size = PAGE_SIZE * 2;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(size, ZX_VMO_RESIZABLE, &vmo), ZX_OK);

    zx_handle_t clone_vmo;
    EXPECT_EQ(ZX_OK, zx_vmo_create_child(
        vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");

    uintptr_t ptr_rw;
    EXPECT_EQ(ZX_OK, zx_vmar_map(
            zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
            clone_vmo, 0, size, &ptr_rw), "map");

    auto int_arr = reinterpret_cast<int*>(ptr_rw);
    EXPECT_EQ(int_arr[1], 0);

    EXPECT_EQ(ZX_OK, zx_vmo_set_size(vmo, 0u));

    EXPECT_EQ(true, probe_for_read(&int_arr[1]), "read probe");
    EXPECT_EQ(true, probe_for_write(&int_arr[1]), "write probe");

    EXPECT_EQ(ZX_OK, zx_handle_close(vmo));
    EXPECT_EQ(ZX_OK, zx_handle_close(clone_vmo));
    EXPECT_EQ(ZX_OK, zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size), "unmap");
    END_TEST;
}

// Check that non-resizable VMOs cannot get resized.
bool vmo_clone_no_resize_test() {
    BEGIN_TEST;

    const size_t len = PAGE_SIZE * 4;
    zx_handle_t parent = ZX_HANDLE_INVALID;
    zx_handle_t vmo = ZX_HANDLE_INVALID;

    zx_vmo_create(len, 0, &parent);
    zx_vmo_create_child(parent,
        ZX_VMO_CHILD_COPY_ON_WRITE | ZX_VMO_CHILD_NON_RESIZEABLE,
        0, len, &vmo);

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

} // namespace

BEGIN_TEST_CASE(vmo_clone_tests)
RUN_TEST(vmo_clone_size_align_test);
RUN_TEST(vmo_clone_test_1);
RUN_TEST(vmo_clone_test_2);
RUN_TEST(vmo_clone_test_3);
RUN_TEST(vmo_clone_test_4);
RUN_TEST(vmo_clone_decommit_test);
RUN_TEST(vmo_clone_commit_test);
RUN_TEST(vmo_clone_rights_test);
RUN_TEST(vmo_clone_resize_clone_hazard);
RUN_TEST(vmo_clone_resize_parent_ok);
RUN_TEST(vmo_clone_no_resize_test);
END_TEST_CASE(vmo_clone_tests)
