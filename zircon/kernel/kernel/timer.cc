// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Kernel timer subsystem
 * @defgroup timer Timers
 *
 * The timer subsystem allows functions to be scheduled for later
 * execution.  Each timer object is used to cause one function to
 * be executed at a later time.
 *
 * Timer callback functions are called in interrupt context.
 *
 * @{
 */
#include "kernel/timer.h"

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/affine/ratio.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <platform.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/align.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <platform/timer.h>

#define LOCAL_TRACE 0

// Total number of timers set. Always increasing.
KCOUNTER(timer_created_counter, "timer.created")

// Number of timers merged into an existing timer because of slack.
KCOUNTER(timer_coalesced_counter, "timer.coalesced")

// Number of timers that have fired (i.e. callback was invoked).
KCOUNTER(timer_fired_counter, "timer.fired")

// Number of timers that were successfully canceled. Attempts to cancel a timer that is currently
// firing are not counted.
KCOUNTER(timer_canceled_counter, "timer.canceled")

zx_ticks_t current_ticks(void) {
  return platform_current_ticks();
}

namespace {

SpinLock timer_lock __CPU_ALIGN_EXCLUSIVE;
DECLARE_SINGLETON_LOCK_WRAPPER(TimerLock, timer_lock);

affine::Ratio gTicksToTime;
uint64_t gTicksPerSecond;

}  // anonymous namespace

void platform_set_ticks_to_time_ratio(const affine::Ratio& ticks_to_time) {
  // ASSERT that we are not calling this function twice.  Once set, this ratio
  // may not change.
  DEBUG_ASSERT(gTicksPerSecond == 0);
  DEBUG_ASSERT(ticks_to_time.numerator() != 0);
  DEBUG_ASSERT(ticks_to_time.denominator() != 0);
  gTicksToTime = ticks_to_time;
  gTicksPerSecond = gTicksToTime.Inverse().Scale(ZX_SEC(1));
}

const affine::Ratio& platform_get_ticks_to_time_ratio(void) { return gTicksToTime; }

zx_time_t current_time(void) { return gTicksToTime.Scale(current_ticks()); }

zx_ticks_t ticks_per_second(void) { return gTicksPerSecond; }

void TimerQueue::UpdatePlatformTimer(zx_time_t new_deadline) {
  DEBUG_ASSERT(arch_ints_disabled());
  if (new_deadline < next_timer_deadline_) {
    LTRACEF("rescheduling timer for %" PRIi64 " nsecs\n", new_deadline);
    platform_set_oneshot_timer(new_deadline);
    next_timer_deadline_ = new_deadline;
  }
}

void TimerQueue::Insert(Timer* timer, zx_time_t earliest_deadline, zx_time_t latest_deadline) {
  DEBUG_ASSERT(arch_ints_disabled());
  cpu_num_t cpu = arch_curr_cpu_num();
  LTRACEF("timer %p, cpu %u, scheduled %" PRIi64 "\n", timer, cpu, timer->scheduled_time_);

  // For inserting the timer we consider several cases. In general we
  // want to coalesce with the current timer unless we can prove that
  // either that:
  //  1- there is no slack overlap with current timer OR
  //  2- the next timer is a better fit.
  //
  // In diagrams that follow
  // - Let |e| be the current (existing) timer deadline
  // - Let |t| be the deadline of the timer we are inserting
  // - Let |n| be the next timer deadline if any
  // - Let |x| be the end of the list (not a timer)
  // - Let |(| and |)| the earliest_deadline and latest_deadline.

  for (Timer& entry : timer_list_) {
    if (entry.scheduled_time_ > latest_deadline) {
      // New timer latest is earlier than the current timer.
      // Just add upfront as is, without slack.
      //
      //   ---------t---)--e-------------------------------> time
      timer->slack_ = 0ll;
      timer_list_.insert(entry, timer);
      return;
    }

    if (entry.scheduled_time_ >= timer->scheduled_time_) {
      //  New timer slack overlaps and is to the left (or equal). We
      //  coalesce with current by scheduling late.
      //
      //  --------(----t---e-)----------------------------> time
      timer->slack_ = zx_time_sub_time(entry.scheduled_time_, timer->scheduled_time_);
      timer->scheduled_time_ = entry.scheduled_time_;
      kcounter_add(timer_coalesced_counter, 1);
      timer_list_.insert_after(timer_list_.make_iterator(entry), timer);
      return;
    }

    if (entry.scheduled_time_ < earliest_deadline) {
      // new timer earliest is later than the current timer. This case
      // is handled in a future iteration.
      //
      //   ----------------e--(---t-----------------------> time
      continue;
    }

    // New timer is to the right of current timer and there is overlap
    // with the current timer, but could the next timer (if any) be
    // a better fit?
    //
    //  -------------(--e---t-----?-------------------> time

    auto iter = timer_list_.make_iterator(entry);
    ++iter;
    if (iter != timer_list_.end()) {
      const Timer& next = *iter;
      if (next.scheduled_time_ <= timer->scheduled_time_) {
        // The new timer is to the right of the next timer. There is no
        // chance the current timer is a better fit.
        //
        //  -------------(--e---n---t----------------------> time
        continue;
      }

      if (next.scheduled_time_ < latest_deadline) {
        // There is slack overlap with the next timer, and also with the
        // current timer. Which coalescing is a better match?
        //
        //  --------------(-e---t---n-)-----------------------> time
        zx_duration_t delta_entry = zx_time_sub_time(timer->scheduled_time_, entry.scheduled_time_);
        zx_duration_t delta_next = zx_time_sub_time(next.scheduled_time_, timer->scheduled_time_);
        if (delta_next < delta_entry) {
          // New timer is closer to the next timer, handle it in the
          // next iteration.
          continue;
        }
      }
    }

    // Handles the remaining cases, note that there is overlap with
    // the current timer.
    //
    //  1- this is the last timer (next == NULL) or
    //  2- there is no overlap with the next timer, or
    //  3- there is overlap with both current and next but
    //     current is closer.
    //
    //  So we coalesce by scheduling early.
    timer->slack_ = zx_time_sub_time(entry.scheduled_time_, timer->scheduled_time_);
    timer->scheduled_time_ = entry.scheduled_time_;
    kcounter_add(timer_coalesced_counter, 1);
    timer_list_.insert_after(timer_list_.make_iterator(entry), timer);
    return;
  }

  // Walked off the end of the list and there was no overlap.
  timer->slack_ = 0;
  timer_list_.push_back(timer);
}

Timer::~Timer() {
  // Ensure that we are not on any TimerQueue's list.
  ZX_DEBUG_ASSERT(!InContainer());
  // Ensure that we are not active on some cpu.
  ZX_DEBUG_ASSERT(active_cpu_ == INVALID_CPU);
}

void Timer::Set(const Deadline& deadline, Callback callback, void* arg) {
  LTRACEF("timer %p deadline.when %" PRIi64 " deadline.slack.amount %" PRIi64
          " deadline.slack.mode %u callback %p arg %p\n",
          this, deadline.when(), deadline.slack().amount(), deadline.slack().mode(), callback, arg);

  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(deadline.slack().mode() <= TIMER_SLACK_LATE);
  DEBUG_ASSERT(deadline.slack().amount() >= 0);

  if (InContainer()) {
    panic("timer %p already in list\n", this);
  }

  const zx_time_t latest_deadline = deadline.latest();
  const zx_time_t earliest_deadline = deadline.earliest();

  Guard<SpinLock, IrqSave> guard{TimerLock::Get()};

  cpu_num_t cpu = arch_curr_cpu_num();

  bool currently_active = (active_cpu_ == cpu);
  if (unlikely(currently_active)) {
    // The timer is active on our own cpu, we must be inside the callback.
    if (cancel_) {
      return;
    }
  } else if (unlikely(active_cpu_ != INVALID_CPU)) {
    panic("timer %p currently active on a different cpu %u\n", this, active_cpu_);
  }

  // Set up the structure.
  scheduled_time_ = deadline.when();
  callback_ = callback;
  arg_ = arg;
  cancel_ = false;
  // We don't need to modify active_cpu_ because it is managed by timer_tick().

  LTRACEF("scheduled time %" PRIi64 "\n", scheduled_time_);

  TimerQueue& timer_queue = percpu::Get(cpu).timer_queue;

  timer_queue.Insert(this, earliest_deadline, latest_deadline);
  kcounter_add(timer_created_counter, 1);

  if (!timer_queue.timer_list_.is_empty() && &timer_queue.timer_list_.front() == this) {
    // We just modified the head of the timer queue.
    timer_queue.UpdatePlatformTimer(deadline.when());
  }
}

void TimerQueue::PreemptReset(zx_time_t deadline) {
  DEBUG_ASSERT(arch_ints_disabled());
  LTRACEF("preempt timer cpu %u deadline %" PRIi64 "\n", arch_curr_cpu_num(), deadline);
  preempt_timer_deadline_ = deadline;
  UpdatePlatformTimer(deadline);
}

bool Timer::Cancel() {
  DEBUG_ASSERT(magic_ == kMagic);

  Guard<SpinLock, IrqSave> guard{TimerLock::Get()};

  cpu_num_t cpu = arch_curr_cpu_num();

  // mark the timer as canceled
  cancel_ = true;
  arch::DeviceMemoryBarrier();

  // see if we're trying to cancel the timer we're currently in the middle of handling
  if (unlikely(active_cpu_ == cpu)) {
    // zero it out
    callback_ = nullptr;
    arg_ = nullptr;

    // we're done, so return back to the callback
    return false;
  }

  bool callback_not_running;

  // If this Timer is in a queue, remove it and adjust hardware timers if needed.
  if (InContainer()) {
    callback_not_running = true;

    TimerQueue& timer_queue = percpu::Get(cpu).timer_queue;

    // Save a copy of the old head of the queue so later we can see if we modified the head.
    const Timer* oldhead = nullptr;
    if (!timer_queue.timer_list_.is_empty()) {
      oldhead = &timer_queue.timer_list_.front();
    }

    // Remove this Timer from this whatever TimerQueue it's on.
    RemoveFromContainer();
    kcounter_add(timer_canceled_counter, 1);

    // TODO(cpu): If, after removing |timer| there is one other single Timer with
    // the same scheduled_time_ and slack_ non-zero, then it is possible to return
    // that timer to the ideal scheduled_time_.

    // See if we've just modified the head of this TimerQueue.
    //
    // If Timer was on another cpu's queue, we'll just let it fire and sort itself out.
    if (unlikely(oldhead == this)) {
      // The Timer we're canceling was at head of this queue, so see if we should update platform
      // timer.
      if (!timer_queue.timer_list_.is_empty()) {
        timer_queue.UpdatePlatformTimer(timer_queue.timer_list_.front().scheduled_time_);
      } else if (timer_queue.next_timer_deadline_ == ZX_TIME_INFINITE) {
        LTRACEF("clearing old hw timer, preempt timer not set, nothing in the queue\n");
        platform_stop_timer();
      }
    }
  } else {
    callback_not_running = false;
  }

  guard.Release();

  // wait for the timer to become un-busy in case a callback is currently active on another cpu
  while (active_cpu_ != INVALID_CPU) {
    arch::Yield();
  }

  // zero it out
  callback_ = nullptr;
  arg_ = nullptr;

  return callback_not_running;
}

// called at interrupt time to process any pending timers
void timer_tick(zx_time_t now) {
  DEBUG_ASSERT(arch_ints_disabled());

  CPU_STATS_INC(timer_ints);

  cpu_num_t cpu = arch_curr_cpu_num();

  LTRACEF("cpu %u now %" PRIi64 ", sp %p\n", cpu, now, __GET_FRAME());

  percpu::Get(cpu).timer_queue.Tick(now, cpu);
}

void TimerQueue::Tick(zx_time_t now, cpu_num_t cpu) {
  // The platform timer has fired, so no deadline is set.
  next_timer_deadline_ = ZX_TIME_INFINITE;

  // Service the preemption timer before acquiring the timer lock.
  if (now >= preempt_timer_deadline_) {
    preempt_timer_deadline_ = ZX_TIME_INFINITE;
    Scheduler::TimerTick(SchedTime{now});
  }

  Guard<SpinLock, NoIrqSave> guard{TimerLock::Get()};

  for (;;) {
    // See if there's an event to process.
    if (timer_list_.is_empty()) {
      break;
    }

    Timer& timer = timer_list_.front();

    LTRACEF("next item on timer queue %p at %" PRIi64 " now %" PRIi64 " (%p, arg %p)\n", &timer,
            timer.scheduled_time_, now, timer.callback_, timer.arg_);
    if (likely(now < timer.scheduled_time_)) {
      break;
    }

    // Process it.
    LTRACEF("timer %p\n", &timer);
    DEBUG_ASSERT_MSG(timer.magic_ == Timer::kMagic,
                     "ASSERT: timer failed magic check: timer %p, magic 0x%x\n", &timer,
                     (uint)timer.magic_);
    timer_list_.erase(timer);

    // Mark the timer busy.
    timer.active_cpu_ = cpu;
    // Unlocking the spinlock in CallUnlocked acts as a memory barrier.

    // Now that the timer is off of the list, release the spinlock to handle
    // the callback, then re-acquire in case it is requeued.
    guard.CallUnlocked([&timer, now]() {
      LTRACEF("dequeued timer %p, scheduled %" PRIi64 "\n", &timer, timer.scheduled_time_);

      CPU_STATS_INC(timers);
      kcounter_add(timer_fired_counter, 1);

      LTRACEF("timer %p firing callback %p, arg %p\n", &timer, timer.callback_, timer.arg_);
      timer.callback_(&timer, now, timer.arg_);

      DEBUG_ASSERT(arch_ints_disabled());
    });

    // Mark it not busy.
    timer.active_cpu_ = INVALID_CPU;
    arch::DeviceMemoryBarrier();
  }

  // Get the deadline of the event at the head of the queue (if any).
  zx_time_t deadline = ZX_TIME_INFINITE;
  if (!timer_list_.is_empty()) {
    deadline = timer_list_.front().scheduled_time_;
    // This has to be the case or it would have fired already.
    DEBUG_ASSERT(deadline > now);
  }

  // We're done manipulating the timer queue.
  guard.Release();

  // Set the platform timer to the *soonest* of queue event and preemption timer.
  if (preempt_timer_deadline_ < deadline) {
    deadline = preempt_timer_deadline_;
  }
  UpdatePlatformTimer(deadline);
}

zx_status_t Timer::TrylockOrCancel(SpinLock* lock) {
  // spin trylocking on the passed in spinlock either waiting for it
  // to grab or the passed in timer to be canceled.
  while (unlikely(lock->TryAcquire())) {
    // we failed to grab it, check for cancel
    if (cancel_) {
      // we were canceled, so bail immediately
      return ZX_ERR_TIMED_OUT;
    }
    // tell the arch to wait
    arch::Yield();
  }

  return ZX_OK;
}

void TimerQueue::TransitionOffCpu(TimerQueue& source) {
  Guard<SpinLock, IrqSave> guard{TimerLock::Get()};

  Timer* old_head = nullptr;
  if (!timer_list_.is_empty()) {
    old_head = &timer_list_.front();
  }

  // Move all timers from |source| to this TimerQueue.
  Timer* timer;
  while ((timer = source.timer_list_.pop_front()) != nullptr) {
    // We lost the original asymmetric slack information so when we combine them
    // with the other timer queue they are not coalesced again.
    // TODO(cpu): figure how important this case is.
    Insert(timer, timer->scheduled_time_, timer->scheduled_time_);
    // Note, we do not increment the "created" counter here because we are simply moving these
    // timers from one queue to another and we already counted them when they were first
    // created.
  }

  Timer* new_head = nullptr;
  if (!timer_list_.is_empty()) {
    new_head = &timer_list_.front();
  }

  if (new_head != nullptr && new_head != old_head) {
    // We just modified the head of the timer queue.
    UpdatePlatformTimer(new_head->scheduled_time_);
  }

  // The old TimerQueue has no tasks left, so reset the deadlines.
  source.preempt_timer_deadline_ = ZX_TIME_INFINITE;
  source.next_timer_deadline_ = ZX_TIME_INFINITE;
}

void TimerQueue::ThawPercpu(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  Guard<SpinLock, NoIrqSave> guard{TimerLock::Get()};

  // Reset next_timer_deadline_ so that UpdatePlatformTimer will reconfigure the timer.
  next_timer_deadline_ = ZX_TIME_INFINITE;
  zx_time_t deadline = preempt_timer_deadline_;

  if (!timer_list_.is_empty()) {
    Timer& t = timer_list_.front();
    if (t.scheduled_time_ < deadline) {
      deadline = t.scheduled_time_;
    }
  }

  guard.Release();

  UpdatePlatformTimer(deadline);
}

void TimerQueue::PrintTimerQueues(char* buf, size_t len) {
  size_t ptr = 0;
  zx_time_t now = current_time();

  Guard<SpinLock, IrqSave> guard{TimerLock::Get()};
  for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
    if (mp_is_cpu_online(i)) {
      ptr += snprintf(buf + ptr, len - ptr, "cpu %u:\n", i);
      if (ptr >= len) {
        return;
      }
      zx_time_t last = now;
      for (Timer& t : percpu::Get(i).timer_queue.timer_list_) {
        zx_duration_t delta_now = zx_time_sub_time(t.scheduled_time_, now);
        zx_duration_t delta_last = zx_time_sub_time(t.scheduled_time_, last);
        ptr += snprintf(buf + ptr, len - ptr,
                        "\ttime %" PRIi64 " delta_now %" PRIi64 " delta_last %" PRIi64
                        " func %p arg %p\n",
                        t.scheduled_time_, delta_now, delta_last, t.callback_, t.arg_);
        if (ptr >= len) {
          return;
        }
        last = t.scheduled_time_;
      }
    }
  }
}

#include <lib/console.h>

static int cmd_timers(int argc, const cmd_args* argv, uint32_t flags) {
  const size_t timer_buffer_size = PAGE_SIZE;

  // allocate a buffer to dump the timer queue into to avoid reentrancy issues with the
  // timer spinlock
  char* buf = static_cast<char*>(malloc(timer_buffer_size));
  if (!buf) {
    return ZX_ERR_NO_MEMORY;
  }

  TimerQueue::PrintTimerQueues(buf, timer_buffer_size);

  printf("%s", buf);

  free(buf);

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("timers", "dump the current kernel timer queues", &cmd_timers,
                      CMD_AVAIL_NORMAL)
STATIC_COMMAND_END(kernel)
