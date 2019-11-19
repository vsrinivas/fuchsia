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
#include <lib/counters.h>
#include <list.h>
#include <malloc.h>
#include <platform.h>
#include <trace.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/align.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/sched.h>
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

// Default platform ticks hook.  This hook will be replaced with the appropriate
// source of time for the platform, selected during platform initialization.
zx_ticks_t (*current_ticks)(void) = [](void) -> zx_ticks_t { return 0; };

namespace {

spin_lock_t timer_lock __CPU_ALIGN_EXCLUSIVE = SPIN_LOCK_INITIAL_VALUE;
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

void timer_init(timer_t* timer) { *timer = (timer_t)TIMER_INITIAL_VALUE(*timer); }

// Set the platform's oneshot timer to the minimum of its current deadline and |new_deadline|.
//
// Call this when the timer queue's head changes.
//
// Can only be called when interrupts are disabled and current CPU is |cpu|.
static void update_platform_timer(uint cpu, zx_time_t new_deadline) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(cpu == arch_curr_cpu_num());
  if (new_deadline < percpu::Get(cpu).next_timer_deadline) {
    LTRACEF("rescheduling timer for %" PRIi64 " nsecs\n", new_deadline);
    platform_set_oneshot_timer(new_deadline);
    percpu::Get(cpu).next_timer_deadline = new_deadline;
  }
}

