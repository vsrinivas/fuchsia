// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/scheduler.h"

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/ktrace.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <new>

#include <arch/ops.h>
#include <ffl/string.h>
#include <kernel/auto_lock.h>
#include <kernel/cpu.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/scheduler_state.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/algorithm.h>
#include <ktl/forward.h>
#include <ktl/move.h>
#include <ktl/pair.h>
#include <ktl/span.h>
#include <object/thread_dispatcher.h>
#include <vm/vm.h>

#include <ktl/enforce.h>

using ffl::FromRatio;
using ffl::Round;

// Determines which subset of tracers are enabled when detailed tracing is
// enabled. When queue tracing is enabled the minimum trace level is
// KTRACE_COMMON.
#define LOCAL_KTRACE_LEVEL                                                         \
  (SCHEDULER_TRACING_LEVEL == 0 && SCHEDULER_QUEUE_TRACING_ENABLED ? KTRACE_COMMON \
                                                                   : SCHEDULER_TRACING_LEVEL)

// The tracing levels used in this compilation unit.
#define KTRACE_COMMON 1
#define KTRACE_FLOW 2
#define KTRACE_COUNTER 3
#define KTRACE_DETAILED 4

// Evaluates to true if tracing is enabled for the given level.
#define LOCAL_KTRACE_LEVEL_ENABLED(level) ((LOCAL_KTRACE_LEVEL) >= (level))

#define LOCAL_KTRACE(level, string, args...)                                     \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
               KTRACE_STRING_REF(string), ##args)

#define LOCAL_KTRACE_FLOW_BEGIN(level, string, flow_id, args...)                      \
  ktrace_flow_begin(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
                    KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string), flow_id, ##args)

#define LOCAL_KTRACE_FLOW_END(level, string, flow_id, args...)                      \
  ktrace_flow_end(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
                  KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string), flow_id, ##args)

#define LOCAL_KTRACE_FLOW_STEP(level, string, flow_id, args...)                      \
  ktrace_flow_step(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
                   KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string), flow_id, ##args)

#define LOCAL_KTRACE_COUNTER(level, string, value, args...)                           \
  ktrace_counter(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, KTRACE_GRP_SCHEDULER, \
                 KTRACE_STRING_REF(string), value, ##args)

template <size_t level>
using LocalTraceDuration = TraceDuration<TraceEnabled<LOCAL_KTRACE_LEVEL_ENABLED(level)>,
                                         KTRACE_GRP_SCHEDULER, TraceContext::Cpu>;

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

// Writes a context switch record to the ktrace buffer. This is always enabled
// so that user mode tracing can track which threads are running.
inline void TraceContextSwitch(const Thread* current_thread, const Thread* next_thread,
                               cpu_num_t current_cpu) {
  if (unlikely(ktrace_tag_enabled(TAG_CONTEXT_SWITCH))) {
    fxt_context_switch(TAG_CONTEXT_SWITCH, current_ticks(), static_cast<uint8_t>(current_cpu),
                       current_thread->state(),
                       fxt::ThreadRef(current_thread->pid(), current_thread->tid()),
                       fxt::ThreadRef(next_thread->pid(), next_thread->tid()),
                       static_cast<uint8_t>(current_thread->scheduler_state().base_priority()),
                       static_cast<uint8_t>(next_thread->scheduler_state().base_priority()));
  }
}

// Returns true if the given thread is fair scheduled.
inline bool IsFairThread(const Thread* thread) {
  return thread->scheduler_state().discipline() == SchedDiscipline::Fair;
}

// Returns true if the given thread is deadline scheduled.
inline bool IsDeadlineThread(const Thread* thread) {
  return thread->scheduler_state().discipline() == SchedDiscipline::Deadline;
}

// Returns true if the given thread's time slice is adjustable under changes to
// the fair scheduler demand on the CPU.
inline bool IsThreadAdjustable(const Thread* thread) {
  // Checking the thread state avoids unnecessary adjustments on a thread that
  // is no longer competing.
  return !thread->IsIdle() && IsFairThread(thread) && thread->state() == THREAD_READY;
}

// Returns a delta value to additively update a predictor. Compares the given
// sample to the current value of the predictor and returns a delta such that
// the predictor either exponentially peaks or decays toward the sample. The
// rate of decay depends on the alpha parameter, while the rate of peaking
// depends on the beta parameter. The predictor is not permitted to become
// negative.
//
// A single-rate exponential moving average is updated as follows:
//
//   Sn = Sn-1 + a * (Yn - Sn-1)
//
// This function updates the exponential moving average using potentially
// different rates for peak and decay:
//
//   D  = Yn - Sn-1
//        [ Sn-1 + a * D      if D < 0
//   Sn = [
//        [ Sn-1 + b * D      if D >= 0
//
template <typename T, typename Alpha, typename Beta>
constexpr T PeakDecayDelta(T value, T sample, Alpha alpha, Beta beta) {
  const T delta = sample - value;
  return ktl::max<T>(delta >= 0 ? T{beta * delta} : T{alpha * delta}, -value);
}

}  // anonymous namespace

// Scales the given value up by the reciprocal of the CPU performance scale.
template <typename T>
inline T Scheduler::ScaleUp(T value) const {
  return value * performance_scale_reciprocal();
}

// Scales the given value down by the CPU performance scale.
template <typename T>
inline T Scheduler::ScaleDown(T value) const {
  return value * performance_scale();
}

// Returns a new flow id when flow tracing is enabled, zero otherwise.
inline uint64_t Scheduler::NextFlowId() {
  if constexpr (LOCAL_KTRACE_LEVEL >= KTRACE_FLOW) {
    return next_flow_id_.fetch_add(1);
  }
  return 0;
}

// Records details about the threads entering/exiting the run queues for various
// CPUs, as well as which task on each CPU is currently active. These events are
// used for trace analysis to compute statistics about overall utilization,
// taking CPU affinity into account.
inline void Scheduler::TraceThreadQueueEvent(StringRef* name, Thread* thread) const {
  // Traces marking the end of a queue/dequeue operation have arguments encoded
  // as follows:
  //
  // arg0[ 0..64] : TID
  //
  // arg1[ 0..15] : CPU availability mask.
  // arg1[16..19] : CPU_ID of the affected queue.
  // arg1[20..27] : Number of runnable tasks on this CPU after the queue event.
  // arg1[28..28] : 1 == fair, 0 == deadline
  // arg1[29..29] : 1 == eligible, 0 == ineligible
  //
  if constexpr (SCHEDULER_QUEUE_TRACING_ENABLED) {
    const zx_time_t now = current_time();  // TODO(johngro): plumb this in from above
    const bool fair = IsFairThread(thread);
    const bool eligible = fair || (thread->scheduler_state().start_time_ <= now);
    const size_t cnt = fair_run_queue_.size() + deadline_run_queue_.size() +
                       ((active_thread_ && !active_thread_->IsIdle()) ? 1 : 0);

    const uint64_t arg0 = thread->IsIdle() ? 0 : thread->tid();
    const uint64_t arg1 =
        (thread->scheduler_state().GetEffectiveCpuMask(mp_get_active_mask()) & 0xFFFF) |
        (ktl::clamp<uint64_t>(this_cpu_, 0, 0xF) << 16) |
        (ktl::clamp<uint64_t>(cnt, 0, 0xFF) << 20) | ((fair ? 1 : 0) << 28) |
        ((eligible ? 1 : 0) << 29);

    ktrace_probe(TraceAlways, TraceContext::Cpu, name, arg0, arg1);
  }
}

// Updates the total expected runtime estimator with the given delta. The
// exported value is scaled by the relative performance factor of the CPU to
// account for performance differences in the estimate.
inline void Scheduler::UpdateTotalExpectedRuntime(SchedDuration delta_ns) {
  total_expected_runtime_ns_ += delta_ns;
  DEBUG_ASSERT(total_expected_runtime_ns_ >= 0);
  const SchedDuration scaled_ns = ScaleUp(total_expected_runtime_ns_);
  exported_total_expected_runtime_ns_ = scaled_ns;
  LOCAL_KTRACE_COUNTER(KTRACE_COUNTER, "Est Load", scaled_ns.raw_value(), this_cpu());
}

// Updates the total deadline utilization estimator with the given delta. The
// exported value is scaled by the relative performance factor of the CPU to
// account for performance differences in the estimate.
inline void Scheduler::UpdateTotalDeadlineUtilization(SchedUtilization delta) {
  total_deadline_utilization_ += delta;
  DEBUG_ASSERT(total_deadline_utilization_ >= 0);
  const SchedUtilization scaled = ScaleUp(total_deadline_utilization_);
  exported_total_deadline_utilization_ = scaled;
  LOCAL_KTRACE_COUNTER(KTRACE_COUNTER, "Est Util", Round<uint64_t>(scaled * 10000), this_cpu());
}

inline void Scheduler::TraceTotalRunnableThreads() const {
  LOCAL_KTRACE_COUNTER(KTRACE_COUNTER, "Run-Q Len",
                       runnable_fair_task_count_ + runnable_deadline_task_count_, this_cpu());
}

void Scheduler::Dump(FILE* output_target) {
  // TODO(eieio): HACK! Take the thread lock here to prevent the IO path from
  // attempting to acquire the thread lock after the queue lock. Figure out a
  // similar strategy to prevent lock order inversion with whichever lock
  // protects the IO path once the thread lock is removed.
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  DumpThreadLocked(output_target);
}

void Scheduler::DumpThreadLocked(FILE* output_target) {
  Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&queue_lock_, SOURCE_TAG};

  fprintf(output_target,
          "\ttweight=%s nfair=%d ndeadline=%d vtime=%" PRId64 " period=%" PRId64 " tema=%" PRId64
          " tutil=%s\n",
          Format(weight_total_).c_str(), runnable_fair_task_count_, runnable_deadline_task_count_,
          virtual_time_.raw_value(), scheduling_period_grans_.raw_value(),
          total_expected_runtime_ns_.raw_value(), Format(total_deadline_utilization_).c_str());

  if (active_thread_ != nullptr) {
    const SchedulerState& state = active_thread_->scheduler_state();
    if (IsFairThread(active_thread_)) {
      fprintf(output_target,
              "\t-> name=%s weight=%s start=%" PRId64 " finish=%" PRId64 " ts=%" PRId64
              " ema=%" PRId64 "\n",
              active_thread_->name(), Format(state.fair_.weight).c_str(),
              state.start_time_.raw_value(), state.finish_time_.raw_value(),
              state.time_slice_ns_.raw_value(), state.expected_runtime_ns_.raw_value());
    } else {
      fprintf(output_target,
              "\t-> name=%s deadline=(%" PRId64 ", %" PRId64 ") start=%" PRId64 " finish=%" PRId64
              " ts=%" PRId64 " ema=%" PRId64 "\n",
              active_thread_->name(), state.deadline_.capacity_ns.raw_value(),
              state.deadline_.deadline_ns.raw_value(), state.start_time_.raw_value(),
              state.finish_time_.raw_value(), state.time_slice_ns_.raw_value(),
              state.expected_runtime_ns_.raw_value());
    }
  }

  for (const Thread& thread : deadline_run_queue_) {
    const SchedulerState& state = thread.scheduler_state();
    fprintf(output_target,
            "\t   name=%s deadline=(%" PRId64 ", %" PRId64 ") start=%" PRId64 " finish=%" PRId64
            " ts=%" PRId64 " ema=%" PRId64 "\n",
            thread.name(), state.deadline_.capacity_ns.raw_value(),
            state.deadline_.deadline_ns.raw_value(), state.start_time_.raw_value(),
            state.finish_time_.raw_value(), state.time_slice_ns_.raw_value(),
            state.expected_runtime_ns_.raw_value());
  }

  for (const Thread& thread : fair_run_queue_) {
    const SchedulerState& state = thread.scheduler_state();
    fprintf(output_target,
            "\t   name=%s weight=%s start=%" PRId64 " finish=%" PRId64 " ts=%" PRId64
            " ema=%" PRId64 "\n",
            thread.name(), Format(state.fair_.weight).c_str(), state.start_time_.raw_value(),
            state.finish_time_.raw_value(), state.time_slice_ns_.raw_value(),
            state.expected_runtime_ns_.raw_value());
  }
}

