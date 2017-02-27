// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <trace.h>

#include <lib/ktrace.h>

#include <magenta/process_dispatcher.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_futex_wait(mx_futex_t* value_ptr, int current_value, mx_time_t timeout) {
    LTRACEF("futex %p current %d\n", value_ptr, current_value);

    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWait(
        make_user_ptr(value_ptr), current_value, timeout);
}

mx_status_t sys_futex_wake(const mx_futex_t* value_ptr, uint32_t count) {
    LTRACEF("futex %p count %" PRIu32 "\n", value_ptr, count);

    return ProcessDispatcher::GetCurrent()->futex_context()->FutexWake(
        make_user_ptr(value_ptr), count);
}

mx_status_t sys_futex_requeue(mx_futex_t* wake_ptr, uint32_t wake_count, int current_value,
                              mx_futex_t* requeue_ptr, uint32_t requeue_count) {
    LTRACEF("futex %p wake_count %" PRIu32 "current_value %d "
           "requeue_futex %p requeue_count %" PRIu32 "\n",
           wake_ptr, wake_count, current_value, requeue_ptr, requeue_count);

    return ProcessDispatcher::GetCurrent()->futex_context()->FutexRequeue(
        make_user_ptr(wake_ptr), wake_count, current_value,
        make_user_ptr(requeue_ptr), requeue_count);
}
