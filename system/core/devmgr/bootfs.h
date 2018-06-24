// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef struct bootfs_entry bootfs_entry_t;

typedef struct bootfs {
    zx_handle_t vmo;
    uint32_t dirsize;
    void* dir;
} bootfs_t;

// Create a bootfs file system from |vmo|.
//
// Takes ownership of |vmo|.
zx_status_t bootfs_create(bootfs_t* bfs, zx_handle_t vmo);

// Destroys the given bootfs file system.
//
// Closes the |vmo| and unmaps the memory backing the file system.
void bootfs_destroy(bootfs_t* bfs);

// Opens the file with the given |name| in the bootfs file system.
//
// The contents of the file are returned as a copy-on-write VMO clone. Upon
// success, the caller owns the returned |vmo|.
zx_status_t bootfs_open(bootfs_t* bfs, const char* name,
                        zx_handle_t* vmo, uint32_t* size);

// Parses the bootfs file system and calls |cb| for each |bootfs_entry_t|.
zx_status_t bootfs_parse(bootfs_t* bfs,
                         zx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie);

__END_CDECLS