SchedWeight Scheduler::GetTotalWeight() const {
  Guard<MonitoredSpinLock, IrqSave> guard{&queue_lock_, SOURCE_TAG};
  return weight_total_;
}

size_t Scheduler::GetRunnableTasks() const {
  Guard<MonitoredSpinLock, IrqSave> guard{&queue_lock_, SOURCE_TAG};
  const int64_t total_runnable_tasks = runnable_fair_task_count_ + runnable_deadline_task_count_;
  return static_cast<size_t>(total_runnable_tasks);
}

// Performs an augmented binary search for the task with the earliest finish
// time that also has a start time equal to or later than the given eligible
// time. An optional predicate may be supplied to filter candidates based on
// additional conditions.
//
// The tree is ordered by start time and is augmented by maintaining an
// additional invariant: each task node in the tree stores the minimum finish
// time of its descendents, including itself, in addition to its own start and
// finish time. The combination of these three values permits traversinng the
// tree along a perfect partition of minimum finish times with eligible start
// times.
//
// See fbl/wavl_tree_best_node_observer.h for an explanation of how the
// augmented invariant is maintained.
Thread* Scheduler::FindEarliestEligibleThread(RunQueue* run_queue, SchedTime eligible_time) {
  return FindEarliestEligibleThread(run_queue, eligible_time, [](const auto iter) { return true; });
}
template <typename Predicate>
Thread* Scheduler::FindEarliestEligibleThread(RunQueue* run_queue, SchedTime eligible_time,
                                              Predicate&& predicate) {
  // Early out if there is no eligible thread.
  if (run_queue->is_empty() || run_queue->front().scheduler_state().start_time_ > eligible_time) {
    return nullptr;
  }

  // Deduces either Predicate& or const Predicate&, preserving the const
  // qualification of the predicate.
  decltype(auto) accept = ktl::forward<Predicate>(predicate);

  auto node = run_queue->root();
  auto subtree = run_queue->end();
  auto path = run_queue->end();

  // Descend the tree, with |node| following the path from the root to a leaf,
  // such that the path partitions the tree into two parts: the nodes on the
  // left represent eligible tasks, while the nodes on the right represent tasks
  // that are not eligible. Eligible tasks are both in the left partition and
  // along the search path, tracked by |path|.
  while (node) {
    if (node->scheduler_state().start_time_ <= eligible_time) {
      if (!path || path->scheduler_state().finish_time_ > node->scheduler_state().finish_time_) {
        path = node;
      }

      if (auto left = node.left();
          !subtree || (left && subtree->scheduler_state().min_finish_time_ >
                                   left->scheduler_state().min_finish_time_)) {
        subtree = left;
      }

      node = node.right();
    } else {
      node = node.left();
    }
  }

  if (!subtree) {
    return path && accept(path) ? path.CopyPointer() : nullptr;
  }
  if (subtree->scheduler_state().min_finish_time_ >= path->scheduler_state().finish_time_ &&
      accept(path)) {
    return path.CopyPointer();
  }

  // Find the node with the earliest finish time among the decendents of the
  // subtree with the smallest minimum finish time.
  node = subtree;
  do {
    if (subtree->scheduler_state().min_finish_time_ == node->scheduler_state().finish_time_ &&
        accept(node)) {
      return node.CopyPointer();
    }

    if (auto left = node.left(); left && node->scheduler_state().min_finish_time_ ==
                                             left->scheduler_state().min_finish_time_) {
      node = left;
    } else {
      node = node.right();
    }
  } while (node);

  return nullptr;
}

Scheduler* Scheduler::Get() { return Get(arch_curr_cpu_num()); }

Scheduler* Scheduler::Get(cpu_num_t cpu) { return &percpu::Get(cpu).scheduler; }

void Scheduler::InitializeThread(Thread* thread, int priority) {
  new (&thread->scheduler_state()) SchedulerState{PriorityToWeight(priority)};
  thread->scheduler_state().base_priority_ = priority;
  thread->scheduler_state().effective_priority_ = priority;
  thread->scheduler_state().inherited_priority_ = -1;
  thread->scheduler_state().expected_runtime_ns_ = kDefaultMinimumGranularity;
}

void Scheduler::InitializeThread(Thread* thread, const zx_sched_deadline_params_t& params) {
  new (&thread->scheduler_state()) SchedulerState{params};
  // Set the numeric priority of the deadline task to the highest as a temporary
  // workaround for the rest of the kernel not knowing about deadlines. This
  // will cause deadline tasks to exert maximum fair scheduler pressure on fair
  // tasks during PI interactions.
  // TODO(eieio): Fix this with an abstraction that the higher layers can use
  // to express priority / deadline more abstractly for PI and etc...
  thread->scheduler_state().base_priority_ = HIGHEST_PRIORITY;
  thread->scheduler_state().effective_priority_ = HIGHEST_PRIORITY;
  thread->scheduler_state().inherited_priority_ = -1;
  thread->scheduler_state().expected_runtime_ns_ = SchedDuration{params.capacity};
}

// Initialize the first thread to run on the current CPU.  Called from
// thread_construct_first, this method will initialize the thread's scheduler
// state, then mark the thread as being "active" in its cpu's scheduler.
void Scheduler::InitializeFirstThread(Thread* thread) {
  cpu_num_t current_cpu = arch_curr_cpu_num();

  // Construct our scheduler state and assign a "priority"
  InitializeThread(thread, HIGHEST_PRIORITY);

  // Fill out other details about the thread, making sure to assign it to the
  // current CPU with hard affinity.
  SchedulerState& ss = thread->scheduler_state();
  ss.state_ = THREAD_RUNNING;
  ss.curr_cpu_ = current_cpu;
  ss.last_cpu_ = current_cpu;
  ss.next_cpu_ = INVALID_CPU;
  ss.hard_affinity_ = cpu_num_to_mask(current_cpu);

  // Finally, make sure that the thread is the active thread for the scheduler,
  // and that the weight_total bookkeeping is accurate.
  Scheduler* sched = Get(current_cpu);
  {
    Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&sched->queue_lock_, SOURCE_TAG};
    ss.active_ = true;
    sched->active_thread_ = thread;
    sched->weight_total_ = ss.fair_.weight;
    sched->runnable_fair_task_count_++;
    sched->UpdateTotalExpectedRuntime(ss.expected_runtime_ns_);
  }
}

// Remove the impact of a CPUs first thread from the scheduler's bookkeeping.
//
// During initial startup, threads are not _really_ being scheduled, yet they
// can still do things like obtain locks and block, resulting in profile
// inheritance.  In order to hold the scheduler's bookkeeping invariants, we
// assign these threads a fair weight, and include it in the total fair weight
// tracked by the scheduler instance.  When the thread either becomes the idle
// thread (as the boot CPU first thread does), or exits (as secondary CPU first
// threads do), it is important that we remove this weight from the total
// bookkeeping.  However, this is not as simple as just changing the thread's
// weight via ChangeWeight, as idle threads are special cases who contribute no
// weight to the total.
//
// So, this small method simply fixes up the bookkeeping before allowing the
// thread to move on to become the idle thread (boot CPU), or simply exiting
// (secondary CPU).
void Scheduler::RemoveFirstThread(Thread* thread) {
  cpu_num_t current_cpu = arch_curr_cpu_num();
  Scheduler* sched = Get(current_cpu);
  SchedulerState& ss = thread->scheduler_state();

  // Since this is becoming an idle thread, it must have been one of the CPU's
  // first threads.  It should already be bound to this core with hard affinity.
  // Assert this.
  DEBUG_ASSERT(ss.last_cpu_ == current_cpu);
  DEBUG_ASSERT(ss.curr_cpu_ == current_cpu);
  DEBUG_ASSERT(ss.hard_affinity_ == cpu_num_to_mask(current_cpu));

  {
    Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&sched->queue_lock_, SOURCE_TAG};

    // We are becoming the idle thread.  We should currently be running with a
    // fair (not deadline) profile, and we should not be holding any locks
    // (therefore, we should not be inheriting any profile pressure).
    DEBUG_ASSERT(ss.discipline_ == SchedDiscipline::Fair);
    DEBUG_ASSERT(ss.inherited_priority_ = -1);

    // We should also be the currently active thread on this core, but no
    // longer.  We are about to either exit, or "UnblockIdle".
    DEBUG_ASSERT(sched->active_thread_ == thread);
    DEBUG_ASSERT(sched->runnable_fair_task_count_ > 0);
    ss.active_ = false;
    sched->active_thread_ = nullptr;
    sched->weight_total_ -= ss.fair_.weight;
    sched->runnable_fair_task_count_--;
    sched->UpdateTotalExpectedRuntime(-ss.expected_runtime_ns_);

    ss.fair_.weight = PriorityToWeight(IDLE_PRIORITY);
    ss.base_priority_ = IDLE_PRIORITY;
    ss.effective_priority_ = IDLE_PRIORITY;
  }
}

// Removes the thread at the head of the first eligible run queue. If there is
// an eligible deadline thread, it takes precedence over available fair
// threads. If there is no eligible work, attempt to steal work from other busy
// CPUs.
Thread* Scheduler::DequeueThread(SchedTime now, Guard<MonitoredSpinLock, NoIrqSave>& queue_guard) {
  if (IsDeadlineThreadEligible(now)) {
    return DequeueDeadlineThread(now);
  }
  if (likely(!fair_run_queue_.is_empty())) {
    return DequeueFairThread();
  }

  // Release the queue lock while attempting to seal work, leaving IRQs disabled.
  Thread* thread;
  queue_guard.CallUnlocked([&] { thread = StealWork(now); });

  return thread != nullptr ? thread : &percpu::Get(this_cpu()).idle_thread;
}

// Attempts to steal work from other busy CPUs and move it to the local run
// queues. Returns a pointer to the stolen thread that is now associated with
// the local Scheduler instance, or nullptr is no work was stolen.
Thread* Scheduler::StealWork(SchedTime now) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"steal_work"_stringref};

  const cpu_num_t current_cpu = this_cpu();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  const cpu_mask_t active_cpu_mask = mp_get_active_mask();

  // Returns true if the given thread can run on this CPU.
  const auto check_affinity = [current_cpu_mask, active_cpu_mask](const Thread& thread) -> bool {
    return current_cpu_mask & thread.scheduler_state().GetEffectiveCpuMask(active_cpu_mask);
  };

  Thread* thread = nullptr;
  const CpuSearchSet& search_set = percpu::Get(current_cpu).search_set;
  for (const auto& entry : search_set.const_iterator()) {
    if (entry.cpu != current_cpu && active_cpu_mask & cpu_num_to_mask(entry.cpu)) {
      Scheduler* const queue = Get(entry.cpu);

      // Only steal across clusters if the target is above the load threshold.
      if (cluster() != entry.cluster &&
          queue->predicted_queue_time_ns() <= kInterClusterThreshold) {
        continue;
      }

      Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&queue->queue_lock_, SOURCE_TAG};

      // Returns true if the given thread in the run queue meets the criteria to
      // run on this CPU.
      const auto deadline_predicate = [this, check_affinity](const auto iter) {
        const SchedulerState& state = iter->scheduler_state();
        const SchedUtilization scaled_utilization = ScaleUp(state.deadline_.utilization);
        const bool is_scheduleable = scaled_utilization <= kThreadUtilizationMax;
        return check_affinity(*iter) && is_scheduleable && !iter->has_migrate_fn();
      };

      // Attempt to find a deadline thread that can run on this CPU.
      thread =
          queue->FindEarliestEligibleThread(&queue->deadline_run_queue_, now, deadline_predicate);
      if (thread != nullptr) {
        DEBUG_ASSERT(!thread->has_migrate_fn());
        DEBUG_ASSERT(check_affinity(*thread));
        queue->deadline_run_queue_.erase(*thread);
        queue->Remove(thread);
        queue->TraceThreadQueueEvent("tqe_deque_steal_work"_stringref, thread);
        break;
      }

      // Returns true if the given thread in the run queue meets the criteria to
      // run on this CPU.
      const auto fair_predicate = [check_affinity](const auto iter) {
        return check_affinity(*iter) && !iter->has_migrate_fn();
      };

      // TODO(eieio): Revisit the eligibility time parameter if/when moving to WF2Q.
      queue->UpdateTimeline(now);
      SchedTime eligible_time = queue->virtual_time_;
      if (!queue->fair_run_queue_.is_empty()) {
        const auto& earliest_thread = queue->fair_run_queue_.front();
        const auto earliest_start = earliest_thread.scheduler_state().start_time_;
        eligible_time = ktl::max(eligible_time, earliest_start);
      }
      thread =
          queue->FindEarliestEligibleThread(&queue->fair_run_queue_, eligible_time, fair_predicate);
      if (thread != nullptr) {
        DEBUG_ASSERT(!thread->has_migrate_fn());
        DEBUG_ASSERT(check_affinity(*thread));
        queue->fair_run_queue_.erase(*thread);
        queue->Remove(thread);
        queue->TraceThreadQueueEvent("tqe_deque_steal_work"_stringref, thread);
        break;
      }
    }
  }

  if (thread) {
    // Associate the thread with this Scheduler, but don't enqueue it. It
    // will run immediately on this CPU as if dequeued from a local queue.
    Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&queue_lock_, SOURCE_TAG};
    Insert(now, thread, Placement::Association);
  }
  return thread;
}

