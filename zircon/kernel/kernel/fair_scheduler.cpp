// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/fair_scheduler.h>

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/move.h>
#include <lib/ktrace.h>
#include <list.h>
#include <platform.h>
#include <printf.h>
#include <string.h>
#include <target.h>
#include <trace.h>
#include <vm/vm.h>
#include <zircon/types.h>

#include <algorithm>
#include <new>

using ffl::Expression;
using ffl::Fixed;
using ffl::FromRatio;
using ffl::Round;

// Enable/disable ktraces local to this file.
#define LOCAL_KTRACE_ENABLE 0 || WITH_DETAILED_SCHEDULER_TRACING

#define LOCAL_KTRACE(string, args...)                                \
    ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, \
                 KTRACE_STRING_REF(string), ##args)

#define LOCAL_KTRACE_FLOW_BEGIN(string, flow_id)                          \
    ktrace_flow_begin(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, \
                      KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string),    \
                      flow_id)

#define LOCAL_KTRACE_FLOW_END(string, flow_id)                          \
    ktrace_flow_end(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, \
                    KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string),    \
                    flow_id)

#define LOCAL_KTRACE_DURATION                        \
    TraceDuration<TraceEnabled<LOCAL_KTRACE_ENABLE>, \
                  KTRACE_GRP_SCHEDULER, TraceContext::Cpu>

// Enable/disable console traces local to this file.
#define LOCAL_TRACE 0

#define SCHED_LTRACEF(str, args...) LTRACEF("[%u] " str, arch_curr_cpu_num(), ##args)
#define SCHED_TRACEF(str, args...) TRACEF("[%u] " str, arch_curr_cpu_num(), ##args)

namespace {

constexpr SchedWeight kMinWeight = FromRatio(LOWEST_PRIORITY + 1, NUM_PRIORITIES);
constexpr SchedWeight kReciprocalMinWeight = 1 / kMinWeight;

constexpr SchedWeight PriorityToWeight(int priority) {
    return FromRatio(priority, NUM_PRIORITIES);
}
constexpr int WeightToPriority(SchedWeight weight) {
    return Round<int>(kReciprocalMinWeight * weight);
}

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
__NO_SAFESTACK void FinalContextSwitch(thread_t* oldthread,
                                       thread_t* newthread) {
    set_current_thread(newthread);
    arch_context_switch(oldthread, newthread);
}

inline void TraceContextSwitch(const thread_t* current_thread,
                               const thread_t* next_thread, cpu_num_t current_cpu) {
    const uintptr_t raw_current = reinterpret_cast<uintptr_t>(current_thread);
    const uintptr_t raw_next = reinterpret_cast<uintptr_t>(next_thread);
    const uint32_t current = static_cast<uint32_t>(raw_current);
    const uint32_t next = static_cast<uint32_t>(raw_next);
    const uint32_t user_tid = static_cast<uint32_t>(next_thread->user_tid);
    const uint32_t context = current_cpu |
                             (current_thread->state << 8) |
                             (current_thread->base_priority << 16) |
                             (next_thread->base_priority << 24);

    ktrace(TAG_CONTEXT_SWITCH, user_tid, context, current, next);
}

// Returns a sufficiently unique flow id for a thread based on the thread id and
// queue generation count. This flow id cannot be used across enqueues because
// the generation count changes during enqueue.
inline uint64_t FlowIdFromThreadGeneration(const thread_t* thread) {
    const int kRotationBits = 32;
    const uint64_t rotated_tid = (thread->user_tid << kRotationBits) |
                                 (thread->user_tid >> kRotationBits);
    return rotated_tid ^ thread->fair_task_state.generation();
}

} // anonymous namespace

void FairScheduler::Dump() {
    printf("\tweight_total=%#x runnable_tasks=%d vtime=%ld period=%ld\n",
           static_cast<uint32_t>(weight_total_.raw_value()),
           runnable_task_count_,
           virtual_time_.raw_value(),
           scheduling_period_grans_.raw_value());

    if (active_thread_ != nullptr) {
        const FairTaskState* const state = &active_thread_->fair_task_state;
        printf("\t-> name=%s weight=%#x vstart=%ld vfinish=%ld time_slice_ns=%ld\n",
               active_thread_->name,
               static_cast<uint32_t>(state->weight_.raw_value()),
               state->virtual_start_time_.raw_value(),
               state->virtual_finish_time_.raw_value(),
               state->time_slice_ns_.raw_value());
    }

    for (const thread_t& thread : run_queue_) {
        const FairTaskState* const state = &thread.fair_task_state;
        printf("\t   name=%s weight=%#x vstart=%ld vfinish=%ld time_slice_ns=%ld\n",
               thread.name,
               static_cast<uint32_t>(state->weight_.raw_value()),
               state->virtual_start_time_.raw_value(),
               state->virtual_finish_time_.raw_value(),
               state->time_slice_ns_.raw_value());
    }
}

