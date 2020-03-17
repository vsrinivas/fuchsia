// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYNC_COMPLETION_H_
#define LIB_SYNC_COMPLETION_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

////////////////////////////////////////////////////////////////////////////////
//
// sync_completion_t
//
// :: Overview ::
//
// sync_completion_t objects (henceforth, simply "completions") are lightweight
// in-process events. Conceptually, a completion has an internal state of either
// UNSIGNALED or SIGNALED. Threads in a process may alter this state, check the
// state without blocking, or wait on the completion with an optional
// timeout/deadline until it achieves the SIGNALED state. Completions are
// currently implemented using Zircon futexes.
//
// :: Initialization ::
//
// Completions always start in the UNSIGNALED state. When used in C code, users
// *must* explicitly initialize their completion's state using the
// SYNC_COMPLETION_INIT preprocessor symbol. For example:
//
// ```
// struct my_structure {
//   int32_t a_variable;
//   sync_completion_t a_completion;
// };
//
// void init_my_structure(struct my_structure* s) {
//   s->a_variable = 0;
//   s->a_completion = SYNC_COMPLETION_INIT;
// }
// ```
//
// When used in C++ code, no explicit initialization steps are required. The
// completion's structure will contain a default constructor which properly
// initializes the completion's UNSIGNALED state.
//
// :: Operations ::
//
// The permitted operations on a completion are as follows.
//
// ++ sync_completion_wait ++
// Block a thread with an optional relative timeout until the completion
// achieves the signaled state. If the completion is already in the SIGNALED
// state, the waiting thread will not block.
//
// ++ sync_completion_wait_deadline ++
// Block a thread with an optional absolute deadline timeout until the
// completion achieves the signaled state. If the completion is already in the
// SIGNALED state, the waiting thread will not block.
//
// ++ sync_completion_signal ++
// Change the internal state of a completion to SIGNALED, releasing any threads
// which are currently waiting on it.
//
// ++ sync_completion_reset ++
// Change the internal state of a completion to UNSIGNALED. See also `Avoid
// "Strobing" Signals` (below) for some hazards related to the use of the reset
// operation.
//
// ++ sync_completion_signaled ++
// Observe the internal state of a completion, and return true if it is
// SIGNALED, and false otherwise.
//
// :: No Spurious Wakeups ::
//
// sync_completion_wait() will *only* return:
// ++ if the completion is signaled at some point by a call to
//    sync_completion_signal() (either before or after the call to
//    sync_completion_wait())
// ++ or if the timeout occurs (if using timeouts)
//
// Implementation details:
//
// In general, futex-based concurrency algorithms can cause futex wakeups on
// memory locations that have been deallocated (for example, the standard
// algorithm for a mutex_unlock can do that). This means that futex-based
// concurrency algorithms must be robust against spurious wakeups, because a
// futex memory location may have been previously used, deallocated, and then
// recycled.
//
// Completions guarantee that waiters will not suffer any spurious wakeups,
// provided that the lifetime of the sync_completion_t instance is properly
// respected. For example, pretend we have the following situation.
//
// ```
// void thread_a() {
//   sync_completion_t C = SYNC_COMPLETION_INIT;
//   share_completion_with_thread_b(&C);
//   sync_completion_wait(&C, ZX_TIME_INFINITE);
// }
//
// void thread_b() {
//   sync_completion_t* C = obtain_completion_from_thread_a();
//   sync_completion_signal(C);
// }
// ```
//
// The call to sync_completion_wait is guaranteed to not wake up spuriously,
// even if an unrelated zx_futex_wake operation targeting the same memory
// location happens to occur during the interactions between thread_a and
// thread_b. This behavior **depends** on making sure that the life-cycle of C
// is properly obeyed. After thread_b does sync_completion_signal(C), it must
// not perform any further operations on C. Having signaled C, thread_b has
// caused thread_a to unblock and begin the process of deallocating C. Any
// operations performed on C after this point are racing with the deallocation
// of C and might result in a use-after-free situation. While it is possible
// that thread_b may still be in the call to sync_completion_signal when
// thread_a unwinds and deallocates the completion, no reads or writes from or to
// the completion's state will be made after this point, and the code is safe
// provided that this is the final completion operation.
//
// :: Avoid "Strobing" Signals ::
//
// Users should avoid the pattern of "strobing" a signal operation.
// Specifically, calling sync_completion_signal(C) immediately followed by
// sync_completion_reset(C) is not guaranteed to wake up a waiter, even if the
// user could prove that the waiter is waiting in the completion.
//
// Implementation details:
//
// The following is an example of a sequence which makes use of the strobe
// pattern which can lead a missed event.  It is based on specific details of
// how sync_completion_t is implemented today, and should not be considered to
// be the only way that a signal might end up getting missed, either now or in
// the future.  Again, user should always avoid this "strobing" pattern, it is
// not guaranteed to wake a waiter.
//
// ```
// global sync_completion_t C;
//
// Thread A:
// 1) Wait on C with no timeout.
// 2) Declare victory
//
// Thread B:
// 1) Wait until thread A is blocked on completion C by polling Thread A's state
//    via zx_object_get_info.
// 2) sync_completion_signal(C)
// 3) sync_completion_reset(C)
// 4) sync_completion_wait(C, timeout)
// ```
//
// Step B.1 establishes the fact that (from thread B's perspective) thread A
// managed to observe C in the UNSIGNALED state and join the internal futex wait
// queue used to implement the completion_t. In the process, thread A sets the
// completion_t's internal state to UNSIGNALED_WITH_WAITERS.  The subsequent
// signal/reset/wait operations (steps B.2-B.4), will release thread A from the
// wait queue, but cycle the internal state of the completion_t through the
// SIGNALED and UNSIGNALED states, and back again to the UNSIGNALED_WITH_WAITERS
// state.  If thread B manages to cycle the state all of the way back around to
// UNSIGNALED_WITH_WAITERS before thread A manages to wake up, thread A will see
// the state as UNSIGNALED_WITH_WAITERS and rejoin the wait queue thinking that
// it had been woken by a spurious futex_wake.
//
// Once again, as a general rule the signal/reset pattern shown here should not
// be used with sync_completion_t objects. It is considered to be racy and can
// result in undesired behavior, no matter what steps are taken establish that A
// is waiting before the signal/reset operations take place.
//
// :: Memory Ordering Semantics ::
//
// When a thread transitions the state of a completion from UNSIGNALED to
// SIGNALED by calling sync_completion_signal, the operation provides the same
// guarantees as an atomic modification of the completion state with Release
// semantics. These guarantees do _not_ hold if the completion is already in
// the SIGNALED state when the thread calls sync_completion_signal.
//
// When a thread returns from a sync_completion_wait operation with a status of
// ZX_OK, the operation provides the same guarantees as having atomically
// observed the completion state as being SIGNALED with Acquire semantics.
// These guarantees do _not_ hold if the wait operation times out, or returns
// any other error.
//
// The effects of these guarantees are that write operations to shared memory
// performed by a thread may not be reordered beyond a signal operation which
// successfully transitions a completion from UNSIGNALED to SIGNALED performed
// by the same thread. Likewise, successful wait operations performed by a
// thread against a completion guarantee that subsequent read operations
// performed by that thread may not be reordered before the wait operation.
//
// Taken together, these guarantees make the following common pattern safe.
//
// ```
// typedef struct {
//   uint32_t val;
//   sync_completion_t C;
// } read_operation_t;
//
// void thread_a() {
//   read_operation_t Op;
//   Op.C = SYNC_COMPLETION_INIT;
//
//   send_op_to_thread_b(&Op);
//   sync_completion_wait(&Op.C);
//   do_great_things_with_val(Op.val);
// }
//
// void thread_b() {
//   while (true) {
//     read_operation_t* Op = obtain_read_op();
//     Op->val = compute_a_value_only_thread_b_can_compute();
//     sync_completion_signal(&Op->C);
//   }
// }
// ```
//
// Thread A is guaranteed to see the results written by thread B into the shared
// Op.val member. The modifications performed by thread B may not be reordered
// past the signal operation, and read operations performed by thread A may not
// be moved before the wait operation.
//