// Dequeues the eligible thread with the earliest virtual finish time. The
// caller must ensure that there is at least one thread in the queue.
Thread* Scheduler::DequeueFairThread() {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_fair_thread"_stringref};

  // Snap the virtual clock to the earliest start time.
  const auto& earliest_thread = fair_run_queue_.front();
  const auto earliest_start = earliest_thread.scheduler_state().start_time_;
  const SchedTime eligible_time = ktl::max(virtual_time_, earliest_start);

  // Find the eligible thread with the earliest virtual finish time.
  // Note: Currently, fair tasks are always eligible when added to the run
  // queue, such that this search is equivalent to taking the front element of
  // a tree sorted by finish time, instead of start time. However, when moving
  // to the WF2Q algorithm, eligibility becomes a factor. Using the eligibility
  // query now prepares for migrating the algorithm and also avoids having two
  // different template instantiations of fbl::WAVLTree to support the fair and
  // deadline disciplines.
  Thread* const eligible_thread = FindEarliestEligibleThread(&fair_run_queue_, eligible_time);
  DEBUG_ASSERT_MSG(eligible_thread != nullptr,
                   "virtual_time=%" PRId64 ", eligible_time=%" PRId64 " , start_time=%" PRId64
                   ", finish_time=%" PRId64 ", min_finish_time=%" PRId64 "!",
                   virtual_time_.raw_value(), eligible_time.raw_value(),
                   earliest_thread.scheduler_state().start_time_.raw_value(),
                   earliest_thread.scheduler_state().finish_time_.raw_value(),
                   earliest_thread.scheduler_state().min_finish_time_.raw_value());

  virtual_time_ = eligible_time;
  fair_run_queue_.erase(*eligible_thread);
  TraceThreadQueueEvent("tqe_deque_fair"_stringref, eligible_thread);
  return eligible_thread;
}

// Dequeues the eligible thread with the earliest deadline. The caller must
// ensure that there is at least one eligible thread in the queue.
Thread* Scheduler::DequeueDeadlineThread(SchedTime eligible_time) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_deadline_thread"_stringref};

  Thread* const eligible_thread = FindEarliestEligibleThread(&deadline_run_queue_, eligible_time);
  DEBUG_ASSERT_MSG(eligible_thread != nullptr,
                   "eligible_time=%" PRId64 ", start_time=%" PRId64 ", finish_time=%" PRId64
                   ", min_finish_time=%" PRId64 "!",
                   eligible_time.raw_value(),
                   eligible_thread->scheduler_state().start_time_.raw_value(),
                   eligible_thread->scheduler_state().finish_time_.raw_value(),
                   eligible_thread->scheduler_state().min_finish_time_.raw_value());

  deadline_run_queue_.erase(*eligible_thread);
  TraceThreadQueueEvent("tqe_deque_deadline"_stringref, eligible_thread);

  const SchedulerState& state = eligible_thread->scheduler_state();
  trace.End(Round<uint64_t>(state.start_time_), Round<uint64_t>(state.finish_time_));
  return eligible_thread;
}

// Returns the eligible thread with the earliest deadline that is also earlier
// than the given deadline. Returns nullptr if no threads meet this criteria or
// the run queue is empty.
Thread* Scheduler::FindEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time) {
  Thread* const eligible_thread = FindEarliestEligibleThread(&deadline_run_queue_, eligible_time);
  const bool found_earlier_deadline =
      eligible_thread && eligible_thread->scheduler_state().finish_time_ < finish_time;
  return found_earlier_deadline ? eligible_thread : nullptr;
}

// Returns the time that the next deadline task will become eligible or infinite
// if there are no ready deadline tasks.
SchedTime Scheduler::GetNextEligibleTime() {
  return deadline_run_queue_.is_empty() ? SchedTime{ZX_TIME_INFINITE}
                                        : deadline_run_queue_.front().scheduler_state().start_time_;
}

// Dequeues the eligible thread with the earliest deadline that is also earlier
// than the given deadline. Returns nullptr if no threads meet the criteria or
// the run queue is empty.
Thread* Scheduler::DequeueEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_earlier_deadline_thread"_stringref};
  Thread* const eligible_thread = FindEarlierDeadlineThread(eligible_time, finish_time);

  if (eligible_thread != nullptr) {
    deadline_run_queue_.erase(*eligible_thread);
    TraceThreadQueueEvent("tqe_deque_earlier_deadline"_stringref, eligible_thread);
  }

  return eligible_thread;
}

// Selects a thread to run. Performs any necessary maintenance if the current
// thread is changing, depending on the reason for the change.
Thread* Scheduler::EvaluateNextThread(SchedTime now, Thread* current_thread, bool timeslice_expired,
                                      SchedDuration total_runtime_ns,
                                      Guard<MonitoredSpinLock, NoIrqSave>& queue_guard) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"find_thread"_stringref};

  const bool is_idle = current_thread->IsIdle();
  const bool is_active = current_thread->state() == THREAD_READY;
  const bool is_deadline = IsDeadlineThread(current_thread);
  const bool is_new_deadline_eligible = IsDeadlineThreadEligible(now);

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();

  // Returns true when the given thread requires active migration.
  const auto needs_migration = [active_mask, current_cpu_mask](Thread* const thread) {
    // Threads may be created and resumed before the thread init level. Work
    // around an empty active mask by assuming only the current cpu is available.
    return active_mask != 0 &&
           ((thread->scheduler_state().GetEffectiveCpuMask(active_mask) & current_cpu_mask) == 0 ||
            thread->scheduler_state().next_cpu_ != INVALID_CPU);
  };

  Thread* next_thread = nullptr;
  if (is_active && needs_migration(current_thread)) {
    // Avoid putting the current thread into the run queue in any of the paths
    // below if it needs active migration. Let the migration loop below handle
    // moving the thread. This avoids an edge case where time slice expiration
    // coincides with an action that requires migration. Migration should take
    // precedence over time slice expiration.
    next_thread = current_thread;
  } else if (is_active && likely(!is_idle)) {
    if (timeslice_expired) {
      // If the timeslice expired insert the current thread into the run queue.
      QueueThread(current_thread, Placement::Insertion, now, total_runtime_ns);
    } else if (is_new_deadline_eligible && is_deadline) {
      // The current thread is deadline scheduled and there is at least one
      // eligible deadline thread in the run queue: select the eligible thread
      // with the earliest deadline, which may still be the current thread.
      const SchedTime deadline_ns = current_thread->scheduler_state().finish_time_;
      if (Thread* const earlier_thread = DequeueEarlierDeadlineThread(now, deadline_ns);
          earlier_thread != nullptr) {
        QueueThread(current_thread, Placement::Preemption, now, total_runtime_ns);
        next_thread = earlier_thread;
      } else {
        // The current thread still has the earliest deadline.
        next_thread = current_thread;
      }
    } else if (is_new_deadline_eligible && !is_deadline) {
      // The current thread is fair scheduled and there is at least one eligible
      // deadline thread in the run queue: return this thread to the run queue.
      QueueThread(current_thread, Placement::Preemption, now, total_runtime_ns);
    } else {
      // The current thread has remaining time and no eligible contender.
      next_thread = current_thread;
    }
  } else if (!is_active && likely(!is_idle)) {
    // The current thread is no longer ready, remove its accounting.
    Remove(current_thread);
  }

  // The current thread is no longer running or has returned to the run queue,
  // select another thread to run.
  if (next_thread == nullptr) {
    next_thread = DequeueThread(now, queue_guard);
  }

  // If the next thread needs *active* migration, call the migration function,
  // migrate the thread, and select another thread to run.
  //
  // Most migrations are passive. Passive migration happens whenever a thread
  // becomes READY and a different CPU is selected than the last CPU the thread
  // ran on.
  //
  // Active migration happens under the following conditions:
  //  1. The CPU affinity of a thread that is READY or RUNNING is changed to
  //     exclude the CPU it is currently active on.
  //  2. Passive migration, or active migration due to #1, selects a different
  //     CPU for a thread with a migration function. Migration to the next CPU
  //     is delayed until the migration function is called on the last CPU.
  //  3. A thread that is READY or RUNNING is relocated by the periodic load
  //     balancer. NOT YET IMPLEMENTED.
  //
  cpu_mask_t cpus_to_reschedule_mask = 0;
  for (; needs_migration(next_thread); next_thread = DequeueThread(now, queue_guard)) {
    SchedulerState* const next_state = &next_thread->scheduler_state();

    // If the thread is not scheduled to migrate to a specific CPU, find a
    // suitable target CPU. If the thread has a migration function, the search
    // will schedule the thread to migrate to a specific CPU and return the
    // current CPU.
    cpu_num_t target_cpu = INVALID_CPU;
    if (next_state->next_cpu_ == INVALID_CPU) {
      target_cpu = FindTargetCpu(next_thread);
      DEBUG_ASSERT(target_cpu != this_cpu() || next_state->next_cpu_ != INVALID_CPU);
    }

    // If the thread is scheduled to migrate to a specific CPU, set the target
    // to that CPU and call the migration function.
    if (next_state->next_cpu_ != INVALID_CPU) {
      DEBUG_ASSERT_MSG(next_state->last_cpu_ == this_cpu(),
                       "name=\"%s\" this_cpu=%u last_cpu=%u next_cpu=%u hard_affinity=%x "
                       "soft_affinity=%x migrate_pending=%d",
                       next_thread->name(), this_cpu(), next_state->last_cpu_,
                       next_state->next_cpu_, next_state->hard_affinity(),
                       next_state->soft_affinity(), next_thread->migrate_pending());
      target_cpu = next_state->next_cpu_;
      thread_lock.AssertHeld();  // TODO(eieio): HACK!
      next_thread->CallMigrateFnLocked(Thread::MigrateStage::Before);
      next_state->next_cpu_ = INVALID_CPU;
    }

    // The target CPU must always be different than the current CPU.
    DEBUG_ASSERT(target_cpu != this_cpu());

    // Remove accounting from this run queue and insert in the target run queue.
    Remove(next_thread);
    Scheduler* const target = Get(target_cpu);

    queue_guard.CallUnlocked([&] {
      Guard<MonitoredSpinLock, NoIrqSave> target_queue_guard{&target->queue_lock_, SOURCE_TAG};
      target->Insert(now, next_thread);
    });

    cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
  }

  // Issue reschedule IPIs to CPUs with migrated threads.
  mp_reschedule(cpus_to_reschedule_mask, 0);

  return next_thread;
}

