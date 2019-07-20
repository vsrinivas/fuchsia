// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <trace.h>
#include <zircon/types.h>

#include <object/futex_context.h>
#include <object/process_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

// zx_status_t zx_futex_wait
zx_status_t sys_futex_wait(user_in_ptr<const zx_futex_t> value_ptr, zx_futex_t current_value,
                           zx_handle_t new_futex_owner, zx_time_t deadline) {
  LTRACEF("futex %p current %d\n", value_ptr.get(), current_value);

  ProcessDispatcher* dispatcher = ThreadDispatcher::GetCurrent()->process();
  const TimerSlack slack = dispatcher->GetTimerSlackPolicy();
  const Deadline slackDeadline(deadline, slack);

  return dispatcher->futex_context().FutexWait(value_ptr, current_value, new_futex_owner,
                                               slackDeadline);
}

// zx_status_t zx_futex_wake
zx_status_t sys_futex_wake(user_in_ptr<const zx_futex_t> value_ptr, uint32_t count) {
  LTRACEF("futex %p count %" PRIu32 "\n", value_ptr.get(), count);

  return ProcessDispatcher::GetCurrent()->futex_context().FutexWake(
      value_ptr, count, FutexContext::OwnerAction::RELEASE);
}

// zx_status_t zx_futex_requeue
zx_status_t sys_futex_requeue(user_in_ptr<const zx_futex_t> wake_ptr, uint32_t wake_count,
                              zx_futex_t current_value, user_in_ptr<const zx_futex_t> requeue_ptr,
                              uint32_t requeue_count, zx_handle_t requeue_owner) {
  LTRACEF("futex %p wake_count %" PRIu32
          "current_value %d "
          "requeue_futex %p requeue_count %" PRIu32 "\n",
          wake_ptr.get(), wake_count, current_value, requeue_ptr.get(), requeue_count);

  return ProcessDispatcher::GetCurrent()->futex_context().FutexRequeue(
      wake_ptr, wake_count, current_value, FutexContext::OwnerAction::RELEASE, requeue_ptr,
      requeue_count, requeue_owner);
}

// zx_status_t zx_futex_wake_single_owner
zx_status_t sys_futex_wake_single_owner(user_in_ptr<const zx_futex_t> value_ptr) {
  LTRACEF("futex %p\n", value_ptr.get());

  return ProcessDispatcher::GetCurrent()->futex_context().FutexWake(
      value_ptr, 1u, FutexContext::OwnerAction::ASSIGN_WOKEN);
}

// zx_status_t zx_futex_requeue_single_owner
zx_status_t sys_futex_requeue_single_owner(user_in_ptr<const zx_futex_t> wake_ptr,
                                           zx_futex_t current_value,
                                           user_in_ptr<const zx_futex_t> requeue_ptr,
                                           uint32_t requeue_count, zx_handle_t requeue_owner) {
  LTRACEF("futex %p current_value %d requeue_futex %p requeue_count %" PRIu32 "\n", wake_ptr.get(),
          current_value, requeue_ptr.get(), requeue_count);

  return ProcessDispatcher::GetCurrent()->futex_context().FutexRequeue(
      wake_ptr, 1u, current_value, FutexContext::OwnerAction::ASSIGN_WOKEN, requeue_ptr,
      requeue_count, requeue_owner);
}

zx_status_t sys_futex_get_owner(user_in_ptr<const zx_futex_t> value_ptr,
                                user_out_ptr<zx_koid_t> koid) {
  LTRACEF("futex %p\n", value_ptr.get());
  return ProcessDispatcher::GetCurrent()->futex_context().FutexGetOwner(value_ptr, koid);
}
