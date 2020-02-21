// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/ktrace.h>
#include <list.h>
#include <platform.h>
#include <printf.h>
#include <string.h>
#include <target.h>
#include <trace.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <vm/vm.h>

#include "kernel/sched.h"

// disable priority boosting
#define NO_BOOST 0

#define MAX_PRIORITY_ADJ 4  // +/- priority levels from the base priority

// ktraces just local to this file
#define LOCAL_KTRACE_ENABLE 0

#define LOCAL_KTRACE(string, args...)                                                         \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_STRING_REF(string), \
               ##args)

#define LOCAL_KTRACE_DURATION \
  TraceDuration<TraceEnabled<LOCAL_KTRACE_ENABLE>, KTRACE_GRP_SCHEDULER, TraceContext::Cpu>

#define LOCAL_TRACE 0

#define DEBUG_THREAD_CONTEXT_SWITCH 0

#define TRACE_CONTEXT_SWITCH(str, x...) \
  do {                                  \
    if (DEBUG_THREAD_CONTEXT_SWITCH)    \
      printf("CS " str, ##x);           \
  } while (0)

// threads get 10ms to run before they use up their time slice and the scheduler is invoked
#define THREAD_INITIAL_TIME_SLICE ZX_MSEC(10)

KCOUNTER(boost_promotions, "kernel.thread.boost.promotions")
KCOUNTER(boost_demotions, "kernel.thread.boost.demotions")
KCOUNTER(boost_wq_recalcs, "kernel.thread.boost.wait_queue_recalcs")

// counters to track system latency
KCOUNTER(latency_counter, "thread.latency_accum")
KCOUNTER(samples_counter, "thread.samples_accum")

static void update_counters(zx_duration_t queue_time_ns) {
  latency_counter.Add(queue_time_ns);
  samples_counter.Add(1);
}

static bool local_migrate_if_needed(Thread* curr_thread);

// compute the effective priority of a thread
static void compute_effec_priority_(Thread* t) {
  int ep = t->base_priority_ + t->priority_boost_;
  if (t->inherited_priority_ > ep) {
    ep = t->inherited_priority_;
  }

  DEBUG_ASSERT(ep >= LOWEST_PRIORITY && ep <= HIGHEST_PRIORITY);

  t->effec_priority_ = ep;
}

static inline void post_boost_bookkeeping(Thread* t) TA_REQ(thread_lock) {
  DEBUG_ASSERT(!NO_BOOST);

  int old_ep = t->effec_priority_;

  compute_effec_priority_(t);

  if (old_ep != t->effec_priority_) {
    if (old_ep < t->effec_priority_) {
      boost_promotions.Add(1);
    } else {
      boost_demotions.Add(1);
    }

    if (t->blocking_wait_queue_ != nullptr) {
      boost_wq_recalcs.Add(1);
      wait_queue_priority_changed(t, old_ep, PropagatePI::Yes);
    }
  }
}

// boost the priority of the thread by +1
static void boost_thread(Thread* t) TA_REQ(thread_lock) {
  if (NO_BOOST) {
    return;
  }

  if (unlikely(t->CannotBoost())) {
    return;
  }

  if (t->priority_boost_ < MAX_PRIORITY_ADJ &&
      likely((t->base_priority_ + t->priority_boost_) < HIGHEST_PRIORITY)) {
    t->priority_boost_++;
    post_boost_bookkeeping(t);
  }
}

// deboost the priority of the thread by -1.
// If deboosting because the thread is using up all of its time slice,
// then allow the boost to go negative, otherwise only deboost to 0.
static void deboost_thread(Thread* t, bool quantum_expiration) TA_REQ(thread_lock) {
  if (NO_BOOST) {
    return;
  }

  if (unlikely(t->CannotBoost())) {
    return;
  }

  int boost_floor;
  if (quantum_expiration) {
    // deboost into negative boost
    boost_floor = -MAX_PRIORITY_ADJ;

    // make sure we dont deboost a thread too far
    if (unlikely(t->base_priority_ + boost_floor < LOWEST_PRIORITY)) {
      boost_floor = t->base_priority_ - LOWEST_PRIORITY;
    }

  } else {
    // otherwise only deboost to 0
    boost_floor = 0;
  }

  // if we're already bottomed out or below bottomed out, leave it alone
  if (t->priority_boost_ <= boost_floor) {
    return;
  }

  // drop a level
  t->priority_boost_--;
  post_boost_bookkeeping(t);
}

// pick a 'random' cpu out of the passed in mask of cpus
static cpu_mask_t rand_cpu(cpu_mask_t mask) {
  if (unlikely(mask == 0)) {
    return 0;
  }

  // check that the mask passed in has at least one bit set in the active mask
  cpu_mask_t active = mp_get_active_mask();
  mask &= active;
  if (unlikely(mask == 0)) {
    return 0;
  }

  // compute the highest cpu in the mask
  cpu_num_t highest_cpu = highest_cpu_set(mask);

  // not very random, round robins a bit through the mask until it gets a hit
  for (;;) {
    // protected by THREAD_LOCK, safe to use non atomically
    static uint rot = 0;

    if (++rot > highest_cpu) {
      rot = 0;
    }

    if ((1u << rot) & mask) {
      return (1u << rot);
    }
  }
}

// return the mask of CPUs this thread may be scheduled on
static cpu_mask_t get_allowed_cpus_mask(cpu_mask_t active_mask, const Thread* thread) {
  // the thread may run on any active CPU allowed by both its hard and
  // soft CPU affinity
  const cpu_mask_t soft_affinity = thread->soft_affinity_;
  const cpu_mask_t hard_affinity = thread->hard_affinity_;
  const cpu_mask_t available_mask = active_mask & soft_affinity & hard_affinity;
  if (likely(available_mask != 0)) {
    return available_mask;
  }

  // there is no CPU allowed by the intersection of active CPUs, the
  // hard affinity mask, and the soft affinity mask. ignore the soft
  // affinity.
  return active_mask & hard_affinity;
}

// find a cpu to wake up
static cpu_mask_t find_cpu_mask(Thread* t) TA_REQ(thread_lock) {
  // get the last cpu the thread ran on
  cpu_mask_t last_ran_cpu_mask = cpu_num_to_mask(t->last_cpu_);

  // the current cpu
  cpu_mask_t curr_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());

  // determine CPUs the thread can be scheduled on
  //
  // threads may be created and resumed before the thread init level. work around
  // an empty active mask by assuming the current cpu is scheduleable.
  const cpu_mask_t active_cpu_mask = mp_get_active_mask();
  cpu_mask_t allowed_cpus_mask =
      active_cpu_mask == 0 ? curr_cpu_mask : get_allowed_cpus_mask(active_cpu_mask, t);
  DEBUG_ASSERT_MSG(allowed_cpus_mask != 0,
                   "Thread not able to be scheduled on any CPU: active_mask: %#x, "
                   "kernel affinity: %#x, userspace affinity: %#x",
                   active_cpu_mask, t->hard_affinity_, t->soft_affinity_);

  LTRACEF_LEVEL(2, "last %#x curr %#x kernel affinity %#x userspace affinity %#x name %s\n",
                last_ran_cpu_mask, curr_cpu_mask, t->hard_affinity_, t->soft_affinity_, t->name_);

  // get a list of idle cpus and mask off the ones that aren't in our affinity mask
  cpu_mask_t candidate_cpu_mask = mp_get_idle_mask();
  candidate_cpu_mask &= allowed_cpus_mask;
  if (candidate_cpu_mask != 0) {
    if (candidate_cpu_mask & curr_cpu_mask) {
      // the current cpu is idle and within our affinity mask, so run it here
      return curr_cpu_mask;
    }

    if (last_ran_cpu_mask & candidate_cpu_mask) {
      // the last core it ran on is idle, active, and isn't the current cpu
      return last_ran_cpu_mask;
    }

    // pick an idle_cpu
    return rand_cpu(candidate_cpu_mask);
  }

  // no idle cpus in our affinity mask

  // if the last cpu it ran on is in the affinity mask and not the current cpu, pick that
  if ((last_ran_cpu_mask & allowed_cpus_mask) && last_ran_cpu_mask != curr_cpu_mask) {
    return last_ran_cpu_mask;
  }

  // fall back to picking a cpu out of the affinity mask, preferring something other
  // than the local cpu.
  // the affinity mask hard pins the thread to the cpus in the mask, so it's not possible
  // to pick a cpu outside of that list.
  cpu_mask_t mask = allowed_cpus_mask & ~(curr_cpu_mask);
  if (mask == 0) {
    // the code above verified that at least 1 CPU must be schedulable: if it
    // is not any other CPU, it must be the local CPU.
    return curr_cpu_mask;
  }
  return rand_cpu(mask);
}