SchedWeight FairScheduler::GetTotalWeight() {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    return weight_total_;
}

size_t FairScheduler::GetRunnableTasks() {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    return static_cast<size_t>(runnable_task_count_);
}

FairScheduler* FairScheduler::Get() {
    return Get(arch_curr_cpu_num());
}

FairScheduler* FairScheduler::Get(cpu_num_t cpu) {
    return &percpu[cpu].fair_runqueue;
}

void FairScheduler::InitializeThread(thread_t* thread, SchedWeight weight) {
    new (&thread->fair_task_state) FairTaskState{weight};
    thread->base_priority = WeightToPriority(weight);
    thread->effec_priority = WeightToPriority(weight);
    thread->inherited_priority = -1;
    thread->priority_boost = 0;
}

thread_t* FairScheduler::DequeueThread() {
    return run_queue_.pop_front();
}

// Selects a thread to run. Performs any necessary maintenanace if the current
// thread is changing, depending on the reason for the change.
thread_t* FairScheduler::EvaluateNextThread(SchedTime now, thread_t* current_thread,
                                            bool timeslice_expired) {
    const bool is_idle = thread_is_idle(current_thread);
    const bool is_active = current_thread->state == THREAD_READY;
    const bool should_migrate = !(current_thread->cpu_affinity &
                                  cpu_num_to_mask(arch_curr_cpu_num()));

    if (is_active && unlikely(should_migrate)) {
        // The current CPU is not in the thread's affinity mask, find a new CPU
        // and move it to that queue.
        current_thread->state = THREAD_READY;
        Remove(current_thread);

        const cpu_num_t target_cpu = FindTargetCpu(current_thread);
        FairScheduler* const target = Get(target_cpu);
        DEBUG_ASSERT(target != this);

        target->Insert(now, current_thread);
        mp_reschedule(cpu_num_to_mask(target_cpu), 0);
    } else if (is_active && likely(!is_idle)) {
        // If the timeslice expired put the current thread back in the runqueue,
        // otherwise continue to run it.
        if (timeslice_expired) {
            UpdateThreadTimeline(current_thread, Placement::Insertion);
            QueueThread(current_thread, Placement::Insertion);
        } else {
            return current_thread;
        }
    } else if (!is_active && likely(!is_idle)) {
        // The current thread is not longer ready, remove its accounting.
        Remove(current_thread);
    }

    // The current thread is no longer running or has returned to the runqueue.
    // Select another thread to run.
    if (likely(!run_queue_.is_empty())) {
        return DequeueThread();
    } else {
        const cpu_num_t current_cpu = arch_curr_cpu_num();
        return &percpu[current_cpu].idle_thread;
    }
}

