// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>

#include <arch/ops.h>
#include <lib/ktrace.h>

// The first 1K koids are reserved.
static mx_koid_t global_koid = 1024ULL;

mx_koid_t Dispatcher::GenerateKernelObjectId() {
    return atomic_add_u64(&global_koid, 1ULL);
}

Dispatcher::Dispatcher()
    : koid_(GenerateKernelObjectId()),
      handle_count_(0u) {
}

Dispatcher::~Dispatcher() {
#if WITH_LIB_KTRACE
    ktrace(TAG_OBJECT_DELETE, (uint32_t)koid_, 0, 0, 0);
#endif
}

void Dispatcher::add_handle() {
    atomic_add_relaxed(&handle_count_, 1);
}

void Dispatcher::remove_handle() {
    if (atomic_add_release(&handle_count_, -1) == 1) {
        atomic_fence_acquire();
        on_zero_handles();
    }
}

status_t Dispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask) {
    auto state_tracker = get_state_tracker();
    if (!state_tracker)
        return ERR_NOT_SUPPORTED;

    if ((set_mask & ~MX_EVENT_SIGNAL_MASK) || (clear_mask & ~MX_EVENT_SIGNAL_MASK))
        return ERR_INVALID_ARGS;

    state_tracker->UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}
