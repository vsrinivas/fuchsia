// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/scheduler.h"

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
#include <zircon/types.h>

#include <algorithm>
#include <new>

#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/move.h>
#include <vm/vm.h>

using ffl::Expression;
using ffl::Fixed;
using ffl::FromRatio;
using ffl::Round;

// Enable/disable ktraces local to this file.
#define LOCAL_KTRACE_ENABLE 0 || WITH_DETAILED_SCHEDULER_TRACING

#define LOCAL_KTRACE(string, args...)                                                         \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_STRING_REF(string), \
               ##args)

#define LOCAL_KTRACE_FLOW_BEGIN(string, flow_id)                                              \
  ktrace_flow_begin(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_GRP_SCHEDULER, \
                    KTRACE_STRING_REF(string), flow_id)

#define LOCAL_KTRACE_FLOW_END(string, flow_id)                                              \
  ktrace_flow_end(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_GRP_SCHEDULER, \
                  KTRACE_STRING_REF(string), flow_id)

using LocalTraceDuration =
    TraceDuration<TraceEnabled<LOCAL_KTRACE_ENABLE>, KTRACE_GRP_SCHEDULER, TraceContext::Cpu>;

// Enable/disable console traces local to this file.
#define LOCAL_TRACE 0

#define SCHED_LTRACEF(str, args...) LTRACEF("[%u] " str, arch_curr_cpu_num(), ##args)
#define SCHED_TRACEF(str, args...) TRACEF("[%u] " str, arch_curr_cpu_num(), ##args)

// Counters to track system load metrics.
KCOUNTER(demand_counter, "thread.demand_accum")
KCOUNTER(latency_counter, "thread.latency_accum")
KCOUNTER(runnable_counter, "thread.runnable_accum")
KCOUNTER(samples_counter, "thread.samples_accum")

namespace {

// Conversion table entry. Scales the integer argument to a fixed-point weight
// in the interval (0.0, 1.0].
struct WeightTableEntry {
  constexpr WeightTableEntry(int64_t value)
      : value{FromRatio<int64_t>(value, SchedWeight::Format::Power)} {}
  constexpr operator SchedWeight() const { return value; }
  const SchedWeight value;
};

// Table of fixed-point constants converting from kernel priority to fair
// scheduler weight.
constexpr WeightTableEntry kPriorityToWeightTable[] = {
    121,   149,   182,   223,   273,   335,   410,   503,   616,   754,  924,
    1132,  1386,  1698,  2080,  2549,  3122,  3825,  4685,  5739,  7030, 8612,
    10550, 12924, 15832, 19394, 23757, 29103, 35651, 43672, 53499, 65536};

// Converts from kernel priority value in the interval [0, 31] to weight in the
// interval (0.0, 1.0]. See the definition of SchedWeight for an explanation of
// the weight distribution.
constexpr SchedWeight PriorityToWeight(int priority) { return kPriorityToWeightTable[priority]; }

// The minimum possible weight and its reciprocal.
constexpr SchedWeight kMinWeight = PriorityToWeight(LOWEST_PRIORITY);
constexpr SchedWeight kReciprocalMinWeight = 1 / kMinWeight;

// Utility operator to make expressions more succinct that update thread times
// and durations of basic types using the fixed-point counterparts.
constexpr zx_time_t& operator+=(zx_time_t& value, SchedDuration delta) {
  value += delta.raw_value();
  return value;
}

// On ARM64 with safe-stack, it's no longer possible to use the unsafe-sp
// after set_current_thread (we'd now see newthread's unsafe-sp instead!).
// Hence this function and everything it calls between this point and the
// the low-level context switch must be marked with __NO_SAFESTACK.
__NO_SAFESTACK void FinalContextSwitch(thread_t* oldthread, thread_t* newthread) {
  set_current_thread(newthread);
  arch_context_switch(oldthread, newthread);
}

// Writes a context switch record to the ktrace buffer. This is always enabled
// so that user mode tracing can track which threads are running.
inline void TraceContextSwitch(const thread_t* current_thread, const thread_t* next_thread,
                               cpu_num_t current_cpu) {
  const auto raw_current = reinterpret_cast<uintptr_t>(current_thread);
  const auto raw_next = reinterpret_cast<uintptr_t>(next_thread);
  const auto current = static_cast<uint32_t>(raw_current);
  const auto next = static_cast<uint32_t>(raw_next);
  const auto user_tid = static_cast<uint32_t>(next_thread->user_tid);
  const uint32_t context = current_cpu | (current_thread->state << 8) |
                           (current_thread->base_priority << 16) |
                           (next_thread->base_priority << 24);

  ktrace(TAG_CONTEXT_SWITCH, user_tid, context, current, next);
}

// Returns a sufficiently unique flow id for a thread based on the thread id and
// queue generation count. This flow id cannot be used across enqueues because
// the generation count changes during enqueue.
inline uint64_t FlowIdFromThreadGeneration(const thread_t* thread) {
  const int kRotationBits = 32;
  const uint64_t rotated_tid =
      (thread->user_tid << kRotationBits) | (thread->user_tid >> kRotationBits);
  return rotated_tid ^ thread->scheduler_state.generation();
}

// Calculate a mask of CPUs a thread is allowed to run on, based on the thread's
// affinity mask and what CPUs are online.
cpu_mask_t GetAllowedCpusMask(cpu_mask_t active_mask, const thread_t* thread) {
  // The thread may run on any active CPU allowed by both its hard and
  // soft CPU affinity.
  const cpu_mask_t soft_affinity = thread->soft_affinity;
  const cpu_mask_t hard_affinity = thread->hard_affinity;
  const cpu_mask_t available_mask = active_mask & soft_affinity & hard_affinity;
  if (likely(available_mask != 0)) {
    return available_mask;
  }

  // There is no CPU allowed by the intersection of active CPUs, the
  // hard affinity mask, and the soft affinity mask. Ignore the soft
  // affinity.
  return active_mask & hard_affinity;
}

}  // anonymous namespace