// run queue manipulation
static void insert_in_run_queue_head(cpu_num_t cpu, Thread* t) TA_REQ(thread_lock) {
  DEBUG_ASSERT(!list_in_list(&t->queue_node_));

  list_add_head(&percpu::Get(cpu).run_queue[t->effec_priority_], &t->queue_node_);
  percpu::Get(cpu).run_queue_bitmap |= (1u << t->effec_priority_);

  // mark the cpu as busy since the run queue now has at least one item in it
  mp_set_cpu_busy(cpu);
}

static void insert_in_run_queue_tail(cpu_num_t cpu, Thread* t) TA_REQ(thread_lock) {
  DEBUG_ASSERT(!list_in_list(&t->queue_node_));

  list_add_tail(&percpu::Get(cpu).run_queue[t->effec_priority_], &t->queue_node_);
  percpu::Get(cpu).run_queue_bitmap |= (1u << t->effec_priority_);

  // mark the cpu as busy since the run queue now has at least one item in it
  mp_set_cpu_busy(cpu);
}

// remove the thread from the run queue it's in
static void remove_from_run_queue(Thread* t, int prio_queue) TA_REQ(thread_lock) {
  DEBUG_ASSERT(t->state_ == THREAD_READY);
  DEBUG_ASSERT(is_valid_cpu_num(t->curr_cpu_));

  list_delete(&t->queue_node_);

  // clear the old cpu's queue bitmap if that was the last entry
  struct percpu& c = percpu::Get(t->curr_cpu_);
  if (list_is_empty(&c.run_queue[prio_queue])) {
    c.run_queue_bitmap &= ~(1u << prio_queue);
  }
}

