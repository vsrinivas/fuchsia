// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/boot/bootdata.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "bootfs.h"

zx_status_t bootfs_create(bootfs_t* bfs, zx_handle_t vmo) {
    bootfs_header_t hdr;
    zx_status_t r = zx_vmo_read(vmo, &hdr, 0, sizeof(hdr));
    if (r < 0) {
        printf("bootfs_create: couldn't read boot_data - %d\n", r);
        return r;
    }
    if (hdr.magic != BOOTFS_MAGIC) {
        printf("bootfs_create: incorrect bootdata header: %x\n", hdr.magic);
        return ZX_ERR_IO;
    }
    if ((r = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &bfs->vmo)) < 0) {
        return r;
    }
    uintptr_t addr;
    if ((r = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0,
                         sizeof(hdr) + hdr.dirsize,
                         ZX_VM_FLAG_PERM_READ, &addr)) < 0) {
        printf("boofts_create: couldn't map directory: %d\n", r);
        zx_handle_close(bfs->vmo);
        return r;
    }
    bfs->dirsize = hdr.dirsize;
    bfs->dir = (void*)addr + sizeof(hdr);
    return ZX_OK;
}

void bootfs_destroy(bootfs_t* bfs) {
    zx_handle_close(bfs->vmo);
    zx_vmar_unmap(zx_vmar_root_self(),
                  (uintptr_t)bfs->dir - sizeof(bootfs_header_t),
                  bfs->dirsize);
}

zx_status_t bootfs_parse(bootfs_t* bfs,
                         zx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie) {
    size_t avail = bfs->dirsize;
    void* p = bfs->dir;
    zx_status_t r;
    while (avail > sizeof(bootfs_entry_t)) {
        bootfs_entry_t* e = p;
        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) ||
            (e->name[e->name_len - 1] != 0) || (sz > avail)) {
            printf("bootfs: bogus entry!\n");
            return ZX_ERR_IO;
        }
        if ((r = cb(cookie, e)) != ZX_OK) {
            return r;
        }
        p += sz;
        avail -= sz;
    }
    return ZX_OK;
}

zx_status_t bootfs_open(bootfs_t* bfs, const char* name, zx_handle_t* vmo_out) {
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
            return ZX_ERR_IO;
        }
        if ((name_len == e->name_len) && (memcmp(name, e->name, name_len) == 0)) {
            goto found;
        }
        p += sz;
        avail -= sz;
    }
    printf("bootfs_open: '%s' not found\n", name);
    return ZX_ERR_NOT_FOUND;

found:;
    zx_handle_t vmo;
    zx_status_t r;

    // Clone a private copy of the file's subset of the bootfs VMO.
    // TODO(mcgrathr): Create a plain read-only clone when the feature
    // is implemented in the VM.
    if ((r = zx_vmo_clone(bfs->vmo, ZX_VMO_CLONE_COPY_ON_WRITE,
                          e->data_off, e->data_len, &vmo)) != ZX_OK) {
        return r;
    }

    zx_object_set_property(vmo, ZX_PROP_NAME, name, name_len - 1);

    // Drop unnecessary ZX_RIGHT_WRITE rights.
    // TODO(mcgrathr): Should be superfluous with read-only zx_vmo_clone.
    if ((r = zx_handle_replace(vmo,
                               ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_EXECUTE |
                               ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY,
                               &vmo)) != ZX_OK) {
        return r;
    }

    *vmo_out = vmo;
    return ZX_OK;
}
