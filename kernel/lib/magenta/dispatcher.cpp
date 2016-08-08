// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/dispatcher.h>

#include <arch/ops.h>

static mx_koid_t global_koid = 255ULL;

mx_koid_t Dispatcher::GenerateKernelObjectId() {
    return atomic_add_u64(&global_koid, 1ULL);
}

Dispatcher::Dispatcher()
    : koid_(GenerateKernelObjectId()),
      handle_count_(0u) {
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