cpu_num_t FairScheduler::FindTargetCpu(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"find_target: cpu,avail"_stringref};

    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
    const cpu_mask_t last_cpu_mask = cpu_num_to_mask(thread->last_cpu);
    const cpu_mask_t affinity_mask = thread->cpu_affinity;
    const cpu_mask_t active_mask = mp_get_active_mask();
    const cpu_mask_t idle_mask = mp_get_idle_mask();

    // Threads may be created and resumed before the thread init level. Work around
    // an empty active mask by assuming the current cpu is scheduleable.
    const cpu_mask_t available_mask = active_mask != 0 ? affinity_mask & active_mask
                                                       : current_cpu_mask;
    DEBUG_ASSERT_MSG(available_mask != 0,
                     "thread=%s affinity=%#x active=%#x idle=%#x arch_ints_disabled=%d",
                     thread->name, affinity_mask, active_mask, idle_mask, arch_ints_disabled());

    LOCAL_KTRACE("target_mask: online,active", mp_get_online_mask(), active_mask);

    cpu_num_t target_cpu;
    FairScheduler* target_queue;

    // Select an initial target.
    if (last_cpu_mask & available_mask) {
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
        FairScheduler* const candidate_queue = Get(candidate_cpu);

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

void FairScheduler::UpdateTimeline(SchedTime now) {
    LOCAL_KTRACE_DURATION trace{"update_vtime"_stringref};

    const Expression runtime_ns = now - last_update_time_ns_;
    last_update_time_ns_ = now;

    if (weight_total_ > SchedWeight{0}) {
        virtual_time_ += runtime_ns;
    }

    trace.End(Round<uint64_t>(runtime_ns), Round<uint64_t>(virtual_time_));
}

void FairScheduler::RescheduleCommon(SchedTime now) {
    LOCAL_KTRACE_DURATION trace{"reschedule_common"_stringref};

    const cpu_num_t current_cpu = arch_curr_cpu_num();
    thread_t* const current_thread = get_current_thread();
    FairTaskState* const current_state = &current_thread->fair_task_state;

    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT_MSG(current_thread->state != THREAD_RUNNING, "state %d\n", current_thread->state);
    DEBUG_ASSERT(!arch_blocking_disallowed());
    DEBUG_ASSERT_MSG(current_cpu == this_cpu(),
                     "current_cpu=%u this_cpu=%u", current_cpu, this_cpu());

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
    if (weight_total_ > 0 && weight_total_ != scheduled_weight_total_) {
        LOCAL_KTRACE_DURATION trace_adjust_rate{"adjust_rate"_stringref};
        scheduled_weight_total_ = weight_total_;

        const SchedDuration time_slice_ns = CalculateTimeslice(current_thread);
        const bool timeslice_changed = time_slice_ns != current_state->time_slice_ns_;
        const bool timeslice_remaining = total_runtime_ns < time_slice_ns;

        // Update the preemption timer if necessary.
        if (timeslice_changed && timeslice_remaining) {
            const SchedTime absolute_deadline_ns =
                current_thread->last_started_running + time_slice_ns;
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
                  current_thread->name, ToString(current_thread->state),
                  next_thread->name, ToString(next_thread->state),
                  timeslice_expired, run_queue_.is_empty(),
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
        percpu[current_cpu].stats.idle_time += actual_runtime_ns;
    }

    if (thread_is_idle(next_thread) /*|| runnable_task_count_ == 1*/) {
        LOCAL_KTRACE_DURATION trace{"stop_preemption"_stringref};
        SCHED_LTRACEF("Stop preemption timer: current=%s next=%s\n",
                      current_thread->name, next_thread->name);
        next_thread->last_started_running = now.raw_value();
        timer_preempt_cancel();
    } else if (timeslice_expired || next_thread != current_thread) {
        LOCAL_KTRACE_DURATION trace{"start_preemption: now,deadline"_stringref};

        // Re-compute the time slice for the new thread based on the latest state.
        NextThreadTimeslice(next_thread);

        // Update the preemption time based on the time slice.
        FairTaskState* const next_state = &next_thread->fair_task_state;
        const SchedTime absolute_deadline_ns = now + next_state->time_slice_ns_;

        next_thread->last_started_running = now.raw_value();
        start_of_current_time_slice_ns_ = now;
        scheduled_weight_total_ = weight_total_;

        SCHED_LTRACEF("Start preemption timer: current=%s next=%s now=%ld deadline=%ld\n",
                      current_thread->name, next_thread->name, now.raw_value(),
                      absolute_deadline_ns.raw_value());
        timer_preempt_reset(absolute_deadline_ns.raw_value());

        trace.End(Round<uint64_t>(now), Round<uint64_t>(absolute_deadline_ns));

        // Emit a flow end event to match the flow begin event emitted when the
        // thread was enqueued. Emitting in this scope ensures that thread just
        // came from the run queue (and is not the idle thread).
        LOCAL_KTRACE_FLOW_END("sched_latency", FlowIdFromThreadGeneration(next_thread));
    }

    if (next_thread != current_thread) {
        LOCAL_KTRACE("reschedule current: count,slice",
                     runnable_task_count_,
                     Round<uint64_t>(current_thread->fair_task_state.time_slice_ns_));
        LOCAL_KTRACE("reschedule next: wsum,slice",
                     weight_total_.raw_value(),
                     Round<uint64_t>(next_thread->fair_task_state.time_slice_ns_));

        TraceContextSwitch(current_thread, next_thread, current_cpu);

        // Blink the optional debug LEDs on the target.
        target_set_debug_led(0, !thread_is_idle(next_thread));

        SCHED_LTRACEF("current=(%s, flags 0x%#x) next=(%s, flags 0x%#x)\n",
                      current_thread->name, current_thread->flags,
                      next_thread->name, next_thread->flags);

        if (current_thread->aspace != next_thread->aspace) {
            vmm_context_switch(current_thread->aspace, next_thread->aspace);
        }

        CPU_STATS_INC(context_switches);
        FinalContextSwitch(current_thread, next_thread);
    }
}

void FairScheduler::UpdatePeriod() {
    LOCAL_KTRACE_DURATION trace{"update_period"_stringref};

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

    SCHED_LTRACEF("num_tasks=%ld peak_tasks=%ld normal_tasks=%ld period_grans=%ld\n",
                  num_tasks, peak_tasks, normal_tasks, scheduling_period_grans_.raw_value());

    trace.End(Round<uint64_t>(scheduling_period_grans_), num_tasks);
}

SchedDuration FairScheduler::CalculateTimeslice(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"calculate_timeslice: w,wt"_stringref};
    FairTaskState* const state = &thread->fair_task_state;

    // Calculate the relative portion of the scheduling period.
    const Fixed<int64_t, 5> time_slice_grans = scheduling_period_grans_ *
                                               state->weight_ / weight_total_;
    const SchedDuration time_slice_ns = time_slice_grans.Ceiling() *
                                        minimum_granularity_ns_;

    trace.End(state->weight_.raw_value(), weight_total_.raw_value());
    return time_slice_ns;
}

void FairScheduler::NextThreadTimeslice(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"next_timeslice: s,w"_stringref};

    if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
        return;
    }

    FairTaskState* const state = &thread->fair_task_state;
    state->time_slice_ns_ = CalculateTimeslice(thread);
    DEBUG_ASSERT(state->time_slice_ns_ >= minimum_granularity_ns_);

    SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x time_slice_ns=%ld\n",
                  thread->name,
                  static_cast<uint32_t>(weight_total_.raw_value()),
                  static_cast<uint32_t>(state->weight_.raw_value()),
                  state->time_slice_ns_.raw_value());

    trace.End(Round<uint64_t>(state->time_slice_ns_),
              state->weight_.raw_value());
}

