// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>

#include <arch/ops.h>
#include <lib/ktrace.h>
#include <mxtl/atomic.h>

// The first 1K koids are reserved.
static mxtl::atomic<mx_koid_t> global_koid(1024ULL);

mx_koid_t Dispatcher::GenerateKernelObjectId() {
    return global_koid.fetch_add(1ULL);
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

status_t Dispatcher::add_observer(StateObserver* observer) {
    auto state_tracker = get_state_tracker();
    if (!state_tracker)
        return ERR_NOT_SUPPORTED;
    state_tracker->AddObserver(observer, nullptr);
    return NO_ERROR;
}

status_t Dispatcher::user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer) {
    if (peer)
        return ERR_NOT_SUPPORTED;

    auto state_tracker = get_state_tracker();
    if (!state_tracker)
        return ERR_NOT_SUPPORTED;

    // Generic objects can set all USER_SIGNALs. Particular object
    // types (events and eventpairs) may be able to set more.
    if ((set_mask & ~MX_USER_SIGNAL_ALL) || (clear_mask & ~MX_USER_SIGNAL_ALL))
        return ERR_INVALID_ARGS;

    state_tracker->UpdateState(clear_mask, set_mask);
    return NO_ERROR;
}