// using the per cpu run queue bitmap, find the highest populated queue
static uint highest_run_queue(const struct percpu* c) TA_REQ(thread_lock) {
  return HIGHEST_PRIORITY - __builtin_clz(c->run_queue_bitmap) -
         (sizeof(c->run_queue_bitmap) * CHAR_BIT - NUM_PRIORITIES);
}

static Thread* sched_get_top_thread(cpu_num_t cpu) TA_REQ(thread_lock) {
  // pop the head of the highest priority queue with any threads
  // queued up on the passed in cpu.

  struct percpu* c = &percpu::Get(cpu);
  if (likely(c->run_queue_bitmap)) {
    uint highest_queue = highest_run_queue(c);

    Thread* newthread = list_remove_head_type(&c->run_queue[highest_queue], Thread, queue_node_);

    DEBUG_ASSERT(newthread);
    DEBUG_ASSERT_MSG(newthread->hard_affinity_ & cpu_num_to_mask(cpu),
                     "thread %p name %s, aff %#x cpu %u\n", newthread, newthread->name_,
                     newthread->hard_affinity_, cpu);
    DEBUG_ASSERT(newthread->curr_cpu_ == cpu);

    if (list_is_empty(&c->run_queue[highest_queue])) {
      c->run_queue_bitmap &= ~(1u << highest_queue);
    }

    LOCAL_KTRACE("sched_get_top", static_cast<uint32_t>(newthread->priority_boost_),
                 static_cast<uint32_t>(newthread->base_priority_));

    return newthread;
  }

  // no threads to run, select the idle thread for this cpu
  return &c->idle_thread;
}

void sched_init_thread(Thread* t, int priority) {
  t->base_priority_ = priority;
  t->priority_boost_ = 0;
  t->inherited_priority_ = -1;
  compute_effec_priority_(t);
}