cpu_num_t Scheduler::FindTargetCpu(Thread* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"find_target: cpu,avail"_stringref};

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();

  // Determine the set of CPUs the thread is allowed to run on.
  //
  // Threads may be created and resumed before the thread init level. Work around
  // an empty active mask by assuming the current cpu is scheduleable.
  const cpu_mask_t available_mask = active_mask != 0
                                        ? thread->scheduler_state().GetEffectiveCpuMask(active_mask)
                                        : current_cpu_mask;
  DEBUG_ASSERT_MSG(available_mask != 0,
                   "thread=%s affinity=%#x soft_affinity=%#x active=%#x "
                   "idle=%#x arch_ints_disabled=%d",
                   thread->name(), thread->scheduler_state().hard_affinity_,
                   thread->scheduler_state().soft_affinity_, active_mask, mp_get_idle_mask(),
                   arch_ints_disabled());

  LOCAL_KTRACE(KTRACE_DETAILED, "target_mask: online,active", mp_get_online_mask(), active_mask);

  const cpu_num_t last_cpu = thread->scheduler_state().last_cpu_;
  const cpu_mask_t last_cpu_mask = cpu_num_to_mask(last_cpu);

  // Find the best target CPU starting at the last CPU the task ran on, if any.
  // Alternatives are considered in order of best to worst potential cache
  // affinity.
  const cpu_num_t starting_cpu = last_cpu != INVALID_CPU ? last_cpu : current_cpu;
  const CpuSearchSet& search_set = percpu::Get(starting_cpu).search_set;

  // TODO(fxbug.dev/98291): Working on isolating a low-frequency panic due to
  // apparent memory corruption of percpu intersecting CpuSearchSet, resulting
  // in an invalid entry pointer and/or entry count. Adding an assert to help
  // catch the corruption and include additional context. This assert is enabled
  // in non-eng builds, however, the small impact is acceptable for production.
  ASSERT_MSG(search_set.cpu_count() <= SMP_MAX_CPUS,
             "current_cpu=%u starting_cpu=%u active_mask=%x thread=%p search_set=%p cpu_count=%zu "
             "entries=%p",
             current_cpu, starting_cpu, active_mask, thread, &search_set, search_set.cpu_count(),
             search_set.const_iterator().data());

  // Compares candidate queues and returns true if |queue_a| is a better
  // alternative than |queue_b|. This is used by the target selection loop to
  // determine whether the next candidate is better than the current target.
  const auto compare = [thread](const Scheduler* queue_a, const Scheduler* queue_b) {
    const SchedDuration a_predicted_queue_time_ns = queue_a->predicted_queue_time_ns();
    const SchedDuration b_predicted_queue_time_ns = queue_b->predicted_queue_time_ns();
    LocalTraceDuration<KTRACE_DETAILED> trace_compare{"compare: qtime,qtime"_stringref,
                                                      Round<uint64_t>(a_predicted_queue_time_ns),
                                                      Round<uint64_t>(b_predicted_queue_time_ns)};
    if (IsFairThread(thread)) {
      // CPUs in the same logical cluster are considered equivalent in terms of
      // cache affinity. Choose the least loaded among the members of a cluster.
      if (queue_a->cluster() == queue_b->cluster()) {
        ktl::pair a{a_predicted_queue_time_ns, queue_a->predicted_deadline_utilization()};
        ktl::pair b{b_predicted_queue_time_ns, queue_b->predicted_deadline_utilization()};
        return a < b;
      }

      // Only consider crossing cluster boundaries if the current candidate is
      // above the threshold.
      return b_predicted_queue_time_ns > kInterClusterThreshold &&
             a_predicted_queue_time_ns < b_predicted_queue_time_ns;
    } else {
      const SchedUtilization utilization = thread->scheduler_state().deadline_.utilization;
      const SchedUtilization scaled_utilization_a = queue_a->ScaleUp(utilization);
      const SchedUtilization scaled_utilization_b = queue_b->ScaleUp(utilization);

      ktl::pair a{scaled_utilization_a, a_predicted_queue_time_ns};
      ktl::pair b{scaled_utilization_b, b_predicted_queue_time_ns};
      ktl::pair a_prime{queue_a->predicted_deadline_utilization(), a};
      ktl::pair b_prime{queue_b->predicted_deadline_utilization(), b};
      return a_prime < b_prime;
    }
  };

  // Determines whether the current target is sufficiently good to terminate the
  // selection loop.
  const auto is_sufficient = [thread](const Scheduler* queue) {
    const SchedDuration candidate_queue_time_ns = queue->predicted_queue_time_ns();

    LocalTraceDuration<KTRACE_DETAILED> sufficient_trace{"is_sufficient: thresh,qtime"_stringref,
                                                         Round<uint64_t>(kIntraClusterThreshold),
                                                         Round<uint64_t>(candidate_queue_time_ns)};

    if (IsFairThread(thread)) {
      return candidate_queue_time_ns <= kIntraClusterThreshold;
    }

    const SchedUtilization predicted_utilization = queue->predicted_deadline_utilization();
    const SchedUtilization utilization = thread->scheduler_state().deadline_.utilization;
    const SchedUtilization scaled_utilization = queue->ScaleUp(utilization);

    return candidate_queue_time_ns <= kIntraClusterThreshold &&
           scaled_utilization <= kThreadUtilizationMax &&
           predicted_utilization + scaled_utilization <= kCpuUtilizationLimit;
  };

  // Loop over the search set for CPU the task last ran on to find a suitable
  // target.
  cpu_num_t target_cpu = INVALID_CPU;
  Scheduler* target_queue = nullptr;

  for (const auto& entry : search_set.const_iterator()) {
    const cpu_num_t candidate_cpu = entry.cpu;
    const bool candidate_available = available_mask & cpu_num_to_mask(candidate_cpu);
    Scheduler* const candidate_queue = Get(candidate_cpu);

    if (candidate_available &&
        (target_queue == nullptr || compare(candidate_queue, target_queue))) {
      target_cpu = candidate_cpu;
      target_queue = candidate_queue;

      // Stop searching at the first sufficiently unloaded CPU.
      if (is_sufficient(target_queue)) {
        break;
      }
    }
  }

  DEBUG_ASSERT(target_cpu != INVALID_CPU);

  trace.End(last_cpu, target_cpu);

  bool delay_migration = last_cpu != target_cpu && last_cpu != INVALID_CPU &&
                         thread->has_migrate_fn() && (active_mask & last_cpu_mask) != 0;
  if (unlikely(delay_migration)) {
    thread->scheduler_state().next_cpu_ = target_cpu;
    return last_cpu;
  } else {
    return target_cpu;
  }
}

void Scheduler::UpdateTimeline(SchedTime now) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"update_vtime"_stringref};

  const auto runtime_ns = now - last_update_time_ns_;
  last_update_time_ns_ = now;

  if (weight_total_ > SchedWeight{0}) {
    virtual_time_ += runtime_ns;
  }

  trace.End(Round<uint64_t>(runtime_ns), Round<uint64_t>(virtual_time_));
}

