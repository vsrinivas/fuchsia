// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libc.h"
#include <zircon/process.h>
#include <zircon/types.h>

#define _ALL_SOURCE  // For MTX_INIT
#include <threads.h>

static mtx_t startup_handles_lock = MTX_INIT;
static uint32_t startup_handles_num;
static zx_handle_t* startup_handles;
static uint32_t* startup_handles_info;

static void shave_front(void) {
    while (startup_handles_num > 0 &&
           startup_handles[0] == ZX_HANDLE_INVALID) {
        --startup_handles_num;
        ++startup_handles;
        ++startup_handles_info;
    }
}

static void shave_back(void) {
    while (startup_handles_num > 0 &&
           startup_handles[startup_handles_num - 1] == ZX_HANDLE_INVALID)
        --startup_handles_num;
}

// This is called only once at startup, so it doesn't need locking.
ATTR_LIBC_VISIBILITY
void __libc_startup_handles_init(
    uint32_t nhandles, zx_handle_t handles[], uint32_t handle_info[]) {
    startup_handles_num = nhandles;
    startup_handles = handles;
    startup_handles_info = handle_info;
    shave_front();
    shave_back();
}

zx_handle_t zx_get_startup_handle(uint32_t hnd_info) {
    zx_handle_t result = ZX_HANDLE_INVALID;
    mtx_lock(&startup_handles_lock);
    for (uint32_t i = 0; i < startup_handles_num; ++i) {
        if (startup_handles_info[i] == hnd_info) {
            result = startup_handles[i];
            startup_handles[i] = ZX_HANDLE_INVALID;
            startup_handles_info[i] = 0;
            if (i == 0)
                shave_front();
            else if (i == startup_handles_num - 1)
                shave_back();
            break;
        }
    }
    mtx_unlock(&startup_handles_lock);
    return result;
}