void Scheduler::Dump() {
  printf("\tweight_total=%#x runnable_tasks=%d vtime=%ld period=%ld\n",
         static_cast<uint32_t>(weight_total_.raw_value()), runnable_task_count_,
         virtual_time_.raw_value(), scheduling_period_grans_.raw_value());

  if (active_thread_ != nullptr) {
    const SchedulerState* const state = &active_thread_->scheduler_state;
    printf("\t-> name=%s weight=%#x vstart=%ld vfinish=%ld time_slice_ns=%ld\n",
           active_thread_->name, static_cast<uint32_t>(state->weight_.raw_value()),
           state->virtual_start_time_.raw_value(), state->virtual_finish_time_.raw_value(),
           state->time_slice_ns_.raw_value());
  }

  for (const thread_t& thread : run_queue_) {
    const SchedulerState* const state = &thread.scheduler_state;
    printf("\t   name=%s weight=%#x vstart=%ld vfinish=%ld time_slice_ns=%ld\n", thread.name,
           static_cast<uint32_t>(state->weight_.raw_value()),
           state->virtual_start_time_.raw_value(), state->virtual_finish_time_.raw_value(),
           state->time_slice_ns_.raw_value());
  }
}

SchedWeight Scheduler::GetTotalWeight() const {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return weight_total_;
}

size_t Scheduler::GetRunnableTasks() const {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return static_cast<size_t>(runnable_task_count_);
}

Scheduler* Scheduler::Get() { return Get(arch_curr_cpu_num()); }

Scheduler* Scheduler::Get(cpu_num_t cpu) { return &percpu::Get(cpu).scheduler; }

void Scheduler::InitializeThread(thread_t* thread, int priority) {
  new (&thread->scheduler_state) SchedulerState{PriorityToWeight(priority)};
  thread->base_priority = priority;
  thread->effec_priority = priority;
  thread->inherited_priority = -1;
  thread->priority_boost = 0;
}

thread_t* Scheduler::DequeueThread() { return run_queue_.pop_front(); }

// Updates the system load metrics. Updates happen only when the active thread
// changes or the time slice expires.
void Scheduler::UpdateCounters(SchedDuration queue_time_ns) {
  demand_counter.Add(weight_total_.raw_value());
  runnable_counter.Add(runnable_task_count_);
  latency_counter.Add(queue_time_ns.raw_value());
  samples_counter.Add(1);
}

// Selects a thread to run. Performs any necessary maintenanace if the current
// thread is changing, depending on the reason for the change.
thread_t* Scheduler::EvaluateNextThread(SchedTime now, thread_t* current_thread,
                                        bool timeslice_expired) {
  const bool is_idle = thread_is_idle(current_thread);
  const bool is_active = current_thread->state == THREAD_READY;
  const cpu_num_t current_cpu = arch_curr_cpu_num();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();
  const bool needs_migration =
      (GetAllowedCpusMask(active_mask, current_thread) & current_cpu_mask) == 0;

  if (is_active && unlikely(needs_migration)) {
    // The current CPU is not in the thread's affinity mask, find a new CPU
    // and move it to that queue.
    current_thread->state = THREAD_READY;
    Remove(current_thread);

    const cpu_num_t target_cpu = FindTargetCpu(current_thread);
    Scheduler* const target = Get(target_cpu);
    DEBUG_ASSERT(target != this);

    target->Insert(now, current_thread);
    mp_reschedule(cpu_num_to_mask(target_cpu), 0);
  } else if (is_active && likely(!is_idle)) {
    // If the timeslice expired put the current thread back in the runqueue,
    // otherwise continue to run it.
    if (timeslice_expired) {
      UpdateThreadTimeline(current_thread, Placement::Insertion);
      QueueThread(current_thread, Placement::Insertion, now);
    } else {
      return current_thread;
    }
  } else if (!is_active && likely(!is_idle)) {
    // The current thread is no longer ready, remove its accounting.
    Remove(current_thread);
  }

  // The current thread is no longer running or has returned to the runqueue.
  // Select another thread to run.
  if (likely(!run_queue_.is_empty())) {
    return DequeueThread();
  } else {
    const cpu_num_t current_cpu = arch_curr_cpu_num();
    return &percpu::Get(current_cpu).idle_thread;
  }
}