void sched_block() {
  LOCAL_KTRACE_DURATION trace{"sched_block"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  __UNUSED Thread* current_thread = Thread::Current::Get();

  DEBUG_ASSERT(current_thread->magic_ == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state_ != THREAD_RUNNING);

  // we are blocking on something. the blocking code should have already stuck us on a queue
  sched_resched_internal();
}

// find a cpu to run the thread on, put it in the run queue for that cpu, and accumulate a list
// of cpus we'll need to reschedule, including the local cpu.
static void find_cpu_and_insert(Thread* t, bool* local_resched, cpu_mask_t* accum_cpu_mask)
    TA_REQ(thread_lock) {
  // find a core to run it on
  cpu_mask_t cpu = find_cpu_mask(t);
  cpu_num_t cpu_num;

  DEBUG_ASSERT(cpu != 0);

  cpu_num = lowest_cpu_set(cpu);
  if (cpu_num == arch_curr_cpu_num()) {
    *local_resched = true;
  } else {
    *accum_cpu_mask |= cpu_num_to_mask(cpu_num);
  }

  // reuse this member to track the enqueue time for latency tracking
  t->last_started_running_ = current_time();
  t->curr_cpu_ = cpu_num;
  if (t->remaining_time_slice_ > 0) {
    insert_in_run_queue_head(cpu_num, t);
  } else {
    insert_in_run_queue_tail(cpu_num, t);
  }
}

bool sched_unblock(Thread* t) {
  LOCAL_KTRACE_DURATION trace{"sched_unblock"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);

  // thread is being woken up, boost its priority
  boost_thread(t);

  // stuff the new thread in the run queue
  t->state_ = THREAD_READY;

  bool local_resched = false;
  cpu_mask_t mask = 0;
  find_cpu_and_insert(t, &local_resched, &mask);

  if (mask) {
    mp_reschedule(mask, 0);
  }
  return local_resched;
}

bool sched_unblock_list(struct list_node* list) {
  LOCAL_KTRACE_DURATION trace{"sched_unblock_list"_stringref};

  DEBUG_ASSERT(list);
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  // pop the list of threads and shove into the scheduler
  bool local_resched = false;
  cpu_mask_t accum_cpu_mask = 0;
  Thread* t;
  while ((t = list_remove_tail_type(list, Thread, queue_node_))) {
    DEBUG_ASSERT(t->magic_ == THREAD_MAGIC);
    DEBUG_ASSERT(!t->IsIdle());

    // thread is being woken up, boost its priority
    boost_thread(t);

    // stuff the new thread in the run queue
    t->state_ = THREAD_READY;
    find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
  }

  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
  }

  return local_resched;
}

// handle the special case of resuming a newly created idle thread
void sched_unblock_idle(Thread* t) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  DEBUG_ASSERT(t->IsIdle());
  DEBUG_ASSERT(t->hard_affinity_ && (t->hard_affinity_ & (t->hard_affinity_ - 1)) == 0);

  // idle thread is special case, just jam it into the cpu's run queue in the thread's
  // affinity mask and mark it ready.
  t->state_ = THREAD_READY;
  cpu_num_t cpu = lowest_cpu_set(t->hard_affinity_);
  t->curr_cpu_ = cpu;
  insert_in_run_queue_head(cpu, t);
}