static void insert_timer_in_queue(uint cpu, timer_t* timer, zx_time_t earliest_deadline,
                                  zx_time_t latest_deadline) {
  DEBUG_ASSERT(arch_ints_disabled());
  LTRACEF("timer %p, cpu %u, scheduled %" PRIi64 "\n", timer, cpu, timer->scheduled_time);

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
  //
  timer_t* entry;

  list_for_every_entry (&percpu::Get(cpu).timer_queue, entry, timer_t, node) {
    if (entry->scheduled_time > latest_deadline) {
      // New timer latest is earlier than the current timer.
      // Just add upfront as is, without slack.
      //
      //   ---------t---)--e-------------------------------> time
      //
      timer->slack = 0ll;
      list_add_before(&entry->node, &timer->node);
      return;
    }

    if (entry->scheduled_time >= timer->scheduled_time) {
      //  New timer slack overlaps and is to the left (or equal). We
      //  coalesce with current by scheduling late.
      //
      //  --------(----t---e-)----------------------------> time
      //
      timer->slack = zx_time_sub_time(entry->scheduled_time, timer->scheduled_time);
      timer->scheduled_time = entry->scheduled_time;
      kcounter_add(timer_coalesced_counter, 1);
      list_add_after(&entry->node, &timer->node);
      return;
    }

    if (entry->scheduled_time < earliest_deadline) {
      // new timer earliest is later than the current timer. This case
      // is handled in a future iteration.
      //
      //   ----------------e--(---t-----------------------> time
      //
      continue;
    }

    // New timer is to the right of current timer and there is overlap
    // with the current timer, but could the next timer (if any) be
    // a better fit?
    //
    //  -------------(--e---t-----?-------------------> time
    //

    const timer_t* next =
        list_next_type(&percpu::Get(cpu).timer_queue, &entry->node, timer_t, node);

    if (next != NULL) {
      if (next->scheduled_time <= timer->scheduled_time) {
        // The new timer is to the right of the next timer. There is no
        // chance the current timer is a better fit.
        //
        //  -------------(--e---n---t----------------------> time
        //
        continue;
      }

      if (next->scheduled_time < latest_deadline) {
        // There is slack overlap with the next timer, and also with the
        // current timer. Which coalescing is a better match?
        //
        //  --------------(-e---t---n-)-----------------------> time
        //
        zx_duration_t delta_entry = zx_time_sub_time(timer->scheduled_time, entry->scheduled_time);
        zx_duration_t delta_next = zx_time_sub_time(next->scheduled_time, timer->scheduled_time);
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
    //
    timer->slack = zx_time_sub_time(entry->scheduled_time, timer->scheduled_time);
    timer->scheduled_time = entry->scheduled_time;
    kcounter_add(timer_coalesced_counter, 1);
    list_add_after(&entry->node, &timer->node);
    return;
  }

  // Walked off the end of the list and there was no overlap.
  timer->slack = 0;
  list_add_tail(&percpu::Get(cpu).timer_queue, &timer->node);
}

void timer_set(timer_t* timer, const Deadline& deadline, timer_callback callback, void* arg) {
  LTRACEF("timer %p deadline.when %" PRIi64 " deadline.slack.amount %" PRIi64
          " deadline.slack.mode %u callback %p arg %p\n",
          timer, deadline.when(), deadline.slack().amount(), deadline.slack().mode(), callback,
          arg);

  DEBUG_ASSERT(timer->magic == TIMER_MAGIC);
  DEBUG_ASSERT(deadline.slack().mode() <= TIMER_SLACK_LATE);
  DEBUG_ASSERT(deadline.slack().amount() >= 0);

  if (list_in_list(&timer->node)) {
    panic("timer %p already in list\n", timer);
  }

  const zx_time_t latest_deadline = deadline.latest();
  const zx_time_t earliest_deadline = deadline.earliest();

  Guard<spin_lock_t, IrqSave> guard{TimerLock::Get()};

  uint cpu = arch_curr_cpu_num();

  bool currently_active = (timer->active_cpu == (int)cpu);
  if (unlikely(currently_active)) {
    // the timer is active on our own cpu, we must be inside the callback
    if (timer->cancel) {
      return;
    }
  } else if (unlikely(timer->active_cpu >= 0)) {
    panic("timer %p currently active on a different cpu %d\n", timer, timer->active_cpu);
  }

  // Set up the structure.
  timer->scheduled_time = deadline.when();
  timer->callback = callback;
  timer->arg = arg;
  timer->cancel = false;
  // We don't need to modify timer->active_cpu because it is managed by timer_tick().

  LTRACEF("scheduled time %" PRIi64 "\n", timer->scheduled_time);

  insert_timer_in_queue(cpu, timer, earliest_deadline, latest_deadline);
  kcounter_add(timer_created_counter, 1);

  if (list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node) == timer) {
    // we just modified the head of the timer queue
    update_platform_timer(cpu, deadline.when());
  }
}

void timer_preempt_reset(zx_time_t deadline) {
  DEBUG_ASSERT(arch_ints_disabled());

  uint cpu = arch_curr_cpu_num();

  LTRACEF("preempt timer cpu %u deadline %" PRIi64 "\n", cpu, deadline);

  percpu::Get(cpu).preempt_timer_deadline = deadline;

  update_platform_timer(cpu, deadline);
}

void timer_preempt_cancel() {
  DEBUG_ASSERT(arch_ints_disabled());

  uint cpu = arch_curr_cpu_num();

  percpu::Get(cpu).preempt_timer_deadline = ZX_TIME_INFINITE;

  // Note, we're not updating the platform timer. It's entirely possible the timer queue is empty
  // and the preemption timer is the only reason the platform timer is set. To know that, we'd
  // need to acquire a lock and look at the queue. Rather than pay that cost, leave the platform
  // timer as is and expect the recipient to handle spurious wakeups.
}

bool timer_cancel(timer_t* timer) {
  DEBUG_ASSERT(timer->magic == TIMER_MAGIC);

  Guard<spin_lock_t, IrqSave> guard{TimerLock::Get()};

  uint cpu = arch_curr_cpu_num();

  // mark the timer as canceled
  timer->cancel = true;
  mb();

  // see if we're trying to cancel the timer we're currently in the middle of handling
  if (unlikely(timer->active_cpu == (int)cpu)) {
    // zero it out
    timer->callback = NULL;
    timer->arg = NULL;

    // we're done, so return back to the callback
    return false;
  }

  bool callback_not_running;

  // if the timer is in a queue, remove it and adjust hardware timers if needed
  if (list_in_list(&timer->node)) {
    callback_not_running = true;

    // save a copy of the old head of the queue so later we can see if we modified the head
    timer_t* oldhead = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);

    // remove our timer from the queue
    list_delete(&timer->node);
    kcounter_add(timer_canceled_counter, 1);

    // TODO(cpu): if  after removing |timer| there is one other single timer with
    // the same scheduled_time and slack non-zero then it is possible to return
    // that timer to the ideal scheduled_time.

    // see if we've just modified the head of this cpu's timer queue.
    // if we modified another cpu's queue, we'll just let it fire and sort itself out
    if (unlikely(oldhead == timer)) {
      // timer we're canceling was at head of queue, see if we should update platform timer
      timer_t* newhead = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);
      if (newhead) {
        update_platform_timer(cpu, newhead->scheduled_time);
      } else if (percpu::Get(cpu).next_timer_deadline == ZX_TIME_INFINITE) {
        LTRACEF("clearing old hw timer, preempt timer not set, nothing in the queue\n");
        platform_stop_timer();
      }
    }
  } else {
    callback_not_running = false;
  }

  guard.Release();

  // wait for the timer to become un-busy in case a callback is currently active on another cpu
  while (timer->active_cpu >= 0) {
    arch_spinloop_pause();
  }

  // zero it out
  timer->callback = NULL;
  timer->arg = NULL;

  return callback_not_running;
}

