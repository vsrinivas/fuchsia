// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>
#include <stddef.h>
#include <stdint.h>

struct bootfs {
    const uint8_t* contents;
    size_t len;
};

void bootfs_mount(mx_handle_t vmar, mx_handle_t log, mx_handle_t vmo, struct bootfs *fs);
void bootfs_unmount(mx_handle_t vmar, mx_handle_t log, struct bootfs *fs);

mx_handle_t bootfs_open(mx_handle_t log, struct bootfs *fs,
                        const char* filename);

#pragma GCC visibility pop
