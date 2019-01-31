// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

void _zx_cprng_draw(void* buffer, size_t len) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    while (len != 0) {
        size_t chunk = len;
        if (chunk > ZX_CPRNG_DRAW_MAX_LEN)
            chunk = ZX_CPRNG_DRAW_MAX_LEN;
        zx_status_t status = SYSCALL_zx_cprng_draw_once(ptr, chunk);
        // zx_cprng_draw_once shouldn't fail unless given bogus arguments.
        if (unlikely(status != ZX_OK)) {
            // We loop around __builtin_trap in case __builtin_trap doesn't
            // actually terminate the process.
            while (true) {
                __builtin_trap();
            }
        }
        ptr += chunk;
        len -= chunk;
    }
}

VDSO_INTERFACE_FUNCTION(zx_cprng_draw);