cpu_num_t Scheduler::FindTargetCpu(thread_t* thread) {
  LocalTraceDuration trace{"find_target: cpu,avail"_stringref};

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  const cpu_mask_t last_cpu_mask = cpu_num_to_mask(thread->last_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();
  const cpu_mask_t idle_mask = mp_get_idle_mask();

  // Determine the set of CPUs the thread is allowed to run on.
  //
  // Threads may be created and resumed before the thread init level. Work around
  // an empty active mask by assuming the current cpu is scheduleable.
  const cpu_mask_t available_mask =
      active_mask != 0 ? GetAllowedCpusMask(active_mask, thread) : current_cpu_mask;
  DEBUG_ASSERT_MSG(available_mask != 0,
                   "thread=%s affinity=%#x soft_affinity=%#x active=%#x "
                   "idle=%#x arch_ints_disabled=%d",
                   thread->name, thread->hard_affinity, thread->soft_affinity, active_mask,
                   mp_get_idle_mask(), arch_ints_disabled());

  LOCAL_KTRACE("target_mask: online,active", mp_get_online_mask(), active_mask);

  cpu_num_t target_cpu;
  Scheduler* target_queue;

  // Select an initial target.
  if (last_cpu_mask & available_mask && (!idle_mask || last_cpu_mask & idle_mask)) {
    target_cpu = thread->last_cpu;
  } else if (current_cpu_mask & available_mask) {
    target_cpu = arch_curr_cpu_num();
  } else {
    target_cpu = lowest_cpu_set(available_mask);
  }

  target_queue = Get(target_cpu);

  // See if there is a better target in the set of available CPUs.
  // TODO(eieio): Replace this with a search in order of increasing cache
  // distance from the initial target cpu when topology information is available.
  // TODO(eieio): Add some sort of threshold to terminate search when a sufficiently
  // unloaded target is found.
  cpu_mask_t remaining_mask = available_mask & ~cpu_num_to_mask(target_cpu);
  while (remaining_mask != 0 && target_queue->weight_total_ > SchedWeight{0}) {
    const cpu_num_t candidate_cpu = lowest_cpu_set(remaining_mask);
    Scheduler* const candidate_queue = Get(candidate_cpu);

    if (candidate_queue->weight_total_ < target_queue->weight_total_) {
      target_cpu = candidate_cpu;
      target_queue = candidate_queue;
    }

    remaining_mask &= ~cpu_num_to_mask(candidate_cpu);
  }

  SCHED_LTRACEF("thread=%s target_cpu=%u\n", thread->name, target_cpu);
  trace.End(target_cpu, remaining_mask);
  return target_cpu;
}

void Scheduler::UpdateTimeline(SchedTime now) {
  LocalTraceDuration trace{"update_vtime"_stringref};

  const Expression runtime_ns = now - last_update_time_ns_;
  last_update_time_ns_ = now;

  if (weight_total_ > SchedWeight{0}) {
    virtual_time_ += runtime_ns;
  }

  trace.End(Round<uint64_t>(runtime_ns), Round<uint64_t>(virtual_time_));
}

void Scheduler::RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace) {
  LocalTraceDuration trace{"reschedule_common"_stringref};

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  thread_t* const current_thread = get_current_thread();
  SchedulerState* const current_state = &current_thread->scheduler_state;

  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  // Aside from the thread_lock, spinlocks should never be held over a reschedule.
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);
  DEBUG_ASSERT_MSG(current_thread->state != THREAD_RUNNING, "state %d\n", current_thread->state);
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT_MSG(current_cpu == this_cpu(), "current_cpu=%u this_cpu=%u", current_cpu,
                   this_cpu());

  CPU_STATS_INC(reschedules);

  UpdateTimeline(now);

  const SchedTime total_runtime_ns = now - start_of_current_time_slice_ns_;
  const SchedDuration actual_runtime_ns = now - current_thread->last_started_running;
  current_thread->last_started_running = now.raw_value();

  // Update the runtime accounting for the thread that just ran.
  current_thread->runtime_ns += actual_runtime_ns;

  // Adjust the rate of the current thread when demand changes. Changes in
  // demand could be due to threads entering or leaving the run queue, or due
  // to weights changing in the current or enqueued threads.
  if (!thread_is_idle(current_thread) && weight_total_ != scheduled_weight_total_ &&
      total_runtime_ns < current_state->time_slice_ns_) {
    LocalTraceDuration trace_adjust_rate{"adjust_rate"_stringref};
    scheduled_weight_total_ = weight_total_;

    const SchedDuration time_slice_ns = CalculateTimeslice(current_thread);
    const bool timeslice_changed = time_slice_ns != current_state->time_slice_ns_;
    const bool timeslice_remaining = total_runtime_ns < time_slice_ns;

    // Update the preemption timer if necessary.
    if (timeslice_changed && timeslice_remaining) {
      const SchedTime absolute_deadline_ns = start_of_current_time_slice_ns_ + time_slice_ns;
      timer_preempt_reset(absolute_deadline_ns.raw_value());
    }

    current_state->time_slice_ns_ = time_slice_ns;
    trace_adjust_rate.End(Round<uint64_t>(time_slice_ns), Round<uint64_t>(total_runtime_ns));
  }

  const bool timeslice_expired = total_runtime_ns >= current_state->time_slice_ns_;

  // Select a thread to run.
  thread_t* const next_thread = EvaluateNextThread(now, current_thread, timeslice_expired);
  DEBUG_ASSERT(next_thread != nullptr);

  SCHED_LTRACEF("current={%s, %s} next={%s, %s} expired=%d is_empty=%d front=%s\n",
                current_thread->name, ToString(current_thread->state), next_thread->name,
                ToString(next_thread->state), timeslice_expired, run_queue_.is_empty(),
                run_queue_.is_empty() ? "[none]" : run_queue_.front().name);

  // Update the state of the current and next thread.
  current_thread->preempt_pending = false;
  next_thread->state = THREAD_RUNNING;
  next_thread->last_cpu = current_cpu;
  next_thread->curr_cpu = current_cpu;

  active_thread_ = next_thread;

  // Always call to handle races between reschedule IPIs and changes to the run queue.
  mp_prepare_current_cpu_idle_state(thread_is_idle(next_thread));

  if (thread_is_idle(next_thread)) {
    mp_set_cpu_idle(current_cpu);
  } else {
    mp_set_cpu_busy(current_cpu);
  }

  // The task is always non-realtime when managed by this scheduler.
  // TODO(eieio): Revisit this when deadline scheduling is addressed.
  mp_set_cpu_non_realtime(current_cpu);

  if (thread_is_idle(current_thread)) {
    percpu::Get(current_cpu).stats.idle_time += actual_runtime_ns;
  }

  if (thread_is_idle(next_thread)) {
    LocalTraceDuration trace_stop_preemption{"stop_preemption"_stringref};
    SCHED_LTRACEF("Stop preemption timer: current=%s next=%s\n", current_thread->name,
                  next_thread->name);
    UpdateCounters(SchedDuration{0});
    next_thread->last_started_running = now.raw_value();
    timer_preempt_cancel();
  } else if (timeslice_expired || next_thread != current_thread) {
    LocalTraceDuration trace_start_preemption{"start_preemption: now,deadline"_stringref};

    // Re-compute the time slice for the new thread based on the latest state.
    NextThreadTimeslice(next_thread);

    // Update the preemption time based on the time slice.
    SchedulerState* const next_state = &next_thread->scheduler_state;
    const SchedTime absolute_deadline_ns = now + next_state->time_slice_ns_;

    // Compute the time the next thread spent in the run queue. The value of
    // last_started_running for the current thread is updated at the top of
    // this method: when the current and next thread are the same, the queue
    // time is zero. Otherwise, last_started_running is the time the next thread
    // entered the run queue.
    const SchedDuration queue_time_ns = now - next_thread->last_started_running;
    UpdateCounters(queue_time_ns);

    next_thread->last_started_running = now.raw_value();
    start_of_current_time_slice_ns_ = now;
    scheduled_weight_total_ = weight_total_;

    SCHED_LTRACEF("Start preemption timer: current=%s next=%s now=%ld deadline=%ld\n",
                  current_thread->name, next_thread->name, now.raw_value(),
                  absolute_deadline_ns.raw_value());
    timer_preempt_reset(absolute_deadline_ns.raw_value());

    trace_start_preemption.End(Round<uint64_t>(now), Round<uint64_t>(absolute_deadline_ns));

    // Emit a flow end event to match the flow begin event emitted when the
    // thread was enqueued. Emitting in this scope ensures that thread just
    // came from the run queue (and is not the idle thread).
    LOCAL_KTRACE_FLOW_END("sched_latency", FlowIdFromThreadGeneration(next_thread));
  }

  if (next_thread != current_thread) {
    LOCAL_KTRACE("reschedule current: count,slice", runnable_task_count_,
                 Round<uint64_t>(current_thread->scheduler_state.time_slice_ns_));
    LOCAL_KTRACE("reschedule next: wsum,slice", weight_total_.raw_value(),
                 Round<uint64_t>(next_thread->scheduler_state.time_slice_ns_));

    TraceContextSwitch(current_thread, next_thread, current_cpu);

    // Blink the optional debug LEDs on the target.
    target_set_debug_led(0, !thread_is_idle(next_thread));

    SCHED_LTRACEF("current=(%s, flags 0x%#x) next=(%s, flags 0x%#x)\n", current_thread->name,
                  current_thread->flags, next_thread->name, next_thread->flags);

    if (current_thread->aspace != next_thread->aspace) {
      vmm_context_switch(current_thread->aspace, next_thread->aspace);
    }

    CPU_STATS_INC(context_switches);

    // Prevent the scheduler durations from spanning the context switch.
    // Some context switches do not resume within this method on the other
    // thread, which results in unterminated durations. All of the callers
    // with durations tail-call this method, so terminating the duration
    // here should not cause significant inaccuracy of the outer duration.
    trace.End();
    if (end_outer_trace) {
      end_outer_trace();
    }
    FinalContextSwitch(current_thread, next_thread);
  }
}

