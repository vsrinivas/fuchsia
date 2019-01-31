// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/bootfs/parser.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/bootdata.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace bootfs {

Parser::~Parser() {
    if (dir_) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(dir_) - sizeof(bootfs_header_t);
        zx::vmar::root_self()->unmap(addr, MappingSize());
    }
}

zx_status_t Parser::Init(zx::unowned_vmo vmo) {
    if (dir_ != nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    bootfs_header_t hdr;
    zx_status_t r = vmo->read(&hdr, 0, sizeof(hdr));
    if (r != ZX_OK) {
        printf("Parser::Init: couldn't read boot_data - %d\n", r);
        return r;
    }
    if (hdr.magic != BOOTFS_MAGIC) {
        printf("Parser::Init: incorrect bootdata header: %x\n", hdr.magic);
        return ZX_ERR_IO;
    }
    zx_vaddr_t addr = 0;
    if ((r = zx::vmar::root_self()->map(0, *vmo, 0, sizeof(hdr) + hdr.dirsize,
                                        ZX_VM_PERM_READ, &addr)) != ZX_OK) {
        printf("Parser::Init: couldn't map directory: %d\n", r);
        return r;
    }
    dirsize_ = hdr.dirsize;
    dir_ = reinterpret_cast<char*>(addr) + sizeof(hdr);
    return ZX_OK;
}

zx_status_t Parser::Parse(Callback callback) {
    if (dir_ == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
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
        if ((r = callback(e)) != ZX_OK) {
            return r;
        }
        p += sz;
        avail -= sz;
    }
    return ZX_OK;
}

} // namespace bootfs
