// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/sanitizer.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <stddef.h>
#include <threads.h>

#include "dynlink.h"

// TODO(mcgrathr): For now, just use kernel log channels.
// They do the timestamp, process/thread tagging for us.
static zx_handle_t sanitizer_log;
static once_flag once = ONCE_FLAG_INIT;
static void create_sanitizer_log(void) {
    zx_status_t status = _zx_log_create(0, &sanitizer_log);
    if (status != ZX_OK)
        __builtin_trap();
}

#define MAX_DATA (ZX_LOG_RECORD_MAX - offsetof(zx_log_record_t, data))

void __sanitizer_log_write(const char *buffer, size_t len) {
    call_once(&once, &create_sanitizer_log);

    _dl_log_unlogged();

    while (len > 0) {
        size_t chunk = len < MAX_DATA ? len : MAX_DATA;
        zx_status_t status = _zx_log_write(sanitizer_log, chunk, buffer, 0);
        if (status != ZX_OK)
            __builtin_trap();
        buffer += chunk;
        len -= chunk;
    }
}