// the thread is voluntarily giving up its time slice
void sched_yield() {
  LOCAL_KTRACE_DURATION trace{"sched_yield"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  Thread* current_thread = Thread::Current::Get();
  DEBUG_ASSERT(!current_thread->IsIdle());

  // consume the rest of the time slice, deboost ourself, and go to the end of a queue
  current_thread->remaining_time_slice_ = 0;
  deboost_thread(current_thread, true);

  current_thread->state_ = THREAD_READY;

  if (local_migrate_if_needed(current_thread)) {
    return;
  }

  insert_in_run_queue_tail(arch_curr_cpu_num(), current_thread);
  sched_resched_internal();
}

// the current thread is being preempted from interrupt context
void sched_preempt() {
  LOCAL_KTRACE_DURATION trace{"sched_preempt"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  Thread* current_thread = Thread::Current::Get();
  uint curr_cpu = arch_curr_cpu_num();

  DEBUG_ASSERT(current_thread->curr_cpu_ == curr_cpu);
  DEBUG_ASSERT(current_thread->last_cpu_ == current_thread->curr_cpu_);

  current_thread->state_ = THREAD_READY;

  // idle thread doesn't go in the run queue
  if (likely(!current_thread->IsIdle())) {
    if (current_thread->remaining_time_slice_ <= 0) {
      // if we're out of quantum, deboost the thread and put it at the tail of a queue
      deboost_thread(current_thread, true);
    }

    if (local_migrate_if_needed(current_thread)) {
      return;
    }

    if (current_thread->remaining_time_slice_ > 0) {
      insert_in_run_queue_head(curr_cpu, current_thread);
    } else {
      insert_in_run_queue_tail(curr_cpu, current_thread);
    }
  }

  sched_resched_internal();
}

// the current thread is voluntarily reevaluating the scheduler on the current cpu
void sched_reschedule() {
  LOCAL_KTRACE_DURATION trace{"sched_reschedule"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  Thread* current_thread = Thread::Current::Get();
  uint curr_cpu = arch_curr_cpu_num();

  if (current_thread->disable_counts_ != 0) {
    current_thread->preempt_pending_ = true;
    return;
  }

  DEBUG_ASSERT(current_thread->curr_cpu_ == curr_cpu);
  DEBUG_ASSERT(current_thread->last_cpu_ == current_thread->curr_cpu_);

  current_thread->state_ = THREAD_READY;

  // idle thread doesn't go in the run queue
  if (likely(!current_thread->IsIdle())) {
    // deboost the current thread
    deboost_thread(current_thread, false);

    if (local_migrate_if_needed(current_thread)) {
      return;
    }

    if (current_thread->remaining_time_slice_ > 0) {
      insert_in_run_queue_head(curr_cpu, current_thread);
    } else {
      insert_in_run_queue_tail(curr_cpu, current_thread);
    }
  }

  sched_resched_internal();
}

// migrate the current thread to a new cpu and locally reschedule to seal the deal
static void migrate_current_thread(Thread* current_thread) TA_REQ(thread_lock) {
  bool local_resched = false;
  cpu_mask_t accum_cpu_mask = 0;

  // current thread, so just shove ourself into another cpu's queue and reschedule locally
  current_thread->state_ = THREAD_READY;
  find_cpu_and_insert(current_thread, &local_resched, &accum_cpu_mask);
  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
  }
  sched_resched_internal();
}

// migrate all non-pinned threads assigned to |old_cpu| to other queues
//
// must be called on |old_cpu|
void sched_transition_off_cpu(cpu_num_t old_cpu) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  DEBUG_ASSERT(old_cpu == arch_curr_cpu_num());

  // Ensure we do not get scheduled on anymore.
  mp_set_curr_cpu_active(false);

  Thread* t;
  bool local_resched = false;
  cpu_mask_t accum_cpu_mask = 0;
  cpu_mask_t pinned_mask = cpu_num_to_mask(old_cpu);
  list_node_t pinned_threads = LIST_INITIAL_VALUE(pinned_threads);
  while (!(t = sched_get_top_thread(old_cpu))->IsIdle()) {
    // Threads pinned to old_cpu can't run anywhere else, so put them
    // into a temporary list and deal with them later.
    if (t->hard_affinity_ != pinned_mask) {
      find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
      DEBUG_ASSERT(!local_resched);
    } else {
      DEBUG_ASSERT(!list_in_list(&t->queue_node_));
      list_add_head(&pinned_threads, &t->queue_node_);
    }
  }

  // Put pinned threads back on old_cpu's queue.
  while ((t = list_remove_head_type(&pinned_threads, Thread, queue_node_)) != NULL) {
    insert_in_run_queue_head(old_cpu, t);
  }

  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
  }
}

// check to see if the current thread needs to migrate to a new core
// the passed argument must be the current thread and must already be pushed into the READY state
static bool local_migrate_if_needed(Thread* curr_thread) TA_REQ(thread_lock) {
  DEBUG_ASSERT(curr_thread == Thread::Current::Get());
  DEBUG_ASSERT(curr_thread->state_ == THREAD_READY);

  // if the affinity mask does not include the current cpu, migrate us right now
  if (unlikely(get_allowed_cpus_mask(mp_get_active_mask(), curr_thread) &
               cpu_num_to_mask(curr_thread->curr_cpu_)) == 0) {
    migrate_current_thread(curr_thread);
    return true;
  }
  return false;
}