// called at interrupt time to process any pending timers
void timer_tick(zx_time_t now) {
  timer_t* timer;

  DEBUG_ASSERT(arch_ints_disabled());

  CPU_STATS_INC(timer_ints);

  uint cpu = arch_curr_cpu_num();

  LTRACEF("cpu %u now %" PRIi64 ", sp %p\n", cpu, now, __GET_FRAME());

  // platform timer has fired, no deadline is set
  percpu::Get(cpu).next_timer_deadline = ZX_TIME_INFINITE;

  // service preempt timer before acquiring the timer lock
  if (now >= percpu::Get(cpu).preempt_timer_deadline) {
    percpu::Get(cpu).preempt_timer_deadline = ZX_TIME_INFINITE;
    sched_preempt_timer_tick(now);
  }

  Guard<spin_lock_t, NoIrqSave> guard{TimerLock::Get()};

  for (;;) {
    // see if there's an event to process
    timer = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);
    if (likely(timer == 0)) {
      break;
    }
    LTRACEF("next item on timer queue %p at %" PRIi64 " now %" PRIi64 " (%p, arg %p)\n", timer,
            timer->scheduled_time, now, timer->callback, timer->arg);
    if (likely(now < timer->scheduled_time)) {
      break;
    }

    // process it
    LTRACEF("timer %p\n", timer);
    DEBUG_ASSERT_MSG(timer && timer->magic == TIMER_MAGIC,
                     "ASSERT: timer failed magic check: timer %p, magic 0x%x\n", timer,
                     (uint)timer->magic);
    list_delete(&timer->node);

    // mark the timer busy
    timer->active_cpu = cpu;
    // Unlocking the spinlock in CallUnlocked acts as a memory barrier.

    // Now that the timer is off of the list, release the spinlock to handle
    // the callback, then re-acquire in case it is requeued.
    guard.CallUnlocked([timer, now]() {
      LTRACEF("dequeued timer %p, scheduled %" PRIi64 "\n", timer, timer->scheduled_time);

      CPU_STATS_INC(timers);
      kcounter_add(timer_fired_counter, 1);

      LTRACEF("timer %p firing callback %p, arg %p\n", timer, timer->callback, timer->arg);
      timer->callback(timer, now, timer->arg);

      DEBUG_ASSERT(arch_ints_disabled());
    });

    // mark it not busy
    timer->active_cpu = -1;
    mb();
  }

  // get the deadline of the event at the head of the queue (if any)
  zx_time_t deadline = ZX_TIME_INFINITE;
  timer = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);
  if (timer) {
    deadline = timer->scheduled_time;

    // has to be the case or it would have fired already
    DEBUG_ASSERT(deadline > now);
  }

  // we're done manipulating the timer queue
  guard.Release();

  // set the platform timer to the *soonest* of queue event and preempt timer
  if (percpu::Get(cpu).preempt_timer_deadline < deadline) {
    deadline = percpu::Get(cpu).preempt_timer_deadline;
  }
  update_platform_timer(cpu, deadline);
}