void FairScheduler::UpdateThreadTimeline(thread_t* thread, Placement placement) {
    LOCAL_KTRACE_DURATION trace{"update_timeline: vs,vf"_stringref};

    if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
        return;
    }

    FairTaskState* const state = &thread->fair_task_state;

    // Update virtual timeline.
    if (placement == Placement::Insertion) {
        state->virtual_start_time_ = std::max(state->virtual_finish_time_, virtual_time_);
    }

    const SchedDuration scheduling_period_ns = scheduling_period_grans_ *
                                               minimum_granularity_ns_;
    const SchedDuration delta_norm = scheduling_period_ns /
                                     (kReciprocalMinWeight * state->weight_);
    state->virtual_finish_time_ = state->virtual_start_time_ + delta_norm;

    DEBUG_ASSERT_MSG(state->virtual_start_time_ < state->virtual_finish_time_,
                     "vstart=%ld vfinish=%ld delta_norm=%ld\n",
                     state->virtual_start_time_.raw_value(),
                     state->virtual_finish_time_.raw_value(),
                     delta_norm.raw_value());

    SCHED_LTRACEF("name=%s vstart=%ld vfinish=%ld lag=%ld vtime=%ld\n",
                  thread->name,
                  state->virtual_start_time_.raw_value(),
                  state->virtual_finish_time_.raw_value(),
                  state->lag_time_ns_.raw_value(),
                  virtual_time_.raw_value());

    trace.End(Round<uint64_t>(state->virtual_start_time_),
              Round<uint64_t>(state->virtual_finish_time_));
}

