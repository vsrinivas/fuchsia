// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>

void print(mx_handle_t log, const char* s, ...) __attribute__((sentinel));
_Noreturn void fail(mx_handle_t log, mx_status_t status, const char* msg);

static inline void check(mx_handle_t log,
                         mx_status_t status, const char* msg) {
    if (status != NO_ERROR)
        fail(log, status, msg);
}

#pragma GCC visibility pop
