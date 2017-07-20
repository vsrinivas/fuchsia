// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/sanitizer.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <stddef.h>
#include <threads.h>

#include "dynlink.h"

// TODO(mcgrathr): For now, just use kernel log channels.
// They do the timestamp, process/thread tagging for us.
static mx_handle_t sanitizer_log;
static once_flag once = ONCE_FLAG_INIT;
static void create_sanitizer_log(void) {
    mx_status_t status = _mx_log_create(0, &sanitizer_log);
    if (status != MX_OK)
        __builtin_trap();
}

#define MAX_DATA (MX_LOG_RECORD_MAX - offsetof(mx_log_record_t, data))

void __sanitizer_log_write(const char *buffer, size_t len) {
    call_once(&once, &create_sanitizer_log);

    _dl_log_unlogged();

    while (len > 0) {
        size_t chunk = len < MAX_DATA ? len : MAX_DATA;
        mx_status_t status = _mx_log_write(sanitizer_log, chunk, buffer, 0);
        if (status != MX_OK)
            __builtin_trap();
        buffer += chunk;
        len -= chunk;
    }
}