zx_status_t timer_trylock_or_cancel(timer_t* t, spin_lock_t* lock) {
  // spin trylocking on the passed in spinlock either waiting for it
  // to grab or the passed in timer to be canceled.
  while (unlikely(spin_trylock(lock))) {
    // we failed to grab it, check for cancel
    if (t->cancel) {
      // we were canceled, so bail immediately
      return ZX_ERR_TIMED_OUT;
    }
    // tell the arch to wait
    arch_spinloop_pause();
  }

  return ZX_OK;
}

void timer_transition_off_cpu(uint old_cpu) {
  Guard<spin_lock_t, IrqSave> guard{TimerLock::Get()};
  uint cpu = arch_curr_cpu_num();

  timer_t* old_head = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);

  timer_t *entry = NULL, *tmp_entry = NULL;
  // Move all timers from old_cpu to this cpu
  list_for_every_entry_safe (&percpu::Get(old_cpu).timer_queue, entry, tmp_entry, timer_t, node) {
    list_delete(&entry->node);
    // We lost the original asymmetric slack information so when we combine them
    // with the other timer queue they are not coalesced again.
    // TODO(cpu): figure how important this case is.
    insert_timer_in_queue(cpu, entry, entry->scheduled_time, entry->scheduled_time);
    // Note, we do not increment the "created" counter here because we are simply moving these
    // timers from one queue to another and we already counted them when they were first
    // created.
  }

  timer_t* new_head = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);
  if (new_head != NULL && new_head != old_head) {
    // we just modified the head of the timer queue
    update_platform_timer(cpu, new_head->scheduled_time);
  }

  // the old cpu has no tasks left, so reset the deadlines
  percpu::Get(old_cpu).preempt_timer_deadline = ZX_TIME_INFINITE;
  percpu::Get(old_cpu).next_timer_deadline = ZX_TIME_INFINITE;
}

void timer_thaw_percpu(void) {
  DEBUG_ASSERT(arch_ints_disabled());
  Guard<spin_lock_t, NoIrqSave> guard{TimerLock::Get()};

  uint cpu = arch_curr_cpu_num();

  // reset next_timer_deadline so that update_platform_timer will reconfigure the timer
  percpu::Get(cpu).next_timer_deadline = ZX_TIME_INFINITE;
  zx_time_t deadline = percpu::Get(cpu).preempt_timer_deadline;

  timer_t* t = list_peek_head_type(&percpu::Get(cpu).timer_queue, timer_t, node);
  if (t) {
    if (t->scheduled_time < deadline) {
      deadline = t->scheduled_time;
    }
  }

  guard.Release();

  update_platform_timer(cpu, deadline);
}

// print a timer queue dump into the passed in buffer
static void dump_timer_queues(char* buf, size_t len) {
  size_t ptr = 0;
  zx_time_t now = current_time();

  Guard<spin_lock_t, IrqSave> guard{TimerLock::Get()};
  for (uint i = 0; i < percpu::processor_count(); i++) {
    if (mp_is_cpu_online(i)) {
      ptr += snprintf(buf + ptr, len - ptr, "cpu %u:\n", i);

      timer_t* t;
      zx_time_t last = now;
      list_for_every_entry (&percpu::Get(i).timer_queue, t, timer_t, node) {
        zx_duration_t delta_now = zx_time_sub_time(t->scheduled_time, now);
        zx_duration_t delta_last = zx_time_sub_time(t->scheduled_time, last);
        ptr += snprintf(buf + ptr, len - ptr,
                        "\ttime %" PRIi64 " delta_now %" PRIi64 " delta_last %" PRIi64
                        " func %p arg %p\n",
                        t->scheduled_time, delta_now, delta_last, t->callback, t->arg);
        last = t->scheduled_time;
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

  dump_timer_queues(buf, timer_buffer_size);

  printf("%s", buf);

  free(buf);

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("timers", "dump the current kernel timer queues", &cmd_timers,
                      CMD_AVAIL_NORMAL)
STATIC_COMMAND_END(kernel)