void Scheduler::UpdatePeriod() {
  LocalTraceDuration trace{"update_period"_stringref};

  DEBUG_ASSERT(runnable_task_count_ >= 0);
  DEBUG_ASSERT(minimum_granularity_ns_ > 0);
  DEBUG_ASSERT(peak_latency_ns_ > 0);
  DEBUG_ASSERT(target_latency_ns_ > 0);

  const int64_t num_tasks = runnable_task_count_;
  const int64_t peak_tasks = Round<int64_t>(peak_latency_ns_ / minimum_granularity_ns_);
  const int64_t normal_tasks = Round<int64_t>(target_latency_ns_ / minimum_granularity_ns_);

  // The scheduling period stretches when there are too many tasks to fit
  // within the target latency.
  scheduling_period_grans_ = SchedDuration{num_tasks > normal_tasks ? num_tasks : normal_tasks};

  SCHED_LTRACEF("num_tasks=%ld peak_tasks=%ld normal_tasks=%ld period_grans=%ld\n", num_tasks,
                peak_tasks, normal_tasks, scheduling_period_grans_.raw_value());

  trace.End(Round<uint64_t>(scheduling_period_grans_), num_tasks);
}

SchedDuration Scheduler::CalculateTimeslice(thread_t* thread) {
  LocalTraceDuration trace{"calculate_timeslice: w,wt"_stringref};
  SchedulerState* const state = &thread->scheduler_state;

  // Calculate the relative portion of the scheduling period.
  const SchedWeight proportional_time_slice_grans =
      scheduling_period_grans_ * state->weight_ / weight_total_;

  // Ensure that the time slice is at least the minimum granularity.
  const int64_t time_slice_grans = Round<int64_t>(proportional_time_slice_grans);
  const int64_t minimum_time_slice_grans = time_slice_grans > 0 ? time_slice_grans : 1;

  // Calcluate the time slice in nanoseconds.
  const SchedDuration time_slice_ns = minimum_time_slice_grans * minimum_granularity_ns_;

  trace.End(state->weight_.raw_value(), weight_total_.raw_value());
  return time_slice_ns;
}