__BEGIN_CDECLS

typedef struct sync_completion {
  zx_futex_t futex;

#ifdef __cplusplus
  sync_completion() : futex(0) {}
#endif
} sync_completion_t;

#if !defined(__cplusplus)
#define SYNC_COMPLETION_INIT ((sync_completion_t){0})
#endif

// Returns ZX_ERR_TIMED_OUT if timeout elapses, and ZX_OK if woken by
// a call to sync_completion_signal or if the completion has already been
// signaled.
zx_status_t sync_completion_wait(sync_completion_t* completion, zx_duration_t timeout);

// Returns ZX_ERR_TIMED_OUT if deadline elapses, and ZX_OK if woken by
// a call to sync_completion_signal or if the completion has already been
// signaled.
zx_status_t sync_completion_wait_deadline(sync_completion_t* completion, zx_time_t deadline);

// Awakens all waiters on the completion, and marks it as
// signaled. Waits after this call but before a reset of the
// completion will also see the signal and immediately return.
void sync_completion_signal(sync_completion_t* completion);

// Marks the completion as signaled, but doesn't awaken all waiters right away.
// Instead, all waiters are requeued to the |requeue_target|, and the owner of
// the |requeue_target| is set to |requeue_target_owner|, or to no one if
// ZX_HANDLE_INVALID is passed.
//
// Waits after this call but before a reset of the completion will also see the
// signal and immediately return.
//
// Intended to be used by libsync internally, e.g. the condition variable
// implementation.
void sync_completion_signal_requeue(sync_completion_t* completion, zx_futex_t* requeue_target,
                                    zx_handle_t requeue_target_owner);

// Resets the completion's signaled state to unsignaled.
void sync_completion_reset(sync_completion_t* completion);

// Returns true iff a completion has been signaled.
bool sync_completion_signaled(sync_completion_t* completion);

__END_CDECLS

#endif  // LIB_SYNC_COMPLETION_H_
