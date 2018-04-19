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

zx_status_t bootfs_create(bootfs_t* bfs, zx_handle_t vmo);
void bootfs_destroy(bootfs_t* bfs);
zx_status_t bootfs_open(bootfs_t* bfs, const char* name, zx_handle_t* vmo);
zx_status_t bootfs_parse(bootfs_t* bfs,
                         zx_status_t (*cb)(void* cookie, const bootfs_entry_t* entry),
                         void* cookie);

__END_CDECLS