// potentially migrate a thread to a new core based on the affinity mask on the thread. If it's
// running or in a scheduler queue, handle it.
void sched_migrate(Thread* t) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  bool local_resched = false;
  cpu_mask_t accum_cpu_mask = 0;
  const cpu_mask_t active_mask = mp_get_active_mask();
  switch (t->state_) {
    case THREAD_RUNNING:
      // see if we need to migrate
      if (get_allowed_cpus_mask(active_mask, t) & cpu_num_to_mask(t->curr_cpu_)) {
        // it's running and the new mask contains the core it's already running on, nothing to do.
        // TRACEF("t %p nomigrate\n", t);
        return;
      }

      // we need to migrate
      if (t == Thread::Current::Get()) {
        // current thread, so just shove ourself into another cpu's queue and reschedule locally
        migrate_current_thread(t);
        return;
      } else {
        // running on another cpu, interrupt and let sched_preempt() sort it out
        accum_cpu_mask = cpu_num_to_mask(t->curr_cpu_);
      }
      break;
    case THREAD_READY:
      if (get_allowed_cpus_mask(active_mask, t) & cpu_num_to_mask(t->curr_cpu_)) {
        // it's ready and the new mask contains the core it's already waiting on, nothing to do.
        // TRACEF("t %p nomigrate\n", t);
        return;
      }

      // it's sitting in a run queue somewhere, so pull it out of that one and find a new home
      DEBUG_ASSERT_MSG(list_in_list(&t->queue_node_), "thread %p name %s curr_cpu %u\n", t, t->name_,
                       t->curr_cpu_);
      remove_from_run_queue(t, t->effec_priority_);

      find_cpu_and_insert(t, &local_resched, &accum_cpu_mask);
      break;
    default:
      // the other states do not matter, exit
      return;
  }

  // send some ipis based on the previous code
  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
  }
  if (local_resched) {
    sched_reschedule();
  }
}

// the effective priority of a thread has changed, do what is necessary to move the thread
// from different queues and inform us if we need to reschedule
static void sched_priority_changed(Thread* t, int old_prio, bool* local_resched,
                                   cpu_mask_t* accum_cpu_mask, PropagatePI propagate)
    TA_REQ(thread_lock) {
  switch (t->state_) {
    case THREAD_RUNNING:
      if (t->effec_priority_ < old_prio) {
        // we're currently running and dropped our effective priority, might want to resched
        if (t == Thread::Current::Get()) {
          *local_resched = true;
        } else {
          *accum_cpu_mask |= cpu_num_to_mask(t->curr_cpu_);
        }
      }
      break;
    case THREAD_READY:
      // it's sitting in a run queue somewhere, remove and add back to the proper queue on that cpu
      DEBUG_ASSERT_MSG(list_in_list(&t->queue_node_), "thread %p name %s curr_cpu %u\n", t, t->name_,
                       t->curr_cpu_);
      remove_from_run_queue(t, old_prio);

      // insert ourself into the new queue
      if (t->effec_priority_ > old_prio) {
        insert_in_run_queue_head(t->curr_cpu_, t);

        // we may now be higher priority than the current thread on this cpu, reschedule
        if (t->curr_cpu_ == arch_curr_cpu_num()) {
          *local_resched = true;
        } else {
          *accum_cpu_mask |= cpu_num_to_mask(t->curr_cpu_);
        }
      } else {
        insert_in_run_queue_tail(t->curr_cpu_, t);
      }

      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // it's blocked on something, sitting in a wait queue, so we may need to move it around
      // within the wait queue.
      // note it's possible to be blocked but not in a wait queue if the thread is in transition
      // from blocked to running
      if (t->blocking_wait_queue_) {
        wait_queue_priority_changed(t, old_prio, propagate);
      }
      break;
    default:
      // the other states do not matter, exit
      return;
  }
}

// Set the inherited priority to |pri|.
// pri < 0 disables priority inheritance and goes back to the naturally computed values
void sched_inherit_priority(Thread* t, int pri, bool* local_resched, cpu_mask_t* accum_cpu_mask) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (pri > HIGHEST_PRIORITY) {
    pri = HIGHEST_PRIORITY;
  }

  // adjust the priority and remember the old value
  t->inherited_priority_ = pri;
  int old_ep = t->effec_priority_;
  compute_effec_priority_(t);
  if (old_ep == t->effec_priority_) {
    // same effective priority, nothing to do
    return;
  }

  // see if we need to do something based on the state of the thread
  sched_priority_changed(t, old_ep, local_resched, accum_cpu_mask, PropagatePI::No);
}

// changes the thread's base priority and if the re-computed effective priority changed
//  then the thread is moved to the proper queue on the same processor and a re-schedule
//  might be issued.
void sched_change_priority(Thread* t, int pri) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (unlikely(t->state_ == THREAD_DEATH)) {
    return;
  }

  if (pri > HIGHEST_PRIORITY) {
    pri = HIGHEST_PRIORITY;
  }

  int old_ep = t->effec_priority_;
  t->base_priority_ = pri;
  t->priority_boost_ = 0;

  compute_effec_priority_(t);
  if (old_ep == t->effec_priority_) {
    // No effective change so we exit. The boost has reset but that's ok.
    return;
  }

  cpu_mask_t accum_cpu_mask = 0;
  bool local_resched = false;

  // see if we need to do something based on the state of the thread.
  sched_priority_changed(t, old_ep, &local_resched, &accum_cpu_mask, PropagatePI::Yes);

  // send some ipis based on the previous code
  if (accum_cpu_mask) {
    mp_reschedule(accum_cpu_mask, 0);
  }
  if (local_resched) {
    sched_reschedule();
  }
}

