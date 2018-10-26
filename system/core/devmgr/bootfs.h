// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/boot/bootdata.h>
#include <zircon/types.h>
#include <zircon/compiler.h>
#include <stdint.h>

#include <fbl/algorithm.h>
#include <lib/zx/vmo.h>

namespace devmgr {

class Bootfs {
public:
    // Create an empty Bootfs with no backing |vmo_|.
    Bootfs() : Bootfs(zx::vmo(), 0, nullptr) {}

    // Destroys the underlying |vmo_|, but does not unmap the memory
    // backing the filesystem.
    ~Bootfs();

    // Destroys the given bootfs file system.
    //
    // Closes the underlying |vmo_| and unmaps the memory backing the file system.
    void Destroy();

    // Create a bootfs file system from |vmo|.
    //
    // Takes ownership of |vmo|.
    static zx_status_t Create(zx::vmo vmo, Bootfs* bfs_out);

    // Opens the file with the given |name| in the bootfs file system.
    //
    // The contents of the file are returned as a copy-on-write VMO clone. Upon
    // success, the caller owns the returned |vmo_out|.
    zx_status_t Open(const char* name, zx::vmo* vmo_out, uint32_t* size_out);

    // Parses the bootfs file system and calls |callback| for each |bootfs_entry_t|.
    using Callback = zx_status_t (void* cookie, const bootfs_entry_t* entry);
    zx_status_t Parse(Callback callback, void* cookie);

    // Attempts to duplicate the underling |vmo_| with the same
    // rights, and returns it. Returns ZX_HANDLE_INVALID on any
    // failure to do so.
    zx::vmo DuplicateVmo();

private:
    Bootfs(zx::vmo vmo, uint32_t dirsize, void* dir) :
        vmo_(fbl::move(vmo)), dirsize_(dirsize), dir_(dir) {}

    Bootfs(const Bootfs&) = delete;
    Bootfs& operator=(const Bootfs&) = delete;

    Bootfs(Bootfs&&) = default;
    Bootfs& operator=(Bootfs&&) = default;

    size_t MappingSize() const {
        return dirsize_ + sizeof(bootfs_header_t);
    }

    zx::vmo vmo_;
    uint32_t dirsize_;
    void* dir_;
};

} // namespace devmgr
