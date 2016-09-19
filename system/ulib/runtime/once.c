// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/once.h>

#include <magenta/syscalls.h>
#include <stdatomic.h>
#include <stdint.h>

// The default state must be zero to match MXR_ONCE_INIT.
enum once_state {
    UNUSED = 0,
    RUNNING,
    RAN,
    WAITING,
};

void mxr_once(mxr_once_t* once, void (*func)(void)) {
    while (1) {
        int old_state = UNUSED;
        if (atomic_compare_exchange_strong(&once->futex,
                                           &old_state, RUNNING)) {
            (*func)();
            if (atomic_exchange(&once->futex, RAN) == WAITING) {
                mx_status_t status = _mx_futex_wake(&once->futex, UINT32_MAX);
                if (status != NO_ERROR)
                    __builtin_trap();
            }
            return;
        } else {
            switch ((enum once_state)old_state) {
            case RAN:
                return;

            case RUNNING:
                if (!atomic_compare_exchange_strong(&once->futex,
                                                    &old_state, WAITING))
                    continue;
            // Fall through.

            case WAITING:;
                mx_status_t status =
                    _mx_futex_wait(&once->futex, WAITING, MX_TIME_INFINITE);
                if (status != NO_ERROR && status != ERR_BAD_STATE)
                    __builtin_trap();
                continue;

            case UNUSED:
            // This should have triggered the 'if' branch.

            default:
                __builtin_unreachable();
            }
        }
    }
}
