// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <runtime/once.h>

#include <magenta/syscalls.h>
#include <system/atomic.h>
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
        if (atomic_cmpxchg(&once->futex, &old_state, RUNNING)) {
            (*func)();
            if (atomic_swap(&once->futex, RAN) == WAITING) {
                mx_status_t status = mx_futex_wake(&once->futex, UINT32_MAX);
                if (status != NO_ERROR)
                    __builtin_trap();
            }
            return;
        } else {
            switch ((enum once_state)old_state) {
            case RAN:
                return;

            case RUNNING:
                if (!atomic_cmpxchg(&once->futex, &old_state, WAITING))
                    continue;
                // Fall through.

            case WAITING:;
                mx_status_t status =
                    mx_futex_wait(&once->futex, WAITING, MX_TIME_INFINITE);
                if (status != NO_ERROR && status != ERR_BUSY)
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