void FairScheduler::QueueThread(thread_t* thread, Placement placement) {
    LOCAL_KTRACE_DURATION trace{"queue_thread"_stringref};

    DEBUG_ASSERT(thread->state == THREAD_READY);
    DEBUG_ASSERT(!thread_is_idle(thread));
    SCHED_LTRACEF("QueueThread: thread=%s\n", thread->name);

    // Only update the generation and emit a flow event if this is an insertion.
    // In constrast, an adjustment only changes the queue position due to a
    // weight change and should not perform these actions.
    if (placement == Placement::Insertion) {
        thread->fair_task_state.generation_ = ++generation_count_;
    }

    run_queue_.insert(thread);
    LOCAL_KTRACE("queue_thread");

    if (placement == Placement::Insertion) {
        LOCAL_KTRACE_FLOW_BEGIN("sched_latency", FlowIdFromThreadGeneration(thread));
    }
}

void FairScheduler::Insert(SchedTime now, thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"insert"_stringref};

    DEBUG_ASSERT(thread->state == THREAD_READY);
    DEBUG_ASSERT(!thread_is_idle(thread));

    FairTaskState* const state = &thread->fair_task_state;

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
        //virtual_time_ -= state->lag_time_ns_ / weight_total_;

        UpdateThreadTimeline(thread, Placement::Insertion);
        QueueThread(thread, Placement::Insertion);
    }
}

void FairScheduler::Remove(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"remove"_stringref};

    DEBUG_ASSERT(!thread_is_idle(thread));

    FairTaskState* const state = &thread->fair_task_state;
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
        //virtual_time_ += state->lag_time_ns_ / weight_total_;
        weight_total_ -= state->weight_;
        DEBUG_ASSERT(weight_total_ >= SchedWeight{0});

        SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x lag_time_ns=%ld\n",
                      thread->name,
                      static_cast<uint32_t>(weight_total_.raw_value()),
                      static_cast<uint32_t>(state->weight_.raw_value()),
                      state->lag_time_ns_.raw_value());
    }
}

void FairScheduler::Block() {
    LOCAL_KTRACE_DURATION trace{"sched_block"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* const current_thread = get_current_thread();

    DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

    const SchedTime now = CurrentTime();
    SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

    FairScheduler::Get()->RescheduleCommon(now);
}

bool FairScheduler::Unblock(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"sched_unblock"_stringref};

    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    const SchedTime now = CurrentTime();
    SCHED_LTRACEF("thread=%s now=%ld\n", thread->name, now.raw_value());

    const cpu_num_t target_cpu = FindTargetCpu(thread);
    FairScheduler* const target = Get(target_cpu);

    thread->state = THREAD_READY;
    target->Insert(now, thread);

    if (target_cpu == arch_curr_cpu_num()) {
        return true;
    } else {
        mp_reschedule(cpu_num_to_mask(target_cpu), 0);
        return false;
    }
}

bool FairScheduler::Unblock(list_node* list) {
    LOCAL_KTRACE_DURATION trace{"sched_unblock_list"_stringref};

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
        FairScheduler* const target = Get(target_cpu);

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

void FairScheduler::UnblockIdle(thread_t* thread) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    DEBUG_ASSERT(thread_is_idle(thread));
    DEBUG_ASSERT(thread->cpu_affinity && (thread->cpu_affinity & (thread->cpu_affinity - 1)) == 0);

    SCHED_LTRACEF("thread=%s now=%ld\n", thread->name, current_time());

    thread->state = THREAD_READY;
    thread->curr_cpu = lowest_cpu_set(thread->cpu_affinity);
}

