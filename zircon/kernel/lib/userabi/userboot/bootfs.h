// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include <zircon/types.h>

struct bootfs {
    zx_handle_t vmo;
    const std::byte* contents;
    size_t len;
};

void bootfs_mount(zx_handle_t vmar, zx_handle_t log, zx_handle_t vmo, struct bootfs* fs);
void bootfs_unmount(zx_handle_t vmar, zx_handle_t log, struct bootfs* fs);

zx_handle_t bootfs_open(zx_handle_t log, const char* purpose,
                        struct bootfs* fs, const char* root_prefix,
                        const char* filename);