void Scheduler::NextThreadTimeslice(thread_t* thread) {
  LocalTraceDuration trace{"next_timeslice: s,w"_stringref};

  if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
    return;
  }

  SchedulerState* const state = &thread->scheduler_state;
  state->time_slice_ns_ = CalculateTimeslice(thread);

  SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x time_slice_ns=%ld\n", thread->name,
                static_cast<uint32_t>(weight_total_.raw_value()),
                static_cast<uint32_t>(state->weight_.raw_value()),
                state->time_slice_ns_.raw_value());

  trace.End(Round<uint64_t>(state->time_slice_ns_), state->weight_.raw_value());
}

void Scheduler::UpdateThreadTimeline(thread_t* thread, Placement placement) {
  LocalTraceDuration trace{"update_timeline: vs,vf"_stringref};

  if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
    return;
  }

  SchedulerState* const state = &thread->scheduler_state;

  // Update virtual timeline.
  if (placement == Placement::Insertion) {
    state->virtual_start_time_ = std::max(state->virtual_finish_time_, virtual_time_);
  }

  const SchedDuration scheduling_period_ns = scheduling_period_grans_ * minimum_granularity_ns_;
  const SchedWeight rate = kReciprocalMinWeight * state->weight_;
  const SchedDuration delta_norm = scheduling_period_ns / rate;
  state->virtual_finish_time_ = state->virtual_start_time_ + delta_norm;

  DEBUG_ASSERT_MSG(state->virtual_start_time_ < state->virtual_finish_time_,
                   "vstart=%ld vfinish=%ld delta_norm=%ld\n",
                   state->virtual_start_time_.raw_value(), state->virtual_finish_time_.raw_value(),
                   delta_norm.raw_value());

  SCHED_LTRACEF("name=%s vstart=%ld vfinish=%ld lag=%ld vtime=%ld\n", thread->name,
                state->virtual_start_time_.raw_value(), state->virtual_finish_time_.raw_value(),
                state->lag_time_ns_.raw_value(), virtual_time_.raw_value());

  trace.End(Round<uint64_t>(state->virtual_start_time_),
            Round<uint64_t>(state->virtual_finish_time_));
}

