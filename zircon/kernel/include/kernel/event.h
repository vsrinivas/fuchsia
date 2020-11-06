// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_

#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

// Rules for Events and AutounsignalEvents:
// - Events may be signaled from interrupt context *but* the reschedule
//   parameter must be false in that case.
// - Events may not be waited upon from interrupt context.
// - Standard Events:
//   - Wake up any waiting threads when signaled.
//   - Continue to do so (no threads will wait) until unsignaled.
//   - Stores a single result value when first signaled. This result is
//     returned to waiters and cleared when unsignaled.
// - AutounsignalEvents:
//   - If one or more threads are waiting when signaled, one thread will
//     be woken up and return.  The signaled state will not be set.
//   - If no threads are waiting when signaled, the AutounsignalEvent will remain
//     in the signaled state until a thread attempts to wait (at which
//     time it will unsignal atomicly and return immediately) or
//     AutounsignalEvent::Unsignal() is called.
//   - Stores a single result value when signaled until a thread is woken.

class Event {
 public:
  constexpr explicit Event(bool initial = false) : Event(initial, Flags(0)) {}

  ~Event();

  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;

  // Event::Wait() and other Wait functions will return ZX_OK if already signaled, even if deadline
  // has passed. They will return ZX_ERR_TIMED_OUT after the deadline passes if the event has not
  // been signaled.

  // Returns:
  // ZX_OK - signaled
  // ZX_ERR_TIMED_OUT - time out expired
  // ZX_ERR_INTERNAL_INTR_KILLED - thread killed
  // ZX_ERR_INTERNAL_INTR_RETRY - thread is suspended
  // Or the |status| which the caller specified in Event::Signal(status)
  zx_status_t Wait(const Deadline& deadline) { return WaitWorker(deadline, Interruptible::Yes, 0); }

  // Same as Wait() but waits forever and gives a mask of signals to ignore.
  // The caller must be interruptible.
  zx_status_t WaitWithMask(uint signal_mask) {
    return WaitWorker(Deadline::infinite(), Interruptible::Yes, signal_mask);
  }

  // no deadline, non interruptible version of the above.
  zx_status_t Wait() { return WaitWorker(Deadline::infinite(), Interruptible::No, 0); }

  // Wait until deadline
  // Interruptible arg allows it to return early with ZX_ERR_INTERNAL_INTR_KILLED if thread
  // is signaled for kill or with ZX_ERR_INTERNAL_INTR_RETRY if the thread is suspended.
  zx_status_t WaitDeadline(zx_time_t deadline, Interruptible interruptible) {
    return WaitWorker(Deadline::no_slack(deadline), interruptible, 0);
  }

  void Signal(zx_status_t status = ZX_OK) { SignalEtc(true, status); }

  void SignalThreadLocked() TA_REQ(thread_lock);

  void SignalNoResched() { SignalEtc(false); }

  void SignalEtc(bool reschedule, zx_status_t wait_result = ZX_OK);

  zx_status_t Unsignal() TA_EXCL(thread_lock);

 protected:
  enum Flags : uint32_t {
    AUTOUNSIGNAL = 1,
  };

  // Only our AutounsignalEvent subclass can also access this, to keep the flags private.
  constexpr Event(bool initial, Flags flags)
      : magic_(kMagic), result_(initial ? ZX_OK : kNotSignalled), flags_(flags) {}

 private:
  zx_status_t WaitWorker(const Deadline& deadline, Interruptible interruptible, uint signal_mask);
  void SignalInternal(bool reschedule, zx_status_t wait_result) TA_REQ(thread_lock);

  static constexpr uint32_t kMagic = fbl::magic("evnt");
  uint32_t magic_;

  static constexpr zx_status_t kNotSignalled = INT_MAX;
  zx_status_t result_;

  Flags flags_;

  WaitQueue wait_;
};

class AutounsignalEvent : public Event {
 public:
  constexpr explicit AutounsignalEvent(bool initial = false)
      : Event(initial, Flags::AUTOUNSIGNAL) {}
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_EVENT_H_