void Scheduler::RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"reschedule_common"_stringref, Round<uint64_t>(now), 0};

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  Thread* const current_thread = Thread::Current::Get();
  SchedulerState* const current_state = &current_thread->scheduler_state();

  current_thread->get_lock().AssertHeld();

  // Aside from the invariant lock, spinlocks should never be held over a reschedule.
  DEBUG_ASSERT(arch_num_spinlocks_held() == 1);
  DEBUG_ASSERT_MSG(current_thread->state() != THREAD_RUNNING, "state %d\n",
                   current_thread->state());
  DEBUG_ASSERT(!arch_blocking_disallowed());
  DEBUG_ASSERT_MSG(current_cpu == this_cpu(), "current_cpu=%u this_cpu=%u", current_cpu,
                   this_cpu());

  CPU_STATS_INC(reschedules);

  Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&queue_lock_, SOURCE_TAG};
  UpdateTimeline(now);

  const SchedDuration total_runtime_ns = now - start_of_current_time_slice_ns_;
  const SchedDuration actual_runtime_ns = now - current_state->last_started_running_;
  current_state->last_started_running_ = now;
  thread_lock.AssertHeld();  // TODO(eieio): HACK!
  current_thread->UpdateSchedulerStats({.state = current_thread->state(),
                                        .state_time = now.raw_value(),
                                        .cpu_time = actual_runtime_ns.raw_value()});

  // Update the runtime accounting for the thread that just ran.
  current_state->runtime_ns_ += actual_runtime_ns;

  // Adjust the rate of the current thread when demand changes. Changes in
  // demand could be due to threads entering or leaving the run queue, or due
  // to weights changing in the current or enqueued threads.
  if (IsThreadAdjustable(current_thread) && weight_total_ != scheduled_weight_total_ &&
      total_runtime_ns < current_state->time_slice_ns_) {
    LocalTraceDuration<KTRACE_DETAILED> trace_adjust_rate{"adjust_rate"_stringref};
    scheduled_weight_total_ = weight_total_;

    const SchedDuration time_slice_ns = CalculateTimeslice(current_thread);
    const SchedDuration remaining_time_slice_ns =
        time_slice_ns * current_state->fair_.normalized_timeslice_remainder;

    const bool timeslice_changed = time_slice_ns != current_state->fair_.initial_time_slice_ns;
    const bool timeslice_remaining = total_runtime_ns < remaining_time_slice_ns;

    // Update the preemption timer if necessary.
    if (timeslice_changed && timeslice_remaining) {
      target_preemption_time_ns_ = start_of_current_time_slice_ns_ + remaining_time_slice_ns;
      const SchedTime preemption_time_ns = ClampToDeadline(target_preemption_time_ns_);
      DEBUG_ASSERT(preemption_time_ns <= target_preemption_time_ns_);
      percpu::Get(current_cpu).timer_queue.PreemptReset(preemption_time_ns.raw_value());
    }

    current_state->fair_.initial_time_slice_ns = time_slice_ns;
    current_state->time_slice_ns_ = remaining_time_slice_ns;
    trace_adjust_rate.End(Round<uint64_t>(remaining_time_slice_ns),
                          Round<uint64_t>(total_runtime_ns));
  }

  // Update the time slice of a deadline task before evaluating the next task.
  if (IsDeadlineThread(current_thread)) {
    // Scale the actual runtime of the deadline task by the relative performance
    // of the CPU, effectively increasing the capacity of the task in proportion
    // to the performance ratio. The remaining time slice may become negative
    // due to scheduler overhead.
    current_state->time_slice_ns_ -= ScaleDown(actual_runtime_ns);
  }

  // Rounding in the scaling above may result in a small non-zero time slice
  // when the time slice should expire from the perspective of the target
  // preemption time. Use a small epsilon to avoid tripping the consistency
  // check below.
  const SchedDuration deadline_time_slice_epsilon{100};

  // Fair and deadline tasks have different time slice accounting strategies:
  // - A fair task expires when the total runtime meets or exceeds the time
  //   slice, which is updated only when the thread is adjusted or returns to
  //   the run queue.
  // - A deadline task expires when the remaining time slice is exhausted,
  //   updated incrementally on every reschedule, or when the absolute deadline
  //   is reached, which may occur with remaining time slice if the task wakes
  //   up late.
  const bool timeslice_expired =
      IsFairThread(current_thread)
          ? total_runtime_ns >= current_state->time_slice_ns_
          : now >= current_state->finish_time_ ||
                current_state->time_slice_ns_ <= deadline_time_slice_epsilon;

  // Check the consistency of the target preemption time and the current time
  // slice.
  DEBUG_ASSERT_MSG(
      now < target_preemption_time_ns_ || timeslice_expired,
      "capacity_ns=%" PRId64 " deadline_ns=%" PRId64 " now=%" PRId64
      " target_preemption_time_ns=%" PRId64 " total_runtime_ns=%" PRId64
      " actual_runtime_ns=%" PRId64 " finish_time=%" PRId64 " time_slice_ns=%" PRId64
      " start_of_current_time_slice_ns=%" PRId64,
      IsDeadlineThread(current_thread) ? current_state->deadline_.capacity_ns.raw_value() : 0,
      IsDeadlineThread(current_thread) ? current_state->deadline_.deadline_ns.raw_value() : 0,
      now.raw_value(), target_preemption_time_ns_.raw_value(), total_runtime_ns.raw_value(),
      actual_runtime_ns.raw_value(), current_state->finish_time_.raw_value(),
      current_state->time_slice_ns_.raw_value(), start_of_current_time_slice_ns_.raw_value());

  // Select a thread to run.
  Thread* const next_thread =
      EvaluateNextThread(now, current_thread, timeslice_expired, total_runtime_ns, queue_guard);
  DEBUG_ASSERT(next_thread != nullptr);
  SchedulerState* const next_state = &next_thread->scheduler_state();

  // Flush pending preemptions.
  mp_reschedule(current_thread->preemption_state().preempts_pending(), 0);
  current_thread->preemption_state().preempts_pending_clear();

  // Update the state of the current and next thread.
  next_thread->set_running();
  next_state->last_cpu_ = current_cpu;
  next_state->curr_cpu_ = current_cpu;
  active_thread_ = next_thread;

  // Trace the activation of the next thread before context switching.
  if (current_thread != next_thread) {
    TraceThreadQueueEvent("tqe_activate"_stringref, next_thread);
  }

  // Handle any pending migration work.
  next_thread->CallMigrateFnLocked(Thread::MigrateStage::After);

  // Update the expected runtime of the current thread and the per-CPU total.
  // Only update the thread and aggregate values if the current thread is still
  // associated with this CPU or is no longer ready.
  const bool current_is_associated =
      !current_state->active() || current_state->curr_cpu_ == current_cpu;
  if (!current_thread->IsIdle() && current_is_associated &&
      (timeslice_expired || current_thread != next_thread)) {
    LocalTraceDuration<KTRACE_DETAILED> update_ema_trace{"update_expected_runtime"_stringref};

    // Adjust the runtime for the relative performance of the CPU to account for
    // different performance levels in the estimate. The relative performance
    // scale is in the range (0.0, 1.0], such that the adjusted runtime is
    // always less than or equal to the monotonic runtime.
    const SchedDuration adjusted_total_runtime_ns = ScaleDown(total_runtime_ns);
    current_state->banked_runtime_ns_ += adjusted_total_runtime_ns;

    if (timeslice_expired || !current_state->active()) {
      const SchedDuration delta_ns =
          PeakDecayDelta(current_state->expected_runtime_ns_, current_state->banked_runtime_ns_,
                         kExpectedRuntimeAlpha, kExpectedRuntimeBeta);
      current_state->expected_runtime_ns_ += delta_ns;
      current_state->banked_runtime_ns_ = SchedDuration{0};

      // Adjust the aggregate value by the same amount. The adjustment is only
      // necessary when the thread is still active on this CPU.
      if (current_state->active()) {
        UpdateTotalExpectedRuntime(delta_ns);
      }
    }
  }

  // Update the current performance scale only after any uses in the reschedule
  // path above to ensure the scale is applied consistently over the interval
  // between reschedules (i.e. not earlier than the requested update).
  //
  // Updating the performance scale also results in updating the target
  // preemption time below when the current thread is deadline scheduled.
  //
  // TODO(eieio): Apply a minimum value threshold to the userspace value.
  // TODO(eieio): Shed load when total utilization is above kCpuUtilizationLimit.
  const bool performance_scale_updated = performance_scale_ != pending_user_performance_scale_;
  if (performance_scale_updated) {
    performance_scale_ = pending_user_performance_scale_;
    performance_scale_reciprocal_ = 1 / performance_scale_;
  }

  // Always call to handle races between reschedule IPIs and changes to the run
  // queue.
  mp_prepare_current_cpu_idle_state(next_thread->IsIdle());

  if (next_thread->IsIdle()) {
    mp_set_cpu_idle(current_cpu);
  } else {
    mp_set_cpu_busy(current_cpu);
  }

  if (current_thread->IsIdle()) {
    percpu::Get(current_cpu).stats.idle_time += actual_runtime_ns;
  }

  if (next_thread->IsIdle()) {
    LocalTraceDuration<KTRACE_DETAILED> trace_stop_preemption{"idle"_stringref};
    next_state->last_started_running_ = now;

    // If there are no tasks to run in the future, disable the preemption timer.
    // Otherwise, set the preemption time to the earliest eligible time.
    target_preemption_time_ns_ = GetNextEligibleTime();
    percpu::Get(current_cpu).timer_queue.PreemptReset(target_preemption_time_ns_.raw_value());
  } else if (timeslice_expired || next_thread != current_thread) {
    LocalTraceDuration<KTRACE_DETAILED> trace_start_preemption{"next_slice: preempt,abs"_stringref};

    // Re-compute the time slice and deadline for the new thread based on the
    // latest state.
    target_preemption_time_ns_ = NextThreadTimeslice(next_thread, now);

    // Compute the time the next thread spent in the run queue. The value of
    // last_started_running for the current thread is updated at the top of
    // this method: when the current and next thread are the same, the queue
    // time is zero. Otherwise, last_started_running is the time the next thread
    // entered the run queue.
    const SchedDuration queue_time_ns = now - next_state->last_started_running_;

    next_thread->UpdateSchedulerStats({.state = next_thread->state(),
                                       .state_time = now.raw_value(),
                                       .queue_time = queue_time_ns.raw_value()});

    next_state->last_started_running_ = now;
    start_of_current_time_slice_ns_ = now;
    scheduled_weight_total_ = weight_total_;

    // Adjust the preemption time to account for a deadline thread becoming
    // eligible before the current time slice expires.
    const SchedTime preemption_time_ns =
        IsFairThread(next_thread)
            ? ClampToDeadline(target_preemption_time_ns_)
            : ClampToEarlierDeadline(target_preemption_time_ns_, next_state->finish_time_);
    DEBUG_ASSERT(preemption_time_ns <= target_preemption_time_ns_);

    percpu::Get(current_cpu).timer_queue.PreemptReset(preemption_time_ns.raw_value());
    trace_start_preemption.End(Round<uint64_t>(preemption_time_ns),
                               Round<uint64_t>(target_preemption_time_ns_));

    // Emit a flow end event to match the flow begin event emitted when the
    // thread was enqueued. Emitting in this scope ensures that thread just
    // came from the run queue (and is not the idle thread).
    LOCAL_KTRACE_FLOW_END(KTRACE_FLOW, "sched_latency", next_state->flow_id(), next_thread->tid());
  } else {
    LocalTraceDuration<KTRACE_DETAILED> trace_continue{"continue: preempt,abs"_stringref};
    DEBUG_ASSERT(current_thread == next_thread);

    // Update the target preemption time for consistency with the updated CPU
    // performance scale.
    if (performance_scale_updated && IsDeadlineThread(next_thread)) {
      target_preemption_time_ns_ = NextThreadTimeslice(next_thread, now);
    }

    // The current thread should continue to run. A throttled deadline thread
    // might become eligible before the current time slice expires. Figure out
    // whether to set the preemption time earlier to switch to the newly
    // eligible thread.
    //
    // The preemption time should be set earlier when either:
    //   * Current is a fair thread and a deadline thread will become eligible
    //     before its time slice expires.
    //   * Current is a deadline thread and a deadline thread with an earlier
    //     deadline will become eligible before its time slice expires.
    //
    // Note that the target preemption time remains set to the ideal
    // preemption time for the current task, even if the preemption timer is set
    // earlier. If a task that becomes eligible is stolen before the early
    // preemption is handled, this logic will reset to the original target
    // preemption time.
    const SchedTime preemption_time_ns =
        IsFairThread(next_thread)
            ? ClampToDeadline(target_preemption_time_ns_)
            : ClampToEarlierDeadline(target_preemption_time_ns_, next_state->finish_time_);
    DEBUG_ASSERT(preemption_time_ns <= target_preemption_time_ns_);

    percpu::Get(current_cpu).timer_queue.PreemptReset(preemption_time_ns.raw_value());
    trace_continue.End(Round<uint64_t>(preemption_time_ns),
                       Round<uint64_t>(target_preemption_time_ns_));
  }

  // Assert that there is no path beside running the idle thread can leave the
  // preemption timer unarmed. However, the preemption timer may or may not be
  // armed when running the idle thread.
  // TODO(eieio): In the future, the preemption timer may be canceled when there
  // is only one task available to run. Revisit this assertion at that time.
  DEBUG_ASSERT(next_thread->IsIdle() || percpu::Get(current_cpu).timer_queue.PreemptArmed());

  if (next_thread != current_thread) {
    LOCAL_KTRACE(KTRACE_DETAILED, "reschedule current: count,slice",
                 runnable_fair_task_count_ + runnable_deadline_task_count_,
                 Round<uint64_t>(current_thread->scheduler_state().time_slice_ns_));
    LOCAL_KTRACE(KTRACE_DETAILED, "reschedule next: wsum,slice", weight_total_.raw_value(),
                 Round<uint64_t>(next_thread->scheduler_state().time_slice_ns_));

    // Release queue lock before context switching.
    queue_guard.Release();

    TraceContextSwitch(current_thread, next_thread, current_cpu);

    if (current_thread->aspace() != next_thread->aspace()) {
      vmm_context_switch(current_thread->aspace(), next_thread->aspace());
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

    // Record the thread that was previously running for lock handoff after the context switch.
    current_state->incoming_locked_ = true;
    next_state->outgoing_locked_ = next_state->incoming_locked_;
    next_state->incoming_locked_ = true;
    next_state->previous_thread_ = current_thread;

    arch_context_switch(current_thread, next_thread);

    // After the context switch, current_thread holds the same value as
    // next_thread from the previous thread's context.
    LockHandoffInternal(current_thread);
  }
}

void Scheduler::LockHandoffInternal(Thread* thread) {
  SchedulerState* const state = &thread->scheduler_state();
  DEBUG_ASSERT(state->previous_thread_ != nullptr);

  state->previous_thread_->get_lock().AssertHeld();
  state->previous_thread_->get_lock().Release();
  if (state->outgoing_locked_) {
    thread->get_lock().Acquire(SOURCE_TAG);
  }

  state->previous_thread_ = nullptr;
  state->incoming_locked_ = state->outgoing_locked_ = false;
}

void Scheduler::LockHandoff() {
  DEBUG_ASSERT(arch_ints_disabled());
  LockHandoffInternal(Thread::Current::Get());
}

void Scheduler::UpdatePeriod() {
  LocalTraceDuration<KTRACE_DETAILED> trace{"update_period"_stringref};

  DEBUG_ASSERT(runnable_fair_task_count_ >= 0);
  DEBUG_ASSERT(minimum_granularity_ns_ > 0);
  DEBUG_ASSERT(target_latency_grans_ > 0);

  const int64_t num_tasks = runnable_fair_task_count_;
  const int64_t normal_tasks = Round<int64_t>(target_latency_grans_);

  // The scheduling period stretches when there are too many tasks to fit
  // within the target latency.
  scheduling_period_grans_ = SchedDuration{num_tasks > normal_tasks ? num_tasks : normal_tasks};

  trace.End(Round<uint64_t>(scheduling_period_grans_), num_tasks);
}