// Deadline profiles do not exist in the legacy scheduler.  During the
// transition to the new combination fair/deadline scheduler, if we attempt to
// assign a deadline profile to a thread, simply simulate the effect by
// assigning a high priority to the thread instead.  Before the deadline
// scheduler was introduced, P24 was the priority which was assigned to Very
// Important Threads.  We use a value of 30 instead, however, because with the
// introduction of deadline scheduling the timing for real-time tasks was
// cranked down even tighter than before.  We need to have a very high weight in
// order to even have a chance of meeting the expectations of a thread which is
// attempting to apply a deadline profile.
void sched_change_deadline(Thread* t, const zx_sched_deadline_params_t&) {
  sched_change_priority(t, 30);
}

// preemption timer that is set whenever a thread is scheduled
void sched_preempt_timer_tick(zx_time_t now) {
  // if the preemption timer went off on the idle or a real time thread, ignore it
  Thread* current_thread = Thread::Current::Get();
  if (unlikely(current_thread->IsRealTimeOrIdle())) {
    return;
  }

  LOCAL_KTRACE("sched_preempt_timer_tick", (uint32_t)current_thread->user_tid_,
               static_cast<uint32_t>(current_thread->remaining_time_slice_));

  // did this tick complete the time slice?
  DEBUG_ASSERT(now > current_thread->last_started_running_);
  zx_duration_t delta = zx_time_sub_time(now, current_thread->last_started_running_);
  if (delta >= current_thread->remaining_time_slice_) {
    // we completed the time slice, do not restart it and let the scheduler run
    current_thread->remaining_time_slice_ = 0;

    // set a timer to go off on the time slice interval from now
    timer_preempt_reset(zx_time_add_duration(now, THREAD_INITIAL_TIME_SLICE));

    // Mark a reschedule as pending.  The irq handler will call back
    // into us with sched_preempt().
    Thread::Current::PreemptSetPending();
  } else {
    // the timer tick must have fired early, reschedule and continue
    zx_time_t deadline = zx_time_add_duration(current_thread->last_started_running_,
                                              current_thread->remaining_time_slice_);
    timer_preempt_reset(deadline);
  }
}

// On ARM64 with safe-stack, it's no longer possible to use the unsafe-sp
// after arch_set_current_thread (we'd now see newthread's unsafe-sp instead!).
// Hence this function and everything it calls between this point and the
// the low-level context switch must be marked with __NO_SAFESTACK.
__NO_SAFESTACK static void final_context_switch(Thread* oldthread, Thread* newthread) {
  arch_set_current_thread(newthread);
  arch_context_switch(oldthread, newthread);
}