void Scheduler::QueueThread(thread_t* thread, Placement placement, SchedTime now) {
  LocalTraceDuration trace{"queue_thread"_stringref};

  DEBUG_ASSERT(thread->state == THREAD_READY);
  DEBUG_ASSERT(!thread_is_idle(thread));
  DEBUG_ASSERT(placement == Placement::Adjustment || now != SchedTime{0});
  SCHED_LTRACEF("QueueThread: thread=%s\n", thread->name);

  // Only update the generation, enqueue time, and emit a flow event if this
  // is an insertion. In constrast, an adjustment only changes the queue
  // position due to a weight change and should not perform these actions.
  if (placement == Placement::Insertion) {
    thread->scheduler_state.generation_ = ++generation_count_;

    // Reuse this member to track the time the thread enters the run queue.
    // It is not read outside of the scheduler unless the thread state is
    // THREAD_RUNNING.
    thread->last_started_running = now.raw_value();
  }

  run_queue_.insert(thread);
  LOCAL_KTRACE("queue_thread");

  if (placement == Placement::Insertion) {
    LOCAL_KTRACE_FLOW_BEGIN("sched_latency", FlowIdFromThreadGeneration(thread));
  }
}

void Scheduler::Insert(SchedTime now, thread_t* thread) {
  LocalTraceDuration trace{"insert"_stringref};

  DEBUG_ASSERT(thread->state == THREAD_READY);
  DEBUG_ASSERT(!thread_is_idle(thread));

  SchedulerState* const state = &thread->scheduler_state;

  // Ensure insertion happens only once, even if Unblock is called multiple times.
  if (state->OnInsert()) {
    runnable_task_count_++;
    DEBUG_ASSERT(runnable_task_count_ != 0);

    UpdateTimeline(now);
    UpdatePeriod();

    // Insertion can happen from a different CPU. Set the thread's current
    // CPU to the one this scheduler instance services.
    thread->curr_cpu = this_cpu();

    // Factor this task into the run queue.
    weight_total_ += state->weight_;
    DEBUG_ASSERT(weight_total_ > SchedWeight{0});

    UpdateThreadTimeline(thread, Placement::Insertion);
    QueueThread(thread, Placement::Insertion, now);
  }
}

void Scheduler::Remove(thread_t* thread) {
  LocalTraceDuration trace{"remove"_stringref};

  DEBUG_ASSERT(!thread_is_idle(thread));

  SchedulerState* const state = &thread->scheduler_state;
  DEBUG_ASSERT(!state->InQueue());

  // Ensure that removal happens only once, even if Block() is called multiple times.
  if (state->OnRemove()) {
    DEBUG_ASSERT(runnable_task_count_ > 0);
    runnable_task_count_--;

    UpdatePeriod();

    thread->curr_cpu = INVALID_CPU;

    state->virtual_start_time_ = SchedNs(0);
    state->virtual_finish_time_ = SchedNs(0);

    // Factor this task out of the run queue.
    weight_total_ -= state->weight_;
    DEBUG_ASSERT(weight_total_ >= SchedWeight{0});

    SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x lag_time_ns=%ld\n", thread->name,
                  static_cast<uint32_t>(weight_total_.raw_value()),
                  static_cast<uint32_t>(state->weight_.raw_value()),
                  state->lag_time_ns_.raw_value());
  }
}

