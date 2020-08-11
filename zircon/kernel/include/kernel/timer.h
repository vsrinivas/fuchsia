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
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/deadline.h>
#include <kernel/spinlock.h>

// Rules for Timers:
// - Timer callbacks occur from interrupt context.
// - Timers may be programmed or canceled from interrupt or thread context.
// - Timers may be canceled or reprogrammed from within their callback.
// - Setting and canceling timers is not thread safe and cannot be done concurrently.
// - Timer::cancel() may spin waiting for a pending timer to complete on another cpu.

// Timers may be removed from an arbitrary TimerQueue, so their list
// node requires the AllowRemoveFromContainer option.
class Timer : public fbl::DoublyLinkedListable<Timer*, fbl::NodeOptions::AllowRemoveFromContainer> {
 public:
  using Callback = void (*)(Timer*, zx_time_t now, void* arg);

  // Timers need a constexpr constructor, as it is valid to construct them in static storage.
  constexpr Timer() = default;

  // We ensure that timers are not on a list or an active cpu when destroyed.
  ~Timer();

  // Timers are not moved or copied.
  Timer(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer& operator=(Timer&&) = delete;

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
  //   void callback(Timer *, zx_time_t now, void *arg) { ... }
  void Set(const Deadline& deadline, Callback callback, void* arg);

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
  zx_status_t TrylockOrCancel(SpinLock* lock) TA_TRY_ACQ(false, lock);

  // Private accessors for timer tests.
  zx_duration_t slack_for_test() const { return slack_; }
  zx_time_t scheduled_time_for_test() const { return scheduled_time_; }

 private:
  // TimerQueues can directly manipulate the state of their enqueued Timers.
  friend class TimerQueue;

  static constexpr uint32_t kMagic = fbl::magic("timr");
  uint32_t magic_ = kMagic;

  zx_time_t scheduled_time_ = 0;
  // Stores the applied slack adjustment from the ideal scheduled_time.
  zx_duration_t slack_ = 0;
  Callback callback_ = nullptr;
  void* arg_ = nullptr;

  // INVALID_CPU, if inactive.
  volatile cpu_num_t active_cpu_ = INVALID_CPU;

  // true if cancel is pending
  volatile bool cancel_ = false;
};

// Preemption Timers
//
// Each CPU has a dedicated preemption timer that's managed using specialized
// functions (prefixed with timer_preempt_).
//
// Preemption timers are different from general timers. Preemption timers:
//
// - are reset frequently by the scheduler so performance is important
// - should not be migrated off their CPU when the CPU is shutdown
//
// Note: A preemption timer may fire even after it has been canceled.
class TimerQueue {
 public:
  // Set/reset the preemption timer.
  //
  // When the preemption timer fires, Scheduler::TimerTick is called.
  void PreemptReset(zx_time_t deadline);

  // Cancel the preemption timer.
  void PreemptCancel();

  // Returns true if the preemption deadline is set and will definitely fire in
  // the future. A false value does not definitively mean the preempt timer will
  // not fire, as a spurious expiration is allowed.
  bool PreemptArmed() const { return preempt_timer_deadline_ != ZX_TIME_INFINITE; }

  // Internal routines used when bringing cpus online/offline

  // Moves |source|'s timers (except its preemption timer) to this TimerQueue.
  void TransitionOffCpu(TimerQueue& source);

  // This function is to be invoked after resume on each CPU's TimerQueue that
  // may have had timers still on it, in order to restart hardware timers.
  void ThawPercpu();

  // Prints the contents of all timer queues into |buf| of length |len| and null
  // terminates |buf|.
  static void PrintTimerQueues(char* buf, size_t len);

  // This is called periodically by timer_tick(), which itself is invoked
  // periodically by some hardware timer.
  void Tick(zx_time_t now, cpu_num_t cpu);

 private:
  // Timers can directly call Insert and Cancel.
  friend class Timer;

  // Add |timer| to this TimerQueue, possibly coalescing deadlines as well.
  void Insert(Timer* timer, zx_time_t earliest_deadline, zx_time_t latest_deadline);

  // Set the platform's oneshot timer to the minimum of its current
  // deadline and |new_deadline|.
  //
  // This can only be called when interrupts are disabled.
  void UpdatePlatformTimer(zx_time_t new_deadline);

  // Timers on this queue.
  fbl::DoublyLinkedList<Timer*> timer_list_;

  // This TimerQueue's preemption deadline. ZX_TIME_INFINITE means not set.
  zx_time_t preempt_timer_deadline_ = ZX_TIME_INFINITE;

  // This TimerQueue's deadline for its platform timer or ZX_TIME_INFINITE if not set
  zx_time_t next_timer_deadline_ = ZX_TIME_INFINITE;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_TIMER_H_