void FairScheduler::Yield() {
    LOCAL_KTRACE_DURATION trace{"sched_yield"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* const current_thread = get_current_thread();
    FairTaskState* const current_state = &current_thread->fair_task_state;
    DEBUG_ASSERT(!thread_is_idle(current_thread));

    const SchedTime now = CurrentTime();
    SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

    // Update the virtual timeline in preparation for snapping the thread's
    // virtual finish time to the current virtual time.
    FairScheduler* const current = Get();
    current->UpdateTimeline(now);

    // Set the time slice to expire now. The thread is re-evaluated with with
    // zero lag against other competing threads and may skip lower priority
    // threads with similar arrival times.
    current_thread->state = THREAD_READY;
    current_state->virtual_finish_time_ = current->virtual_time_;
    current_state->time_slice_ns_ = now - current->start_of_current_time_slice_ns_;
    DEBUG_ASSERT(current_state->time_slice_ns_ >= 0);

    current->RescheduleCommon(now);
}

void FairScheduler::Preempt() {
    LOCAL_KTRACE_DURATION trace{"sched_preempt"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    thread_t* current_thread = get_current_thread();
    const cpu_num_t current_cpu = arch_curr_cpu_num();

    DEBUG_ASSERT(current_thread->curr_cpu == current_cpu);
    DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);

    const SchedTime now = CurrentTime();
    SCHED_LTRACEF("current=%s now=%ld\n", current_thread->name, now.raw_value());

    current_thread->state = THREAD_READY;
    Get()->RescheduleCommon(now);
}

void FairScheduler::Reschedule() {
    LOCAL_KTRACE_DURATION trace{"sched_reschedule"_stringref};

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
    Get()->RescheduleCommon(now);
}

void FairScheduler::RescheduleInternal() {
    Get()->RescheduleCommon(CurrentTime());
}

void FairScheduler::Migrate(thread_t* thread) {
    LOCAL_KTRACE_DURATION trace{"sched_migrate"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    cpu_mask_t cpus_to_reschedule_mask = 0;

    if (thread->state == THREAD_RUNNING) {
        const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
        if (!(thread->cpu_affinity & thread_cpu_mask)) {
            // Mark the CPU the thread is running on for reschedule. The
            // scheduler on that CPU will take care of the actual migration.
            cpus_to_reschedule_mask |= thread_cpu_mask;
        }
    } else if (thread->state == THREAD_READY) {
        const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
        if (!(thread->cpu_affinity & thread_cpu_mask)) {
            FairScheduler* current = Get(thread->curr_cpu);

            DEBUG_ASSERT(thread->fair_task_state.InQueue());
            current->run_queue_.erase(*thread);
            current->Remove(thread);

            const cpu_num_t target_cpu = FindTargetCpu(thread);
            FairScheduler* const target = Get(target_cpu);
            target->Insert(CurrentTime(), thread);

            cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
        }
    }

    if (cpus_to_reschedule_mask) {
        mp_reschedule(cpus_to_reschedule_mask, 0);
    }

    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
    if (cpus_to_reschedule_mask & current_cpu_mask) {
        Reschedule();
    }
}

void FairScheduler::MigrateUnpinnedThreads(cpu_num_t current_cpu) {
    LOCAL_KTRACE_DURATION trace{"sched_migrate_unpinned"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    DEBUG_ASSERT(current_cpu == arch_curr_cpu_num());

    // Prevent this CPU from being selected as a target for scheduling threads.
    mp_set_curr_cpu_active(false);

    const SchedTime now = CurrentTime();
    FairScheduler* const current = Get(current_cpu);
    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);

    RunQueue pinned_threads;
    cpu_mask_t cpus_to_reschedule_mask = 0;
    while (!current->run_queue_.is_empty()) {
        thread_t* const thread = current->DequeueThread();

        if (thread->cpu_affinity == current_cpu_mask) {
            // Keep track of threads pinned to this CPU.
            pinned_threads.insert(thread);
        } else {
            // Move unpinned threads to another available CPU.
            current->Remove(thread);

            const cpu_num_t target_cpu = FindTargetCpu(thread);
            FairScheduler* const target = Get(target_cpu);
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

void FairScheduler::UpdateWeightCommon(thread_t* thread,
                                       int original_priority,
                                       SchedWeight weight,
                                       cpu_mask_t* cpus_to_reschedule_mask,
                                       PropagatePI propagate) {
    FairTaskState* const state = &thread->fair_task_state;

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
        FairScheduler* const current = Get(thread->curr_cpu);

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

void FairScheduler::ChangeWeight(thread_t* thread, SchedWeight weight,
                                 cpu_mask_t* cpus_to_reschedule_mask) {
    LOCAL_KTRACE_DURATION trace{"sched_change_weight"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d "
                  "current_weight=%d new_weight=%d\n",
                  thread->name, ToString(thread->state),
                  thread->base_priority, thread->effec_priority,
                  thread->inherited_priority,
                  WeightToPriority(thread->fair_task_state.weight()),
                  WeightToPriority(weight));

    if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
        return;
    }

    // TODO(eieio): The rest of the kernel still uses priority so we have to
    // operate in those terms here. Abstract the notion of priority once the
    // deadline scheduler is available and remove this conversion once the
    // kernel uses the abstraction throughout.
    const int original_priority = thread->effec_priority;
    thread->base_priority = WeightToPriority(weight);
    thread->priority_boost = 0;

    // Adjust the effective priority for inheritance, if necessary.
    if (thread->inherited_priority > thread->base_priority) {
        thread->effec_priority = thread->inherited_priority;
        weight = PriorityToWeight(thread->effec_priority);
    } else {
        thread->effec_priority = thread->base_priority;
    }

    // Perform the state-specific updates if the effective priority changed.
    if (thread->effec_priority != original_priority) {
        UpdateWeightCommon(thread, original_priority, weight,
                           cpus_to_reschedule_mask,
                           PropagatePI::Yes);
    }

    trace.End(original_priority, thread->effec_priority);
}

void FairScheduler::InheritWeight(thread_t* thread, SchedWeight weight,
                                  cpu_mask_t* cpus_to_reschedule_mask) {
    LOCAL_KTRACE_DURATION trace{"sched_inherit_weight"_stringref};

    DEBUG_ASSERT(spin_lock_held(&thread_lock));
    SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d "
                  "current_weight=%d new_weight=%d\n",
                  thread->name, ToString(thread->state),
                  thread->base_priority, thread->effec_priority,
                  thread->inherited_priority,
                  WeightToPriority(thread->fair_task_state.weight()),
                  WeightToPriority(weight));

    const int priority = WeightToPriority(weight);
    const int original_priority = thread->effec_priority;
    thread->inherited_priority = priority;
    thread->priority_boost = 0;

    if (thread->inherited_priority > thread->base_priority) {
        thread->effec_priority = thread->inherited_priority;
    } else {
        thread->effec_priority = thread->base_priority;
        weight = PriorityToWeight(thread->effec_priority);
    }

    // Perform the state-specific updates if the effective priority changed.
    if (thread->effec_priority != original_priority) {
        UpdateWeightCommon(thread, original_priority, weight,
                           cpus_to_reschedule_mask,
                           PropagatePI::No);
    }

    trace.End(original_priority, thread->effec_priority);
}

void FairScheduler::TimerTick(SchedTime now) {
    LOCAL_KTRACE_DURATION trace{"sched_timer_tick"_stringref};
    thread_preempt_set_pending();
}

// Temporary compatibility with the thread layer.

void sched_init_thread(thread_t* thread, int priority) {
    FairScheduler::InitializeThread(thread, PriorityToWeight(priority));
}

void sched_block() {
    FairScheduler::Block();
}

bool sched_unblock(thread_t* thread) {
    return FairScheduler::Unblock(thread);
}

bool sched_unblock_list(list_node* list) {
    return FairScheduler::Unblock(list);
}

void sched_unblock_idle(thread_t* thread) {
    FairScheduler::UnblockIdle(thread);
}

void sched_yield() {
    FairScheduler::Yield();
}

void sched_preempt() {
    FairScheduler::Preempt();
}

void sched_reschedule() {
    FairScheduler::Reschedule();
}

void sched_resched_internal() {
    FairScheduler::RescheduleInternal();
}

void sched_transition_off_cpu(cpu_num_t current_cpu) {
    FairScheduler::MigrateUnpinnedThreads(current_cpu);
}

void sched_migrate(thread_t* thread) {
    FairScheduler::Migrate(thread);
}

void sched_inherit_priority(thread_t* thread, int priority,
                            bool* local_reschedule,
                            cpu_mask_t* cpus_to_reschedule_mask) {
    FairScheduler::InheritWeight(thread, PriorityToWeight(priority),
                                 cpus_to_reschedule_mask);

    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
    if (*cpus_to_reschedule_mask & current_cpu_mask) {
        *local_reschedule = true;
    }
}

void sched_change_priority(thread_t* thread, int priority) {
    cpu_mask_t cpus_to_reschedule_mask = 0;
    FairScheduler::ChangeWeight(thread, PriorityToWeight(priority),
                                &cpus_to_reschedule_mask);

    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
    if (cpus_to_reschedule_mask & current_cpu_mask) {
        FairScheduler::Reschedule();
    }
    if (cpus_to_reschedule_mask & ~current_cpu_mask) {
        mp_reschedule(cpus_to_reschedule_mask, 0);
    }
}

void sched_preempt_timer_tick(zx_time_t now) {
    FairScheduler::TimerTick(SchedTime{now});
}

void sched_init_early() {
    for (cpu_num_t cpu = 0; cpu < SMP_MAX_CPUS; cpu++) {
        percpu[cpu].fair_runqueue.this_cpu_ = cpu;
    }
}