SchedDuration Scheduler::CalculateTimeslice(Thread* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"calculate_timeslice: w,wt"_stringref};
  SchedulerState* const state = &thread->scheduler_state();

  // Calculate the relative portion of the scheduling period.
  const SchedWeight proportional_time_slice_grans =
      scheduling_period_grans_ * state->fair_.weight / weight_total_;

  // Ensure that the time slice is at least the minimum granularity.
  const int64_t time_slice_grans = Round<int64_t>(proportional_time_slice_grans);
  const int64_t minimum_time_slice_grans = time_slice_grans > 0 ? time_slice_grans : 1;

  // Calcluate the time slice in nanoseconds.
  const SchedDuration time_slice_ns = minimum_time_slice_grans * minimum_granularity_ns_;

  trace.End(state->fair_.weight.raw_value(), weight_total_.raw_value());
  return time_slice_ns;
}

SchedTime Scheduler::ClampToDeadline(SchedTime completion_time) {
  return ktl::min(completion_time, GetNextEligibleTime());
}

SchedTime Scheduler::ClampToEarlierDeadline(SchedTime completion_time, SchedTime finish_time) {
  Thread* const thread = FindEarlierDeadlineThread(completion_time, finish_time);
  return thread ? ktl::min(completion_time, thread->scheduler_state().start_time_)
                : completion_time;
}

SchedTime Scheduler::NextThreadTimeslice(Thread* thread, SchedTime now) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"next_timeslice: t,abs"_stringref};

  SchedulerState* const state = &thread->scheduler_state();
  SchedTime target_preemption_time_ns;

  if (IsFairThread(thread)) {
    // Calculate the next time slice and the deadline when the time slice is
    // completed.
    const SchedDuration time_slice_ns = CalculateTimeslice(thread);
    const SchedDuration remaining_time_slice_ns =
        time_slice_ns * state->fair_.normalized_timeslice_remainder;

    DEBUG_ASSERT(time_slice_ns > 0);
    DEBUG_ASSERT(remaining_time_slice_ns > 0);

    state->fair_.initial_time_slice_ns = time_slice_ns;
    state->time_slice_ns_ = remaining_time_slice_ns;
    target_preemption_time_ns = now + remaining_time_slice_ns;

    DEBUG_ASSERT_MSG(state->time_slice_ns_ > 0 && target_preemption_time_ns > now,
                     "time_slice_ns=%" PRId64 " now=%" PRId64 " target_preemption_time_ns=%" PRId64,
                     state->time_slice_ns_.raw_value(), now.raw_value(),
                     target_preemption_time_ns.raw_value());

    trace.End(Round<uint64_t>(state->time_slice_ns_), Round<uint64_t>(target_preemption_time_ns));
  } else {
    // Calculate the deadline when the remaining time slice is completed. The
    // time slice is maintained by the deadline queuing logic, no need to update
    // it here. The target preemption time is based on the time slice scaled by
    // the performance of the CPU and clamped to the deadline. This increases
    // capacity on slower processors, however, bandwidth isolation is preserved
    // because CPU selection attempts to keep scaled total capacity below one.
    const SchedDuration scaled_time_slice_ns = ScaleUp(state->time_slice_ns_);
    target_preemption_time_ns =
        ktl::min<SchedTime>(now + scaled_time_slice_ns, state->finish_time_);

    trace.End(Round<uint64_t>(scaled_time_slice_ns), Round<uint64_t>(target_preemption_time_ns));
  }

  return target_preemption_time_ns;
}

void Scheduler::QueueThread(Thread* thread, Placement placement, SchedTime now,
                            SchedDuration total_runtime_ns) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"queue_thread: s,f"_stringref};

  DEBUG_ASSERT(thread->state() == THREAD_READY);
  DEBUG_ASSERT(!thread->IsIdle());
  DEBUG_ASSERT(placement != Placement::Association);

  SchedulerState* const state = &thread->scheduler_state();

  if (IsFairThread(thread)) {
    // Account for the consumed fair time slice. The consumed time is zero when
    // the thread is unblocking, migrating, or adjusting queue position. The
    // remaining time slice may become negative due to scheduler overhead.
    state->time_slice_ns_ -= total_runtime_ns;

    // Compute the ratio of remaining time slice to ideal time slice. This may
    // be less than 1.0 due to time slice consumed or due to previous preemption
    // by a deadline task or both.
    const SchedRemainder normalized_timeslice_remainder =
        state->time_slice_ns_ / ktl::max(state->fair_.initial_time_slice_ns, SchedDuration{1});

    DEBUG_ASSERT_MSG(
        normalized_timeslice_remainder <= SchedRemainder{1},
        "time_slice_ns=%" PRId64 " initial_time_slice_ns=%" PRId64 " remainder=%" PRId64 "\n",
        state->time_slice_ns_.raw_value(), state->fair_.initial_time_slice_ns.raw_value(),
        normalized_timeslice_remainder.raw_value());

    if (placement == Placement::Insertion || normalized_timeslice_remainder <= 0) {
      state->start_time_ = ktl::max(state->finish_time_, virtual_time_);
      state->fair_.normalized_timeslice_remainder = SchedRemainder{1};
    } else if (placement == Placement::Preemption) {
      DEBUG_ASSERT(state->time_slice_ns_ > 0);
      state->fair_.normalized_timeslice_remainder = normalized_timeslice_remainder;
    }

    const SchedDuration scheduling_period_ns = scheduling_period_grans_ * minimum_granularity_ns_;
    const SchedWeight rate = kReciprocalMinWeight * state->fair_.weight;
    const SchedDuration delta_norm = scheduling_period_ns / rate;
    state->finish_time_ = state->start_time_ + delta_norm;

    DEBUG_ASSERT_MSG(state->start_time_ < state->finish_time_,
                     "start=%" PRId64 " finish=%" PRId64 " delta_norm=%" PRId64 "\n",
                     state->start_time_.raw_value(), state->finish_time_.raw_value(),
                     delta_norm.raw_value());
  } else {
    // Both a new insertion into the run queue or a re-insertion due to
    // preemption can happen after the time slice and/or deadline expires.
    if (placement == Placement::Insertion || placement == Placement::Preemption) {
      const auto string_ref = placement == Placement::Insertion
                                  ? "insert_deadline: r,c"_stringref
                                  : "preemption_deadline: r,c"_stringref;
      LocalTraceDuration<KTRACE_DETAILED> deadline_trace{string_ref};

      // Determine how much time is left before the deadline. This might be less
      // than the remaining time slice or negative if the thread blocked.
      const SchedDuration time_until_deadline_ns = state->finish_time_ - now;
      if (time_until_deadline_ns <= 0 || state->time_slice_ns_ <= 0) {
        const SchedTime period_finish_ns = state->start_time_ + state->deadline_.deadline_ns;

        state->start_time_ = now >= period_finish_ns ? now : period_finish_ns;
        state->finish_time_ = state->start_time_ + state->deadline_.deadline_ns;
        state->time_slice_ns_ = state->deadline_.capacity_ns;
      }
      deadline_trace.End(Round<uint64_t>(time_until_deadline_ns),
                         Round<uint64_t>(state->time_slice_ns_));
    }

    DEBUG_ASSERT_MSG(state->start_time_ < state->finish_time_,
                     "start=%" PRId64 " finish=%" PRId64 " capacity=%" PRId64 "\n",
                     state->start_time_.raw_value(), state->finish_time_.raw_value(),
                     state->time_slice_ns_.raw_value());
  }

  // Only update the generation, enqueue time, and emit a flow event if this
  // is an insertion, preemption, or migration. In contrast, an adjustment only
  // changes the queue position in the same queue due to a parameter change and
  // should not perform these actions.
  if (placement != Placement::Adjustment) {
    if (placement == Placement::Migration) {
      // Connect the flow into the previous queue to the new queue.
      LOCAL_KTRACE_FLOW_STEP(KTRACE_FLOW, "sched_latency", state->flow_id(), thread->tid());
    } else {
      // Reuse this member to track the time the thread enters the run queue. It
      // is not read outside of the scheduler unless the thread state is
      // THREAD_RUNNING.
      state->last_started_running_ = now;
      state->flow_id_ = NextFlowId();
      LOCAL_KTRACE_FLOW_BEGIN(KTRACE_FLOW, "sched_latency", state->flow_id(), thread->tid());
    }

    // The generation count must always be updated when changing between CPUs,
    // as each CPU has its own generation count.
    state->generation_ = ++generation_count_;
  }

  // Insert the thread into the appropriate run queue after the generation count
  // is potentially updated above.
  if (IsFairThread(thread)) {
    fair_run_queue_.insert(thread);
  } else {
    deadline_run_queue_.insert(thread);
  }
  TraceThreadQueueEvent("tqe_enque"_stringref, thread);
  trace.End(Round<uint64_t>(state->start_time_), Round<uint64_t>(state->finish_time_));
}

void Scheduler::Insert(SchedTime now, Thread* thread, Placement placement) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"insert"_stringref};

  DEBUG_ASSERT(thread->state() == THREAD_READY);
  DEBUG_ASSERT(!thread->IsIdle());

  SchedulerState* const state = &thread->scheduler_state();

  // Ensure insertion happens only once, even if Unblock is called multiple times.
  if (state->OnInsert()) {
    // Insertion can happen from a different CPU. Set the thread's current
    // CPU to the one this scheduler instance services.
    state->curr_cpu_ = this_cpu();

    UpdateTotalExpectedRuntime(state->expected_runtime_ns_);

    if (IsFairThread(thread)) {
      runnable_fair_task_count_++;
      DEBUG_ASSERT(runnable_fair_task_count_ > 0);

      UpdateTimeline(now);
      UpdatePeriod();

      weight_total_ += state->fair_.weight;
      DEBUG_ASSERT(weight_total_ > 0);
    } else {
      UpdateTotalDeadlineUtilization(state->deadline_.utilization);
      runnable_deadline_task_count_++;
      DEBUG_ASSERT(runnable_deadline_task_count_ != 0);
    }
    TraceTotalRunnableThreads();

    if (placement != Placement::Association) {
      QueueThread(thread, placement, now);
    } else {
      // Connect the flow into the previous queue to the new queue.
      LOCAL_KTRACE_FLOW_STEP(KTRACE_FLOW, "sched_latency", state->flow_id(), thread->tid());
    }
  }
}

void Scheduler::Remove(Thread* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"remove"_stringref};

  DEBUG_ASSERT(!thread->IsIdle());

  SchedulerState* const state = &thread->scheduler_state();
  DEBUG_ASSERT(!state->InQueue());

  // Ensure that removal happens only once, even if Block() is called multiple times.
  if (state->OnRemove()) {
    state->curr_cpu_ = INVALID_CPU;

    UpdateTotalExpectedRuntime(-state->expected_runtime_ns_);

    if (IsFairThread(thread)) {
      DEBUG_ASSERT(runnable_fair_task_count_ > 0);
      runnable_fair_task_count_--;

      UpdatePeriod();

      state->start_time_ = SchedNs(0);
      state->finish_time_ = SchedNs(0);

      weight_total_ -= state->fair_.weight;
      DEBUG_ASSERT(weight_total_ >= 0);
    } else {
      UpdateTotalDeadlineUtilization(-state->deadline_.utilization);
      DEBUG_ASSERT(runnable_deadline_task_count_ > 0);
      runnable_deadline_task_count_--;
    }
    TraceTotalRunnableThreads();
  }
}

