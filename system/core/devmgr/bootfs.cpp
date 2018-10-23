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

#include <fbl/algorithm.h>

#include "bootfs.h"

namespace devmgr {

Bootfs::~Bootfs() = default;

zx_status_t Bootfs::Create(zx::vmo vmo, Bootfs* bfs_out) {
    bootfs_header_t hdr;
    zx_status_t r = vmo.read(&hdr, 0, sizeof(hdr));
    if (r < 0) {
        printf("bootfs_create: couldn't read boot_data - %d\n", r);
        return r;
    }
    if (hdr.magic != BOOTFS_MAGIC) {
        printf("bootfs_create: incorrect bootdata header: %x\n", hdr.magic);
        return ZX_ERR_IO;
    }
    zx_vaddr_t addr;
    if ((r = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo.get(), 0,
                         sizeof(hdr) + hdr.dirsize,
                         &addr)) < 0) {
        printf("boofts_create: couldn't map directory: %d\n", r);
        return r;
    }
    auto dir = reinterpret_cast<char*>(addr) + sizeof(hdr);
    *bfs_out = Bootfs(fbl::move(vmo), hdr.dirsize, dir);
    return ZX_OK;
}

void Bootfs::Destroy() {
    vmo_.reset();
    zx_vmar_unmap(zx_vmar_root_self(),
                  (uintptr_t)dir_ - sizeof(bootfs_header_t),
                  MappingSize());
}

zx_status_t Bootfs::Parse(Callback callback, void* cookie) {
    size_t avail = dirsize_;
    auto* p = static_cast<char*>(dir_);
    zx_status_t r;
    while (avail > sizeof(bootfs_entry_t)) {
        auto e = reinterpret_cast<bootfs_entry_t*>(p);
        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) ||
            (e->name[e->name_len - 1] != 0) || (sz > avail)) {
            printf("bootfs: bogus entry!\n");
            return ZX_ERR_IO;
        }
        if ((r = callback(cookie, e)) != ZX_OK) {
            return r;
        }
        p += sz;
        avail -= sz;
    }
    return ZX_OK;
}

static zx_status_t CloneVmo(const char* name, size_t name_len, const bootfs_entry_t& e,
                            const zx::vmo& original_vmo, zx::vmo* vmo_out, uint32_t* size_out) {
    zx::vmo vmo;
    zx_status_t r;

    // Clone a private copy of the file's subset of the bootfs VMO.
    // TODO(mcgrathr): Create a plain read-only clone when the feature
    // is implemented in the VM.
    if ((r = original_vmo.clone(ZX_VMO_CLONE_COPY_ON_WRITE,
                                e.data_off, e.data_len, &vmo)) != ZX_OK) {
        return r;
    }

    vmo.set_property(ZX_PROP_NAME, name, name_len - 1);

    // Drop unnecessary ZX_RIGHT_WRITE rights.
    // TODO(mcgrathr): Should be superfluous with read-only zx_vmo_clone.
    if ((r = vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHT_READ |
                         ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY,
                         &vmo)) != ZX_OK) {
        return r;
    }

    // TODO(mdempsky): Restrict to bin/ and lib/.
    if ((vmo.replace_as_executable(zx::handle(), &vmo)) != ZX_OK) {
        return r;
    }

    *vmo_out = fbl::move(vmo);
    if (size_out) {
        *size_out = e.data_len;
    }
    return ZX_OK;
}

zx_status_t Bootfs::Open(const char* name, zx::vmo* vmo_out, uint32_t* size_out) {
    size_t name_len = strlen(name) + 1;
    size_t avail = dirsize_;
    auto p = static_cast<char*>(dir_);
    bootfs_entry_t* e;
    while (avail > sizeof(bootfs_entry_t)) {
        e = reinterpret_cast<bootfs_entry_t*>(p);
        size_t sz = BOOTFS_RECSIZE(e);
        if ((e->name_len < 1) || (e->name_len > BOOTFS_MAX_NAME_LEN) ||
            (e->name[e->name_len - 1] != 0) || (sz > avail)) {
            printf("bootfs: bogus entry!\n");
            return ZX_ERR_IO;
        }
        if ((name_len == e->name_len) && (memcmp(name, e->name, name_len) == 0)) {
            return CloneVmo(name, name_len, *e, vmo_, vmo_out, size_out);
        }
        p += sz;
        avail -= sz;
    }
    printf("bootfs_open: '%s' not found\n", name);
    return ZX_ERR_NOT_FOUND;
}

zx::vmo Bootfs::DuplicateVmo() {
    zx::vmo duplicate;
    vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
    return duplicate;
}

} // namespace devmgr
