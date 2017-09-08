// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/util.h>

mx_status_t bootfs_create(bootfs_t* bfs, mx_handle_t vmo) {
    bootfs_header_t hdr;
    size_t rlen;
    mx_status_t r = mx_vmo_read(vmo, &hdr, 0, sizeof(hdr), &rlen);
    if ((r < 0) || (rlen < sizeof(hdr))) {
        printf("bootfs_create: couldn't read boot_data - %d\n", r);
        return r;
    }
    if (hdr.magic != BOOTFS_MAGIC) {
        printf("bootfs_create: incorrect bootdata header: %x\n", hdr.magic);
        return MX_ERR_IO;
    }
    if ((r = mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &bfs->vmo)) < 0) {
        return r;
    }
    uintptr_t addr;
    if ((r = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0,
                         sizeof(hdr) + hdr.dirsize,
                         MX_VM_FLAG_PERM_READ, &addr)) < 0) {
        printf("boofts_create: couldn't map directory: %d\n", r);
        mx_handle_close(bfs->vmo);
        return r;
    }
    bfs->dirsize = hdr.dirsize;
    bfs->dir = (void*)addr + sizeof(hdr);
    return MX_OK;
}

void bootfs_destroy(bootfs_t* bfs) {
    mx_handle_close(bfs->vmo);
    mx_vmar_unmap(mx_vmar_root_self(),
                  (uintptr_t)bfs->dir - sizeof(bootfs_header_t),
                  bfs->dirsize);
}

mx_status_t bootfs_parse(bootfs_t* bfs,
                         mx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie) {
    size_t avail = bfs->dirsize;
    void* p = bfs->dir;
    mx_status_t r;
    while (avail > sizeof(bootfs_entry_t)) {
        bootfs_entry_t* e = p;
        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) ||
            (e->name[e->name_len - 1] != 0) || (sz > avail)) {
            printf("bootfs: bogus entry!\n");
            return MX_ERR_IO;
        }
        if ((r = cb(cookie, e)) != MX_OK) {
            return r;
        }
        p += sz;
        avail -= sz;
    }
    return MX_OK;
}

mx_status_t bootfs_open(bootfs_t* bfs, const char* name, mx_handle_t* vmo_out) {
    size_t name_len = strlen(name) + 1;
    size_t avail = bfs->dirsize;
    void* p = bfs->dir;
    bootfs_entry_t* e;
    while (avail > sizeof(bootfs_entry_t)) {
        e = p;
        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) ||
            (e->name[e->name_len - 1] != 0) || (sz > avail)) {
            printf("bootfs: bogus entry!\n");
            return MX_ERR_IO;
        }
        if ((name_len == e->name_len) && (memcmp(name, e->name, name_len) == 0)) {
            goto found;
        }
        p += sz;
        avail -= sz;
    }
    printf("bootfs_open: '%s' not found\n", name);
    return MX_ERR_NOT_FOUND;

found:;
    mx_handle_t vmo;
    mx_status_t r;

    // Clone a private copy of the file's subset of the bootfs VMO.
    // TODO(mcgrathr): Create a plain read-only clone when the feature
    // is implemented in the VM.
    if ((r = mx_vmo_clone(bfs->vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                          e->data_off, e->data_len, &vmo)) != MX_OK) {
        return r;
    }

    mx_object_set_property(vmo, MX_PROP_NAME, name, name_len - 1);

    // Drop unnecessary MX_RIGHT_WRITE rights.
    // TODO(mcgrathr): Should be superfluous with read-only mx_vmo_clone.
    if ((r = mx_handle_replace(vmo,
                               MX_RIGHT_READ | MX_RIGHT_EXECUTE |
                               MX_RIGHT_MAP | MX_RIGHT_TRANSFER |
                               MX_RIGHT_DUPLICATE | MX_RIGHT_GET_PROPERTY,
                               &vmo)) != MX_OK) {
        return r;
    }

    *vmo_out = vmo;
    return MX_OK;
}