// Internal reschedule routine. The current thread needs to already be in whatever
// state and queues it needs to be in. This routine simply picks the next thread and
// switches to it.
void sched_resched_internal() {
  Thread* current_thread = Thread::Current::Get();
  uint cpu = arch_curr_cpu_num();

  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  // Aside from the thread_lock, spinlocks should never be held over a reschedule.
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);
  DEBUG_ASSERT_MSG(current_thread->state_ != THREAD_RUNNING, "state %d\n", current_thread->state_);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  CPU_STATS_INC(reschedules);

  // pick a new thread to run
  Thread* newthread = sched_get_top_thread(cpu);

  DEBUG_ASSERT(newthread);

  newthread->state_ = THREAD_RUNNING;

  Thread* oldthread = current_thread;
  oldthread->preempt_pending_ = false;

  LOCAL_KTRACE("resched old pri", (uint32_t)oldthread->user_tid_, oldthread->effec_priority_);
  LOCAL_KTRACE("resched new pri", (uint32_t)newthread->user_tid_, newthread->effec_priority_);

  // call this even if we're not changing threads, to handle the case where another
  // core rescheduled us but the work disappeared before we got to run.
  mp_prepare_current_cpu_idle_state(newthread->IsIdle());

  // if it's the same thread as we're already running, exit
  if (newthread == oldthread) {
    return;
  }

  zx_time_t now = current_time();

  // account for time used on the old thread
  DEBUG_ASSERT(now >= oldthread->last_started_running_);
  zx_duration_t old_runtime = zx_time_sub_time(now, oldthread->last_started_running_);
  oldthread->runtime_ns_ = zx_duration_add_duration(oldthread->runtime_ns_, old_runtime);
  oldthread->remaining_time_slice_ = zx_duration_sub_duration(
      oldthread->remaining_time_slice_, ktl::min(old_runtime, oldthread->remaining_time_slice_));

  // set up quantum for the new thread if it was consumed
  if (newthread->remaining_time_slice_ == 0) {
    newthread->remaining_time_slice_ = THREAD_INITIAL_TIME_SLICE;
  }

  // update system latency metrics.
  zx_duration_t queue_time_ns =
    newthread->IsIdle() ? 0 : zx_time_sub_time(now, newthread->last_started_running_);
  update_counters(queue_time_ns);

  newthread->last_started_running_ = now;

  // mark the cpu ownership of the threads
  if (oldthread->state_ != THREAD_READY) {
    oldthread->curr_cpu_ = INVALID_CPU;
  }
  newthread->last_cpu_ = cpu;
  newthread->curr_cpu_ = cpu;

  // if we selected the idle thread the cpu's run queue must be empty, so mark the
  // cpu as idle
  if (newthread->IsIdle()) {
    mp_set_cpu_idle(cpu);
  }

  if (newthread->IsRealtime()) {
    mp_set_cpu_realtime(cpu);
  } else {
    mp_set_cpu_non_realtime(cpu);
  }

  CPU_STATS_INC(context_switches);

  if (oldthread->IsIdle()) {
    zx_duration_t delta = zx_time_sub_time(now, oldthread->last_started_running_);
    percpu::Get(cpu).stats.idle_time =
        zx_duration_add_duration(percpu::Get(cpu).stats.idle_time, delta);
  }

  LOCAL_KTRACE("CS timeslice old", (uint32_t)oldthread->user_tid_,
               (uint32_t)oldthread->remaining_time_slice_);
  LOCAL_KTRACE("CS timeslice new", (uint32_t)newthread->user_tid_,
               (uint32_t)newthread->remaining_time_slice_);

  ktrace(TAG_CONTEXT_SWITCH, (uint32_t)newthread->user_tid_,
         (cpu | (oldthread->state_ << 8) | (oldthread->effec_priority_ << 16) |
          (newthread->effec_priority_ << 24)),
         (uint32_t)(uintptr_t)oldthread, (uint32_t)(uintptr_t)newthread);

  if (newthread->IsRealTimeOrIdle()) {
    if (!oldthread->IsRealTimeOrIdle()) {
      // if we're switching from a non real time to a real time, cancel
      // the preemption timer.
      TRACE_CONTEXT_SWITCH("stop preempt, cpu %u, old %p (%s), new %p (%s)\n", cpu, oldthread,
                           oldthread->name_, newthread, newthread->name_);
      timer_preempt_cancel();
    }
  } else {
    // set up a one shot timer to handle the remaining time slice on this thread
    TRACE_CONTEXT_SWITCH("start preempt, cpu %u, old %p (%s), new %p (%s)\n", cpu, oldthread,
                         oldthread->name_, newthread, newthread->name_);

    // make sure the time slice is reasonable
    DEBUG_ASSERT(newthread->remaining_time_slice_ > 0 &&
                 newthread->remaining_time_slice_ < ZX_SEC(1));

    timer_preempt_reset(zx_time_add_duration(now, newthread->remaining_time_slice_));
  }

  // set some optional target debug leds
  target_set_debug_led(0, !newthread->IsIdle());

  TRACE_CONTEXT_SWITCH(
      "cpu %u old %p (%s, pri %d [%d:%d], flags 0x%x) "
      "new %p (%s, pri %d [%d:%d], flags 0x%x)\n",
      cpu, oldthread, oldthread->name_, oldthread->effec_priority_, oldthread->base_priority_,
      oldthread->priority_boost_, oldthread->flags_, newthread, newthread->name_,
      newthread->effec_priority_, newthread->base_priority_, newthread->priority_boost_,
      newthread->flags_);

  // see if we need to swap mmu context
  if (newthread->aspace_ != oldthread->aspace_) {
    vmm_context_switch(oldthread->aspace_, newthread->aspace_);
  }

  // do the low level context switch
  final_context_switch(oldthread, newthread);
}