inline void Scheduler::RescheduleMask(cpu_mask_t cpus_to_reschedule_mask) {
  // Does the local CPU need to be preempted?
  const cpu_mask_t local_mask = cpu_num_to_mask(arch_curr_cpu_num());
  const cpu_mask_t local_cpu = cpus_to_reschedule_mask & local_mask;

  PreemptionState& preemption_state = Thread::Current::Get()->preemption_state();

  // First deal with the remote CPUs.
  if (preemption_state.EagerReschedDisableCount() == 0) {
    // |mp_reschedule| will remove the local cpu from the mask for us.
    mp_reschedule(cpus_to_reschedule_mask, 0);
  } else {
    // EagerReschedDisabled implies that local preemption is also disabled.
    DEBUG_ASSERT(!preemption_state.PreemptIsEnabled());
    preemption_state.preempts_pending_add(cpus_to_reschedule_mask);
    if (local_cpu != 0) {
      preemption_state.EvaluateTimesliceExtension();
    }
    return;
  }

  if (local_cpu != 0) {
    const bool preempt_enabled = preemption_state.EvaluateTimesliceExtension();

    // Can we do it here and now?
    if (preempt_enabled) {
      // TODO(fxbug.dev/64884): Once spinlocks imply preempt disable, this if-else can be replaced
      // with a call to Preempt().
      if (arch_num_spinlocks_held() < 2 && !arch_blocking_disallowed()) {
        // Yes, do it.
        Preempt();
        return;
      }
    }

    // Nope, can't do it now.  Make a note for later.
    preemption_state.preempts_pending_add(local_cpu);
  }
}

void Scheduler::Block() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_block"_stringref};

  Thread* const current_thread = Thread::Current::Get();
  current_thread->canary().Assert();
  current_thread->get_lock().AssertHeld();
  DEBUG_ASSERT(current_thread->state() != THREAD_RUNNING);

  const SchedTime now = CurrentTime();
  Scheduler::Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Unblock(Thread* thread) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_unblock"_stringref};

  thread->canary().Assert();

  const SchedTime now = CurrentTime();
  const cpu_num_t target_cpu = FindTargetCpu(thread);
  Scheduler* const target = Get(target_cpu);

  thread->set_ready();
  {
    Guard<MonitoredSpinLock, NoIrqSave> queue_guard_{&target->queue_lock_, SOURCE_TAG};
    target->Insert(now, thread);
  }

  trace.End();
  RescheduleMask(cpu_num_to_mask(target_cpu));
}

void Scheduler::Unblock(Thread::UnblockList list) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_unblock_list"_stringref};

  const SchedTime now = CurrentTime();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  Thread* thread;
  while ((thread = list.pop_back()) != nullptr) {
    thread->canary().Assert();
    DEBUG_ASSERT(!thread->IsIdle());

    const cpu_num_t target_cpu = FindTargetCpu(thread);
    Scheduler* const target = Get(target_cpu);

    thread->set_ready();
    {
      Guard<MonitoredSpinLock, NoIrqSave> queue_guard_{&target->queue_lock_, SOURCE_TAG};
      target->Insert(now, thread);
    }

    cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);
  }

  trace.End();
  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::UnblockIdle(Thread* thread) {
  SchedulerState* const state = &thread->scheduler_state();

  DEBUG_ASSERT(thread->IsIdle());
  DEBUG_ASSERT(state->hard_affinity_ && (state->hard_affinity_ & (state->hard_affinity_ - 1)) == 0);

  thread->set_ready();
  state->curr_cpu_ = lowest_cpu_set(state->hard_affinity_);
}

void Scheduler::Yield() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_yield"_stringref};

  Thread* const current_thread = Thread::Current::Get();
  current_thread->get_lock().AssertHeld();

  SchedulerState* const current_state = &current_thread->scheduler_state();
  DEBUG_ASSERT(!current_thread->IsIdle());

  if (IsFairThread(current_thread)) {
    Scheduler* const current = Get();
    const SchedTime now = CurrentTime();

    {
      // TODO(eieio,johngro): What is this protecting?
      Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&current->queue_lock_, SOURCE_TAG};

      // Update the virtual timeline in preparation for snapping the thread's
      // virtual finish time to the current virtual time.
      current->UpdateTimeline(now);

      // Set the time slice to expire now.
      current_thread->set_ready();
      current_state->time_slice_ns_ = SchedDuration{0};

      // The thread is re-evaluated with zero lag against other competing threads
      // and may skip lower priority threads with similar arrival times.
      current_state->finish_time_ = current->virtual_time_;
      current_state->fair_.initial_time_slice_ns = current_state->time_slice_ns_;
      current_state->fair_.normalized_timeslice_remainder = SchedRemainder{1};
    }

    current->RescheduleCommon(now, trace.Completer());
  }
}

void Scheduler::Preempt() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_preempt"_stringref};

  Thread* const current_thread = Thread::Current::Get();
  current_thread->get_lock().AssertHeld();
  SchedulerState* const current_state = &current_thread->scheduler_state();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  DEBUG_ASSERT(current_state->curr_cpu_ == current_cpu);
  DEBUG_ASSERT(current_state->last_cpu_ == current_state->curr_cpu_);

  const SchedTime now = CurrentTime();
  current_thread->set_ready();
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Reschedule() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_reschedule"_stringref};

  Thread* const current_thread = Thread::Current::Get();
  current_thread->get_lock().AssertHeld();
  SchedulerState* const current_state = &current_thread->scheduler_state();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  const bool preempt_enabled = current_thread->preemption_state().EvaluateTimesliceExtension();

  // Pend the preemption rather than rescheduling if preemption is disabled or
  // if there is more than one spinlock held.
  // TODO(fxbug.dev/64884): Remove check when spinlocks imply preempt disable.
  if (!preempt_enabled || arch_num_spinlocks_held() > 1 || arch_blocking_disallowed()) {
    current_thread->preemption_state().preempts_pending_add(cpu_num_to_mask(current_cpu));
    return;
  }

  DEBUG_ASSERT(current_state->curr_cpu_ == current_cpu);
  DEBUG_ASSERT(current_state->last_cpu_ == current_state->curr_cpu_);

  const SchedTime now = CurrentTime();
  current_thread->set_ready();
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::RescheduleInternal() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_resched_internal"_stringref};
  Get()->RescheduleCommon(CurrentTime(), trace.Completer());
}

void Scheduler::Migrate(Thread* thread) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_migrate"_stringref};

  thread->get_lock().AssertHeld();

  SchedulerState* const state = &thread->scheduler_state();
  const cpu_mask_t effective_cpu_mask = state->GetEffectiveCpuMask(mp_get_active_mask());
  const cpu_mask_t curr_cpu_mask = cpu_num_to_mask(state->curr_cpu_);
  const cpu_mask_t next_cpu_mask = cpu_num_to_mask(state->next_cpu_);

  const bool stale_curr_cpu = (curr_cpu_mask & effective_cpu_mask) == 0;
  const bool stale_next_cpu =
      state->next_cpu_ != INVALID_CPU && (next_cpu_mask & effective_cpu_mask) == 0;

  // Clear the next CPU if it is no longer in the effective CPU mask. A new value will be
  // determined, if necessary.
  if (stale_next_cpu) {
    state->next_cpu_ = INVALID_CPU;
  }

  cpu_mask_t cpus_to_reschedule_mask = 0;
  if (thread->state() == THREAD_RUNNING && stale_curr_cpu) {
    // The CPU the thread is running on will take care of the actual migration.
    cpus_to_reschedule_mask |= curr_cpu_mask;
  } else if (thread->state() == THREAD_READY && (stale_curr_cpu || stale_next_cpu)) {
    Scheduler* current = Get(state->curr_cpu_);
    const cpu_num_t target_cpu = FindTargetCpu(thread);

    // If the thread has a migration function it will stay on the same CPU until the migration
    // function is called there. Otherwise, the migration is handled here.
    if (target_cpu != state->curr_cpu()) {
      {
        Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&current->queue_lock_, SOURCE_TAG};
        current->EraseFromQueue(thread);
        current->Remove(thread);
      }

      Scheduler* const target = Get(target_cpu);
      {
        Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&target->queue_lock_, SOURCE_TAG};
        target->Insert(CurrentTime(), thread);
      }

      // Reschedule both CPUs to handle the run queue changes.
      cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu) | curr_cpu_mask;
    }
  }

  trace.End();
  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::MigrateUnpinnedThreads() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_migrate_unpinned"_stringref};

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  cpu_mask_t cpus_to_reschedule_mask = 0;

  // Prevent this CPU from being selected as a target for scheduling threads.
  mp_set_curr_cpu_active(false);

  const SchedTime now = CurrentTime();
  Scheduler* const current = Get(current_cpu);

  {
    Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&current->queue_lock_, SOURCE_TAG};

    RunQueue pinned_threads;
    while (!current->fair_run_queue_.is_empty()) {
      Thread* const thread = current->fair_run_queue_.pop_front();

      if (thread->scheduler_state().hard_affinity_ == current_cpu_mask) {
        // Keep track of threads pinned to this CPU.
        pinned_threads.insert(thread);
      } else {
        // Move unpinned threads to another available CPU.
        current->TraceThreadQueueEvent("tqe_deque_migrate_unpinned_fair"_stringref, thread);
        current->Remove(thread);

        queue_guard.CallUnlocked([&] {
          thread_lock.AssertHeld();  // TODO(eieio): HACK!
          thread->CallMigrateFnLocked(Thread::MigrateStage::Before);
          thread->scheduler_state().next_cpu_ = INVALID_CPU;

          const cpu_num_t target_cpu = FindTargetCpu(thread);
          Scheduler* const target = Get(target_cpu);
          DEBUG_ASSERT(target != current);
          cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);

          Guard<MonitoredSpinLock, NoIrqSave> target_queue_guard{&target->queue_lock_, SOURCE_TAG};
          target->Insert(now, thread);
        });
      }
    }

    // Return the pinned threads to the fair run queue.
    current->fair_run_queue_ = ktl::move(pinned_threads);

    while (!current->deadline_run_queue_.is_empty()) {
      Thread* const thread = current->deadline_run_queue_.pop_front();

      if (thread->scheduler_state().hard_affinity_ == current_cpu_mask) {
        // Keep track of threads pinned to this CPU.
        pinned_threads.insert(thread);
      } else {
        // Move unpinned threads to another available CPU.
        current->TraceThreadQueueEvent("tqe_deque_migrate_unpinned_deadline"_stringref, thread);
        current->Remove(thread);

        queue_guard.CallUnlocked([&] {
          thread_lock.AssertHeld();  // TODO(eieio): HACK!
          thread->CallMigrateFnLocked(Thread::MigrateStage::Before);
          thread->scheduler_state().next_cpu_ = INVALID_CPU;

          const cpu_num_t target_cpu = FindTargetCpu(thread);
          Scheduler* const target = Get(target_cpu);
          DEBUG_ASSERT(target != current);
          cpus_to_reschedule_mask |= cpu_num_to_mask(target_cpu);

          Guard<MonitoredSpinLock, NoIrqSave> target_queue_guard{&target->queue_lock_, SOURCE_TAG};
          target->Insert(now, thread);
        });
      }
    }

    // Return the pinned threads to the deadline run queue.
    current->deadline_run_queue_ = ktl::move(pinned_threads);
  }

  // Call all migrate functions for threads last run on the current CPU.
  thread_lock.AssertHeld();  // TODO(eieio): HACK!
  Thread::CallMigrateFnForCpuLocked(current_cpu);

  trace.End();
  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::UpdateWeightCommon(Thread* thread, int original_priority, SchedWeight weight,
                                   cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate) {
  SchedulerState* const state = &thread->scheduler_state();

  switch (thread->state()) {
    case THREAD_INITIAL:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // Adjust the weight of the thread so that the correct value is
      // available when the thread enters the run queue.
      state->discipline_ = SchedDiscipline::Fair;
      state->fair_.weight = weight;
      break;

    case THREAD_RUNNING:
    case THREAD_READY: {
      DEBUG_ASSERT(is_valid_cpu_num(state->curr_cpu_));
      Scheduler* const current = Get(state->curr_cpu_);
      Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&current->queue_lock_, SOURCE_TAG};

      // If the thread is in a run queue, remove it before making subsequent
      // changes to the properties of the thread. Erasing and enqueuing depend
      // on having the current discipline set before hand.
      if (thread->state() == THREAD_READY) {
        DEBUG_ASSERT(state->active());
        current->EraseFromQueue(thread);
        current->TraceThreadQueueEvent("tqe_deque_update_weight"_stringref, thread);
      }

      if (IsDeadlineThread(thread)) {
        // Changed to the fair discipline and update the task counts. Changing
        // from deadline to fair behaves similarly to a yield.
        current->UpdateTotalDeadlineUtilization(-state->deadline_.utilization);
        state->discipline_ = SchedDiscipline::Fair;
        state->start_time_ = current->virtual_time_;
        state->finish_time_ = current->virtual_time_;
        state->time_slice_ns_ = SchedDuration{0};
        state->fair_.initial_time_slice_ns = SchedDuration{0};
        state->fair_.normalized_timeslice_remainder = SchedRemainder{1};
        current->runnable_deadline_task_count_--;
        current->runnable_fair_task_count_++;
      } else {
        // Remove the old weight from the run queue.
        current->weight_total_ -= state->fair_.weight;
      }

      // Update the weight of the thread and the run queue. The time slice
      // of a running thread will be adjusted during reschedule due to the
      // change in demand on the run queue.
      current->weight_total_ += weight;
      state->fair_.weight = weight;

      // Adjust the position of the thread in the run queue based on the new
      // weight.
      if (thread->state() == THREAD_READY) {
        current->QueueThread(thread, Placement::Adjustment);
      }

      *cpus_to_reschedule_mask |= cpu_num_to_mask(state->curr_cpu_);
      break;
    }

    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // Update the weight of the thread blocked in a wait queue. Also
      // handle the race where the thread is no longer in the wait queue
      // but has not yet transitioned to ready.
      state->discipline_ = SchedDiscipline::Fair;
      state->fair_.weight = weight;
      thread_lock.AssertHeld();  // TODO(eieio): HACK!
      thread->wait_queue_state().UpdatePriorityIfBlocked(thread, original_priority, propagate);
      break;

    default:
      break;
  }
}