void Scheduler::Block() {
  LocalTraceDuration trace{"sched_block"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* const current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

  Scheduler::Get()->RescheduleCommon(now, trace.Completer());
}

bool Scheduler::Unblock(thread_t* thread) {
  LocalTraceDuration trace{"sched_unblock"_stringref};

  DEBUG_ASSERT(thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("thread=%s now=%ld\n", thread->name, now.raw_value());

  const cpu_num_t target_cpu = FindTargetCpu(thread);
  Scheduler* const target = Get(target_cpu);

  thread->state = THREAD_READY;
  target->Insert(now, thread);

  if (target_cpu == arch_curr_cpu_num()) {
    return true;
  } else {
    mp_reschedule(cpu_num_to_mask(target_cpu), 0);
    return false;
  }
}

bool Scheduler::Unblock(list_node* list) {
  LocalTraceDuration trace{"sched_unblock_list"_stringref};

  DEBUG_ASSERT(list);
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  const SchedTime now = CurrentTime();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  thread_t* thread;
  while ((thread = list_remove_tail_type(list, thread_t, queue_node)) != nullptr) {
    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(!thread_is_idle(thread));

    SCHED_LTRACEF("thread=%s now=%ld\n", thread->name, now.raw_value());

    const cpu_num_t target_cpu = FindTargetCpu(thread);
    Scheduler* const target = Get(target_cpu);

    thread->state = THREAD_READY;
    target->Insert(now, thread);

    cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
  }

  // Issue reschedule IPIs to other CPUs.
  if (cpus_to_reschedule_mask) {
    mp_reschedule(cpus_to_reschedule_mask, 0);
  }

  // Return true if the current CPU is in the mask.
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  return cpus_to_reschedule_mask & current_cpu_mask;
}

void Scheduler::UnblockIdle(thread_t* thread) {
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  DEBUG_ASSERT(thread_is_idle(thread));
  DEBUG_ASSERT(thread->hard_affinity && (thread->hard_affinity & (thread->hard_affinity - 1)) == 0);

  SCHED_LTRACEF("thread=%s now=%ld\n", thread->name, current_time());

  thread->state = THREAD_READY;
  thread->curr_cpu = lowest_cpu_set(thread->hard_affinity);
}

void Scheduler::Yield() {
  LocalTraceDuration trace{"sched_yield"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* const current_thread = get_current_thread();
  SchedulerState* const current_state = &current_thread->scheduler_state;
  DEBUG_ASSERT(!thread_is_idle(current_thread));

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

  // Update the virtual timeline in preparation for snapping the thread's
  // virtual finish time to the current virtual time.
  Scheduler* const current = Get();
  current->UpdateTimeline(now);

  // Set the time slice to expire now. The thread is re-evaluated with with
  // zero lag against other competing threads and may skip lower priority
  // threads with similar arrival times.
  current_thread->state = THREAD_READY;
  current_state->virtual_finish_time_ = current->virtual_time_;
  current_state->time_slice_ns_ = now - current->start_of_current_time_slice_ns_;
  DEBUG_ASSERT(current_state->time_slice_ns_ >= 0);

  current->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Preempt() {
  LocalTraceDuration trace{"sched_preempt"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* current_thread = get_current_thread();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  DEBUG_ASSERT(current_thread->curr_cpu == current_cpu);
  DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

  current_thread->state = THREAD_READY;
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Reschedule() {
  LocalTraceDuration trace{"sched_reschedule"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* current_thread = get_current_thread();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  if (current_thread->disable_counts != 0) {
    current_thread->preempt_pending = true;
    return;
  }

  DEBUG_ASSERT(current_thread->curr_cpu == current_cpu);
  DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

  current_thread->state = THREAD_READY;
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::RescheduleInternal() { Get()->RescheduleCommon(CurrentTime()); }

void Scheduler::Migrate(thread_t* thread) {
  LocalTraceDuration trace{"sched_migrate"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  cpu_mask_t cpus_to_reschedule_mask = 0;

  if (thread->state == THREAD_RUNNING) {
    const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
    if (!(GetAllowedCpusMask(mp_get_active_mask(), thread) & thread_cpu_mask)) {
      // Mark the CPU the thread is running on for reschedule. The
      // scheduler on that CPU will take care of the actual migration.
      cpus_to_reschedule_mask |= thread_cpu_mask;
    }
  } else if (thread->state == THREAD_READY) {
    const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
    if (!(GetAllowedCpusMask(mp_get_active_mask(), thread) & thread_cpu_mask)) {
      Scheduler* current = Get(thread->curr_cpu);

      DEBUG_ASSERT(thread->scheduler_state.InQueue());
      current->run_queue_.erase(*thread);
      current->Remove(thread);

      const cpu_num_t target_cpu = FindTargetCpu(thread);
      Scheduler* const target = Get(target_cpu);
      target->Insert(CurrentTime(), thread);

      cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
    }
  }

  if (cpus_to_reschedule_mask) {
    mp_reschedule(cpus_to_reschedule_mask, 0);
  }

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  if (cpus_to_reschedule_mask & current_cpu_mask) {
    trace.End();
    Reschedule();
  }
}

void Scheduler::MigrateUnpinnedThreads(cpu_num_t current_cpu) {
  LocalTraceDuration trace{"sched_migrate_unpinned"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  DEBUG_ASSERT(current_cpu == arch_curr_cpu_num());

  // Prevent this CPU from being selected as a target for scheduling threads.
  mp_set_curr_cpu_active(false);

  const SchedTime now = CurrentTime();
  Scheduler* const current = Get(current_cpu);
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);

  RunQueue pinned_threads;
  cpu_mask_t cpus_to_reschedule_mask = 0;
  while (!current->run_queue_.is_empty()) {
    thread_t* const thread = current->DequeueThread();

    if (thread->hard_affinity == current_cpu_mask) {
      // Keep track of threads pinned to this CPU.
      pinned_threads.insert(thread);
    } else {
      // Move unpinned threads to another available CPU.
      current->Remove(thread);

      const cpu_num_t target_cpu = FindTargetCpu(thread);
      Scheduler* const target = Get(target_cpu);
      DEBUG_ASSERT(target != current);

      target->Insert(now, thread);
      cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
    }
  }

  // Return the pinned threads to the run queue.
  current->run_queue_ = ktl::move(pinned_threads);

  if (cpus_to_reschedule_mask) {
    mp_reschedule(cpus_to_reschedule_mask, 0);
  }
}

void Scheduler::UpdateWeightCommon(thread_t* thread, int original_priority, SchedWeight weight,
                                   cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate) {
  SchedulerState* const state = &thread->scheduler_state;

  switch (thread->state) {
    case THREAD_INITIAL:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // Adjust the weight of the thread so that the correct value is
      // available when the thread enters the run queue.
      state->weight_ = weight;
      break;

    case THREAD_RUNNING:
    case THREAD_READY: {
      DEBUG_ASSERT(is_valid_cpu_num(thread->curr_cpu));
      Scheduler* const current = Get(thread->curr_cpu);

      // Adjust the weight of the thread and the run queue. The time slice
      // of the running thread will be adjusted during reschedule due to
      // the change in demand on the run queue.
      current->weight_total_ -= state->weight_;
      current->weight_total_ += weight;
      state->weight_ = weight;

      if (thread->state == THREAD_READY) {
        DEBUG_ASSERT(state->InQueue());
        DEBUG_ASSERT(state->active());

        // Adjust the position of the thread in the run queue based on
        // the new weight.
        current->run_queue_.erase(*thread);
        current->UpdateThreadTimeline(thread, Placement::Adjustment);
        current->QueueThread(thread, Placement::Adjustment);
      }

      *cpus_to_reschedule_mask |= cpu_num_to_mask(thread->curr_cpu);
      break;
    }

    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // Update the weight of the thread blocked in a wait queue. Also
      // handle the race where the thread is no longer in the wait queue
      // but has not yet transitioned to ready.
      state->weight_ = weight;
      if (thread->blocking_wait_queue) {
        wait_queue_priority_changed(thread, original_priority, propagate);
      }
      break;

    default:
      break;
  }
}

void Scheduler::ChangeWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration trace{"sched_change_weight"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d\n", thread->name,
                ToString(thread->state), thread->base_priority, thread->effec_priority,
                thread->inherited_priority);

  if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
    return;
  }

  // TODO(eieio): The rest of the kernel still uses priority so we have to
  // operate in those terms here. Abstract the notion of priority once the
  // deadline scheduler is available and remove this conversion once the
  // kernel uses the abstraction throughout.
  const int original_priority = thread->effec_priority;
  thread->base_priority = priority;
  thread->priority_boost = 0;

  // Adjust the effective priority for inheritance, if necessary.
  if (thread->inherited_priority > thread->base_priority) {
    thread->effec_priority = thread->inherited_priority;
  } else {
    thread->effec_priority = thread->base_priority;
  }

  // Perform the state-specific updates if the effective priority changed.
  if (thread->effec_priority != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(thread->effec_priority),
                       cpus_to_reschedule_mask, PropagatePI::Yes);
  }

  trace.End(original_priority, thread->effec_priority);
}

void Scheduler::InheritWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration trace{"sched_inherit_weight"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d\n", thread->name,
                ToString(thread->state), thread->base_priority, thread->effec_priority,
                thread->inherited_priority);

  const int original_priority = thread->effec_priority;
  thread->inherited_priority = priority;
  thread->priority_boost = 0;

  if (thread->inherited_priority > thread->base_priority) {
    thread->effec_priority = thread->inherited_priority;
  } else {
    thread->effec_priority = thread->base_priority;
  }

  // Perform the state-specific updates if the effective priority changed.
  if (thread->effec_priority != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(thread->effec_priority),
                       cpus_to_reschedule_mask, PropagatePI::No);
  }

  trace.End(original_priority, thread->effec_priority);
}

void Scheduler::TimerTick(SchedTime now) {
  LocalTraceDuration trace{"sched_timer_tick"_stringref};
  thread_preempt_set_pending();
}

// Temporary compatibility with the thread layer.

void sched_init_thread(thread_t* thread, int priority) {
  Scheduler::InitializeThread(thread, priority);
}

void sched_block() { Scheduler::Block(); }

bool sched_unblock(thread_t* thread) { return Scheduler::Unblock(thread); }

bool sched_unblock_list(list_node* list) { return Scheduler::Unblock(list); }

void sched_unblock_idle(thread_t* thread) { Scheduler::UnblockIdle(thread); }

void sched_yield() { Scheduler::Yield(); }

void sched_preempt() { Scheduler::Preempt(); }

void sched_reschedule() { Scheduler::Reschedule(); }

void sched_resched_internal() { Scheduler::RescheduleInternal(); }

void sched_transition_off_cpu(cpu_num_t current_cpu) {
  Scheduler::MigrateUnpinnedThreads(current_cpu);
}

void sched_migrate(thread_t* thread) { Scheduler::Migrate(thread); }

void sched_inherit_priority(thread_t* thread, int priority, bool* local_reschedule,
                            cpu_mask_t* cpus_to_reschedule_mask) {
  Scheduler::InheritWeight(thread, priority, cpus_to_reschedule_mask);

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  if (*cpus_to_reschedule_mask & current_cpu_mask) {
    *local_reschedule = true;
  }
}

void sched_change_priority(thread_t* thread, int priority) {
  cpu_mask_t cpus_to_reschedule_mask = 0;
  Scheduler::ChangeWeight(thread, priority, &cpus_to_reschedule_mask);

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  if (cpus_to_reschedule_mask & current_cpu_mask) {
    Scheduler::Reschedule();
  }
  if (cpus_to_reschedule_mask & ~current_cpu_mask) {
    mp_reschedule(cpus_to_reschedule_mask, 0);
  }
}

// Remap any attempt to set a deadline profile to just setting a very high
// priority.  See comments in legacy_scheduler.cc for details.
void sched_change_deadline(thread_t* t, const zx_sched_deadline_params_t&) {
  sched_change_priority(t, 30);
}

void sched_preempt_timer_tick(zx_time_t now) { Scheduler::TimerTick(SchedTime{now}); }
