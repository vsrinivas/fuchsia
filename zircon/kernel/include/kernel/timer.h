// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2009 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_TIMER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_TIMER_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <kernel/deadline.h>
#include <kernel/spinlock.h>

// Rules for Timers:
// - Timer callbacks occur from interrupt context.
// - Timers may be programmed or canceled from interrupt or thread context.
// - Timers may be canceled or reprogrammed from within their callback.
// - Setting and canceling timers is not thread safe and cannot be done concurrently.
// - Timer::cancel() may spin waiting for a pending timer to complete on another cpu.

// For now, Timers are structs with standard layout. Eventually, the list_node inside will no longer
// force that requirement, and this can become a class with private members.
struct Timer {
  using Callback = void (*)(Timer*, zx_time_t now, void* arg);

  static constexpr uint32_t kMagic = fbl::magic("timr");
  uint32_t magic_ = kMagic;

  struct list_node node_ = LIST_INITIAL_CLEARED_VALUE;

  zx_time_t scheduled_time_ = 0;
  // Stores the applied slack adjustment from the ideal scheduled_time.
  zx_duration_t slack_ = 0;
  Callback callback_ = nullptr;
  void* arg_ = nullptr;

  // <0 if inactive
  volatile int active_cpu_ = -1;

  // true if cancel is pending
  volatile bool cancel_ = false;

  // Timers need a constexpr constructor, as it is valid to construct them in static storage.
  constexpr Timer() = default;

  // We ensure that timers are not on a list or an active cpu when destroyed.
  ~Timer();

  // Timers are not moved or copied.
  Timer(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer& operator=(Timer&&) = delete;

  //
  // Set up a timer that executes once
  //
  // This function specifies a callback function to be run after a specified
  // deadline passes. The function will be called one time.
  //
  // deadline: specifies when the timer should be executed
  // callback: the function to call when the timer expires
  // arg: the argument to pass to the callback
  //
  // The timer function is declared as:
  //   void callback(timer_t *, zx_time_t now, void *arg) { ... }
  void Set(const Deadline& deadline, Callback callback, void* arg);

  //
  // Cancel a pending timer
  //
  // Returns true if the timer was canceled before it was
  // scheduled in a cpu and false otherwise or if the timer
  // was not scheduled at all.
  //
  bool Cancel();

  // Equivalent to Set with no slack
  void SetOneshot(zx_time_t deadline, Callback callback, void* arg) {
    return Set(Deadline::no_slack(deadline), callback, arg);
  }

  // Special helper routine to simultaneously try to acquire a spinlock and check for
  // timer cancel, which is needed in a few special cases.
  // returns ZX_OK if spinlock was acquired, ZX_ERR_TIMED_OUT if timer was canceled.
  zx_status_t TrylockOrCancel(spin_lock_t* lock) TA_TRY_ACQ(false, lock);
};

struct TimerQueue {
  // Preemption Timers
  //
  // Each CPU has a dedicated preemption timer that's managed using specialized functions (prefixed
  // with timer_preempt_).
  //
  // Preemption timers are different from general timers. Preemption timers:
  //
  // - are reset frequently by the scheduler so performance is important
  // - should not be migrated off their CPU when the CPU is shutdown
  //
  // Note: A preemption timer may fire even after it has been canceled.
  //

  //
  // Set/reset the current CPU's preemption timer.
  //
  // When the preemption timer fires, sched_preempt_timer_tick is called.
  static void PreemptReset(zx_time_t deadline);

  //
  // Cancel the current CPU's preemption timer.
  static void PreemptCancel();

  // Internal routines used when bringing cpus online/offline

  // Moves |old_cpu|'s timers (except its preemption timer) to the current cpu
  static void TransitionOffCpu(uint old_cpu);

  // This function is to be invoked after resume on each CPU that may have
  // had timers still on it, in order to restart hardware timers.
  static void ThawPercpu();
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_TIMER_H_
