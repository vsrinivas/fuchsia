// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_

#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/thread.h>
#include <kernel/timer.h>

#define EVENT_MAGIC (0x65766E74)  // "evnt"

typedef struct event {
  int magic;
  zx_status_t result;  // INT_MAX means not signaled
  uint flags;
  wait_queue_t wait;
} event_t;

#define EVENT_FLAG_AUTOUNSIGNAL 1

#define EVENT_INITIAL_VALUE(e, initial, _flags)                                     \
  {                                                                                 \
    .magic = EVENT_MAGIC, .result = (initial) ? ZX_OK : INT_MAX, .flags = (_flags), \
    .wait = WAIT_QUEUE_INITIAL_VALUE((e).wait),                                     \
  }

// Rules for Events:
// - Events may be signaled from interrupt context *but* the reschedule
//   parameter must be false in that case.
// - Events may not be waited upon from interrupt context.
// - Events without FLAG_AUTOUNSIGNAL:
//   - Wake up any waiting threads when signaled.
//   - Continue to do so (no threads will wait) until unsignaled.
//   - Stores a single result value when first signaled. This result is
//     returned to waiters and cleared when unsignaled.
// - Events with FLAG_AUTOUNSIGNAL:
//   - If one or more threads are waiting when signaled, one thread will
//     be woken up and return.  The signaled state will not be set.
//   - If no threads are waiting when signaled, the Event will remain
//     in the signaled state until a thread attempts to wait (at which
//     time it will unsignal atomicly and return immediately) or
//     event_unsignal() is called.
//   - Stores a single result value when signaled until a thread is woken.

static inline void event_init(event_t* e, bool initial, uint flags) {
  *e = (event_t)EVENT_INITIAL_VALUE(*e, initial, flags);
}
void event_destroy(event_t*);

// Wait until deadline
// Interruptable arg allows it to return early with ZX_ERR_INTERNAL_INTR_KILLED if thread
// is signaled for kill or with ZX_ERR_INTERNAL_INTR_RETRY if the thread is suspended.
zx_status_t event_wait_deadline(event_t*, zx_time_t, bool interruptable);

// Wait until the event occurs, the deadline has elapsed, or the thread is interrupted.
zx_status_t event_wait_interruptable(event_t* e, const Deadline& deadline);

// no deadline, non interruptable version of the above.
static inline zx_status_t event_wait(event_t* e) {
  return event_wait_deadline(e, ZX_TIME_INFINITE, false);
}

// Version of event_wait_deadline that ignores existing signals in
// |signal_mask|. There is no deadline, and the caller must be interruptable.
zx_status_t event_wait_with_mask(event_t*, uint signal_mask);

int event_signal_etc(event_t*, bool reschedule, zx_status_t result);
int event_signal(event_t*, bool reschedule);
int event_signal_thread_locked(event_t*) TA_REQ(thread_lock);
zx_status_t event_unsignal(event_t*);

static inline bool event_initialized(const event_t* e) { return e->magic == EVENT_MAGIC; }

static inline bool event_signaled(const event_t* e) { return e->result != INT_MAX; }

// C++ wrapper. This should be waited on from only a single thread, but may be
// signaled from many threads (Signal() is thread-safe).
class Event {
 public:
  constexpr explicit Event(uint32_t opts = 0) : event_(EVENT_INITIAL_VALUE(event_, false, opts)) {}
  ~Event() { event_destroy(&event_); }

  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;

  // Returns:
  // ZX_OK - signaled
  // ZX_ERR_TIMED_OUT - time out expired
  // ZX_ERR_INTERNAL_INTR_KILLED - thread killed
  // ZX_ERR_INTERNAL_INTR_RETRY - thread is suspended
  // Or the |status| which the caller specified in Event::Signal(status)
  zx_status_t Wait(const Deadline& deadline) { return event_wait_interruptable(&event_, deadline); }

  // Same as Wait() but waits forever and gives a mask of signals to ignore.
  zx_status_t WaitWithMask(uint signal_mask) { return event_wait_with_mask(&event_, signal_mask); }

  void Signal(zx_status_t status = ZX_OK) { event_signal_etc(&event_, true, status); }

  void SignalThreadLocked() TA_REQ(thread_lock) { event_signal_thread_locked(&event_); }

  void SignalNoResched() { event_signal(&event_, false); }

  zx_status_t Unsignal() { return event_unsignal(&event_); }

 private:
  event_t event_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_
