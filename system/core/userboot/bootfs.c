// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <zircon/boot/bootdata.h>
#include <zircon/syscalls.h>
#include <string.h>

#pragma GCC visibility pop

void bootfs_mount(zx_handle_t vmar, zx_handle_t log, zx_handle_t vmo, struct bootfs *fs) {
    uint64_t size;
    zx_status_t status = zx_vmo_get_size(vmo, &size);
    check(log, status, "zx_vmo_get_size failed on bootfs vmo\n");
    uintptr_t addr = 0;
    status = zx_vmar_map(vmar, 0, vmo, 0, size, ZX_VM_FLAG_PERM_READ, &addr);
    check(log, status, "zx_vmar_map failed on bootfs vmo\n");
    fs->contents = (const void*)addr;
    fs->len = size;
    status = zx_handle_duplicate(
        vmo,
        ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP |
        ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY,
        &fs->vmo);
    check(log, status, "zx_handle_duplicate failed on bootfs VMO handle\n");
}

void bootfs_unmount(zx_handle_t vmar, zx_handle_t log, struct bootfs *fs) {
    zx_status_t status = zx_vmar_unmap(vmar, (uintptr_t)fs->contents, fs->len);
    check(log, status, "zx_vmar_unmap failed\n");
    status = zx_handle_close(fs->vmo);
    check(log, status, "zx_handle_close failed\n");
}

static const bootfs_entry_t* bootfs_search(zx_handle_t log,
                                           struct bootfs *fs,
                                           const char* filename) {
    const void* p = fs->contents;

    if (fs->len < sizeof(bootfs_header_t))
        fail(log, "bootfs is too small");

    const bootfs_header_t* hdr = p;
    if ((hdr->magic != BOOTFS_MAGIC) || (hdr->dirsize > fs->len))
        fail(log, "bootfs bad magic or size");

    size_t filename_len = strlen(filename) + 1;

    p += sizeof(bootfs_header_t);
    size_t avail = hdr->dirsize;

    while (avail > sizeof(bootfs_entry_t)) {
        const bootfs_entry_t* e = p;

        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (sz > avail))
            fail(log, "bootfs has bogus namelen in header");

        if (e->name_len == filename_len) {
            if (!memcmp(e->name, filename, filename_len))
                return e;
        }

        p += sz;
        avail -= sz;
    }

    return NULL;
}

zx_handle_t bootfs_open(zx_handle_t log, const char* purpose,
                        struct bootfs *fs, const char* filename) {
    printl(log, "searching bootfs for '%s'", filename);

    const bootfs_entry_t* e = bootfs_search(log, fs, filename);
    if (e == NULL) {
        printl(log, "file not found");
        return ZX_HANDLE_INVALID;
    }
    if (e->data_off > fs->len)
        fail(log, "bogus offset in bootfs header!");
    if (fs->len - e->data_off < e->data_len)
        fail(log, "bogus size in bootfs header!");

    // Clone a private copy of the file's subset of the bootfs VMO.
    // TODO(mcgrathr): Create a plain read-only clone when the feature
    // is implemented in the VM.
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_clone(fs->vmo, ZX_VMO_CLONE_COPY_ON_WRITE,
                                      e->data_off, e->data_len, &vmo);
    if (status != ZX_OK)
        fail(log, "zx_vmo_clone failed: %d", status);

    zx_object_set_property(vmo, ZX_PROP_NAME, filename, strlen(filename));

    // Drop unnecessary ZX_RIGHT_WRITE rights.
    // TODO(mcgrathr): Should be superfluous with read-only zx_vmo_clone.
    status = zx_handle_replace(
        vmo,
        ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP |
        ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY,
        &vmo);
    if (status != ZX_OK)
        fail(log, "zx_handle_replace failed: %d", status);

    return vmo;
}