void Scheduler::UpdateDeadlineCommon(Thread* thread, int original_priority,
                                     const SchedDeadlineParams& params,
                                     cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate) {
  SchedulerState* const state = &thread->scheduler_state();

  switch (thread->state()) {
    case THREAD_INITIAL:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // Adjust the deadline of the thread so that the correct value is
      // available when the thread enters the run queue.
      state->discipline_ = SchedDiscipline::Deadline;
      state->deadline_ = params;
      break;

    case THREAD_RUNNING:
    case THREAD_READY: {
      DEBUG_ASSERT(is_valid_cpu_num(state->curr_cpu_));
      Scheduler* const current = Get(state->curr_cpu_);
      Guard<MonitoredSpinLock, NoIrqSave> queue_guard{&current->queue_lock_, SOURCE_TAG};

      // If the thread is running or is already a deadline task, keep the
      // original arrival time. Otherwise, when moving a ready task from the
      // fair run queue to the deadline run queue, use the current time as the
      // arrival time.
      SchedTime effective_start_time;
      if (IsDeadlineThread(thread)) {
        effective_start_time = state->start_time_;
      } else if (thread->state() == THREAD_RUNNING) {
        effective_start_time = current->start_of_current_time_slice_ns_;
      } else {
        effective_start_time = CurrentTime();
      }

      // If the thread is in a run queue, remove it before making subsequent
      // changes to the properties of the thread. Erasing and enqueuing depend
      // on having the correct discipline set before hand.
      if (thread->state() == THREAD_READY) {
        DEBUG_ASSERT(state->active());
        current->EraseFromQueue(thread);
      }

      const bool was_fair = IsFairThread(thread);
      if (was_fair) {
        // Changed to the deadline discipline and update the task counts and
        // queue weight.
        current->weight_total_ -= state->fair_.weight;
        state->discipline_ = SchedDiscipline::Deadline;
        current->runnable_fair_task_count_--;
        current->runnable_deadline_task_count_++;
      } else {
        // Remove the old utilization from the run queue. Wait to update the
        // exported value until the new value is added below.
        current->total_deadline_utilization_ -= state->deadline_.utilization;
        DEBUG_ASSERT(current->total_deadline_utilization_ >= 0);
      }

      // Update the deadline params and the run queue.
      state->deadline_ = params;
      state->start_time_ = effective_start_time;
      state->finish_time_ = state->start_time_ + params.deadline_ns;
      state->time_slice_ns_ = ktl::min(state->time_slice_ns_, params.capacity_ns);
      current->UpdateTotalDeadlineUtilization(state->deadline_.utilization);

      // The target preemption time orignially set when the thread was fair
      // scheduled does not account for the performance scale applied to the
      // time slice when computing the preemption time for a deadline scheduled
      // thread. There is an assertion that the time slice is expired by the
      // time the target preemption time is reached. Correct the value to avoid
      // failing the consistency check.
      if (thread->state() == THREAD_RUNNING) {
        const SchedDuration scaled_time_slice_ns = current->ScaleUp(state->time_slice_ns_);
        current->target_preemption_time_ns_ = ktl::min<SchedTime>(
            current->start_of_current_time_slice_ns_ + scaled_time_slice_ns, state->finish_time_);

        // A running fair does not update the time slice until re-entering the
        // run queue, unlike a running deadline thread, where the time slice is
        // updated on every reschedule. Ensure the full fair runtime is counted
        // on the next reschedule after switching to deadline.
        if (was_fair) {
          state->last_started_running_ = current->start_of_current_time_slice_ns_;
        }
      }

      // Adjust the position of the thread in the run queue based on the new
      // deadline.
      if (thread->state() == THREAD_READY) {
        current->QueueThread(thread, Placement::Adjustment);
      }

      *cpus_to_reschedule_mask |= cpu_num_to_mask(state->curr_cpu_);
      break;
    }

    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // Update the weight of the thread blocked in a wait queue. Also
      // handle the race where the thread is no longer in the wait queue
      // but has not yet transitioned to ready.
      state->discipline_ = SchedDiscipline::Deadline;
      state->deadline_ = params;
      thread_lock.AssertHeld();  // TODO(eieio): HACK!
      thread->wait_queue_state().UpdatePriorityIfBlocked(thread, original_priority, propagate);
      break;

    default:
      break;
  }
}

void Scheduler::ChangeWeight(Thread* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_change_weight"_stringref};

  thread->get_lock().AssertHeld();
  SchedulerState* const state = &thread->scheduler_state();
  if (thread->IsIdle() || thread->state() == THREAD_DEATH) {
    return;
  }

  // TODO(eieio): The rest of the kernel still uses priority so we have to
  // operate in those terms here. Abstract the notion of priority once the
  // deadline scheduler is available and remove this conversion once the
  // kernel uses the abstraction throughout.
  const int original_priority = state->effective_priority_;
  state->base_priority_ = priority;
  state->effective_priority_ = ktl::max(state->base_priority_, state->inherited_priority_);

  // Perform the state-specific updates if the discipline or effective priority
  // changed.
  if (IsDeadlineThread(thread) || state->effective_priority_ != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(state->effective_priority_),
                       cpus_to_reschedule_mask, PropagatePI::Yes);
  }

  trace.End(original_priority, state->effective_priority_);
}

void Scheduler::ChangeDeadline(Thread* thread, const SchedDeadlineParams& params,
                               cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_change_deadline"_stringref};

  thread->get_lock().AssertHeld();
  SchedulerState* const state = &thread->scheduler_state();
  if (thread->IsIdle() || thread->state() == THREAD_DEATH) {
    return;
  }

  const bool changed = IsFairThread(thread) || state->deadline_ != params;

  // Always set deadline threads to the highest fair priority. This is a
  // workaround until deadline priority inheritance is worked out.
  // TODO(eieio): Replace this with actual deadline PI.
  const int original_priority = state->effective_priority_;
  state->base_priority_ = HIGHEST_PRIORITY;
  state->inherited_priority_ = -1;
  state->effective_priority_ = state->base_priority_;

  // Perform the state-specific updates if the discipline or deadline params changed.
  if (changed) {
    UpdateDeadlineCommon(thread, original_priority, params, cpus_to_reschedule_mask,
                         PropagatePI::Yes);
  }

  trace.End(original_priority, state->effective_priority_);
}

void Scheduler::InheritWeight(Thread* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_inherit_weight"_stringref};

  thread->get_lock().AssertHeld();
  SchedulerState* const state = &thread->scheduler_state();

  // For now deadline threads are logically max weight for the purposes of
  // priority inheritance.
  if (IsDeadlineThread(thread)) {
    return;
  }

  const int original_priority = state->effective_priority_;
  state->inherited_priority_ = priority;
  state->effective_priority_ = ktl::max(state->base_priority_, state->inherited_priority_);

  // Perform the state-specific updates if the effective priority changed.
  if (state->effective_priority_ != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(state->effective_priority_),
                       cpus_to_reschedule_mask, PropagatePI::No);
  }

  trace.End(original_priority, state->effective_priority_);
}

void Scheduler::TimerTick(SchedTime now) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_timer_tick"_stringref};
  Thread::Current::preemption_state().PreemptSetPending();
}

void Scheduler::InheritPriority(Thread* thread, int priority) {
  thread->get_lock().AssertHeld();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  InheritWeight(thread, priority, &cpus_to_reschedule_mask);
  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::ChangePriority(Thread* thread, int priority) {
  thread->get_lock().AssertHeld();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  ChangeWeight(thread, priority, &cpus_to_reschedule_mask);
  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::ChangeDeadline(Thread* thread, const zx_sched_deadline_params_t& params) {
  thread->get_lock().AssertHeld();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  ChangeDeadline(thread, params, &cpus_to_reschedule_mask);
  RescheduleMask(cpus_to_reschedule_mask);
}

zx_time_t Scheduler::GetTargetPreemptionTime() {
  DEBUG_ASSERT(Thread::Current::preemption_state().PreemptDisableCount() > 0);
  Scheduler* const current = Get();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  return current->target_preemption_time_ns_.raw_value();
}

void Scheduler::InitializePerformanceScale(SchedPerformanceScale scale) {
  DEBUG_ASSERT(scale > 0);
  performance_scale_ = scale;
  default_performance_scale_ = scale;
  pending_user_performance_scale_ = scale;
  performance_scale_reciprocal_ = 1 / scale;
}

void Scheduler::UpdatePerformanceScales(zx_cpu_performance_info_t* info, size_t count) {
  DEBUG_ASSERT(count <= percpu::processor_count());
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  cpu_num_t cpus_to_reschedule_mask = 0;
  for (auto& entry : ktl::span{info, count}) {
    DEBUG_ASSERT(entry.logical_cpu_number <= percpu::processor_count());

    cpus_to_reschedule_mask |= cpu_num_to_mask(entry.logical_cpu_number);
    Scheduler* scheduler = Scheduler::Get(entry.logical_cpu_number);

    // TODO(eieio): Apply a minimum value threshold and update the entry if
    // the requested value is below it.
    scheduler->pending_user_performance_scale_ = ToSchedPerformanceScale(entry.performance_scale);

    // Return the original performance scale.
    entry.performance_scale = ToUserPerformanceScale(scheduler->performance_scale());
  }

  RescheduleMask(cpus_to_reschedule_mask);
}

void Scheduler::GetPerformanceScales(zx_cpu_performance_info_t* info, size_t count) {
  DEBUG_ASSERT(count <= percpu::processor_count());
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  for (cpu_num_t i = 0; i < count; i++) {
    Scheduler* scheduler = Scheduler::Get(i);
    info[i].logical_cpu_number = i;
    info[i].performance_scale = ToUserPerformanceScale(scheduler->pending_user_performance_scale_);
  }
}

void Scheduler::GetDefaultPerformanceScales(zx_cpu_performance_info_t* info, size_t count) {
  DEBUG_ASSERT(count <= percpu::processor_count());
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  for (cpu_num_t i = 0; i < count; i++) {
    Scheduler* scheduler = Scheduler::Get(i);
    info[i].logical_cpu_number = i;
    info[i].performance_scale = ToUserPerformanceScale(scheduler->default_performance_scale_);
  }
}
