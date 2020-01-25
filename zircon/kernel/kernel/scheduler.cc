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
#include <kernel/scheduler_internal.h>
#include <kernel/scheduler_state.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/move.h>
#include <vm/vm.h>

using ffl::FromRatio;
using ffl::Round;

// Determines which subset of tracers are enabled when detailed tracing is
// enabled.
#define LOCAL_KTRACE_LEVEL SCHEDULER_TRACING_LEVEL

// The tracing levels used in this compilation unit.
#define KTRACE_COMMON 1
#define KTRACE_FLOW 2
#define KTRACE_DETAILED 3

// Evaluates to true if tracing is enabled for the given level.
#define LOCAL_KTRACE_LEVEL_ENABLED(level) ((LOCAL_KTRACE_LEVEL) >= (level))

#define LOCAL_KTRACE(level, string, args...)                                     \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
               KTRACE_STRING_REF(string), ##args)

#define LOCAL_KTRACE_FLOW_BEGIN(level, string, flow_id)                               \
  ktrace_flow_begin(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
                    KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string), flow_id)

#define LOCAL_KTRACE_FLOW_END(level, string, flow_id)                               \
  ktrace_flow_end(LocalTrace<LOCAL_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Cpu, \
                  KTRACE_GRP_SCHEDULER, KTRACE_STRING_REF(string), flow_id)

template <size_t level>
using LocalTraceDuration = TraceDuration<TraceEnabled<LOCAL_KTRACE_LEVEL_ENABLED(level)>,
                                         KTRACE_GRP_SCHEDULER, TraceContext::Cpu>;

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

// Returns true if the given thread is fair scheduled.
inline bool IsFairThread(const thread_t* thread) {
  return thread->scheduler_state.discipline() == SchedDiscipline::Fair;
}

// Returns true if the given thread is deadline scheduled.
inline bool IsDeadlineThread(const thread_t* thread) {
  return thread->scheduler_state.discipline() == SchedDiscipline::Deadline;
}

// Returns true if the given thread's time slice is adjustable under changes to
// the fair scheduler demand on the CPU.
inline bool IsThreadAdjustable(const thread_t* thread) {
  // Checking the thread state avoids unnecessary adjustments on a thread that
  // is no longer competing.
  // TODO(eieio): Fix thread_is_idle to take a const pointer to the thread.
  return !thread_is_idle(const_cast<thread_t*>(thread)) && IsFairThread(thread) &&
         thread->state == THREAD_READY;
}

// Returns the effective mask of CPUs a thread is may run on, based on the
// thread's affinity masks and available CPUs.
cpu_mask_t GetEffectiveCpuMask(cpu_mask_t active_mask, const thread_t* thread) {
  // The thread may run on any active CPU allowed by both its hard and
  // soft CPU affinity.
  const cpu_mask_t soft_affinity = thread->soft_affinity;
  const cpu_mask_t hard_affinity = thread->hard_affinity;
  const cpu_mask_t available_mask = active_mask & soft_affinity & hard_affinity;

  // Return the mask honoring soft affinity if it is viable, otherwise ignore
  // soft affinity and honor only hard affinity.
  if (likely(available_mask != 0)) {
    return available_mask;
  }

  return active_mask & hard_affinity;
}

}  // anonymous namespace

void Scheduler::Dump() {
  printf("\tweight_total=%#x fair_tasks=%d deadline_tasks=%d vtime=%" PRId64 " period=%" PRId64
         " ema=%" PRId64 " deadline_utilization=%" PRId64 "\n",
         static_cast<uint32_t>(weight_total_.raw_value()), runnable_fair_task_count_,
         runnable_deadline_task_count_, virtual_time_.raw_value(),
         scheduling_period_grans_.raw_value(), total_expected_runtime_ns_.raw_value(),
         total_deadline_utilization_.raw_value());

  if (active_thread_ != nullptr) {
    const SchedulerState& state = active_thread_->scheduler_state;
    if (IsFairThread(active_thread_)) {
      printf("\t-> name=%s weight=%#x start=%" PRId64 " finish=%" PRId64 " ts=%" PRId64
             " ema=%" PRId64 "\n",
             active_thread_->name, static_cast<uint32_t>(state.fair_.weight.raw_value()),
             state.start_time_.raw_value(), state.finish_time_.raw_value(),
             state.time_slice_ns_.raw_value(), state.expected_runtime_ns_.raw_value());
    } else {
      printf("\t-> name=%s deadline=(%" PRId64 ", %" PRId64 ", %" PRId64 ") start=%" PRId64
             " finish=%" PRId64 " ts=%" PRId64 " ema=%" PRId64 "\n",
             active_thread_->name, state.deadline_.capacity_ns.raw_value(),
             state.deadline_.deadline_ns.raw_value(), state.deadline_.period_ns.raw_value(),
             state.start_time_.raw_value(), state.finish_time_.raw_value(),
             state.time_slice_ns_.raw_value(), state.expected_runtime_ns_.raw_value());
    }
  }

  for (const thread_t& thread : deadline_run_queue_) {
    const SchedulerState& state = thread.scheduler_state;
    printf("\t   name=%s deadline=(%" PRId64 ", %" PRId64 ", %" PRId64 ") start=%" PRId64
           " finish=%" PRId64 " ts=%" PRId64 " ema=%" PRId64 "\n",
           thread.name, state.deadline_.capacity_ns.raw_value(),
           state.deadline_.deadline_ns.raw_value(), state.deadline_.period_ns.raw_value(),
           state.start_time_.raw_value(), state.finish_time_.raw_value(),
           state.time_slice_ns_.raw_value(), state.expected_runtime_ns_.raw_value());
  }

  for (const thread_t& thread : fair_run_queue_) {
    const SchedulerState& state = thread.scheduler_state;
    printf("\t   name=%s weight=%#x start=%" PRId64 " finish=%" PRId64 " ts=%" PRId64
           " ema=%" PRId64 "\n",
           thread.name, static_cast<uint32_t>(state.fair_.weight.raw_value()),
           state.start_time_.raw_value(), state.finish_time_.raw_value(),
           state.time_slice_ns_.raw_value(), state.expected_runtime_ns_.raw_value());
  }
}

SchedWeight Scheduler::GetTotalWeight() const {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return weight_total_;
}

size_t Scheduler::GetRunnableTasks() const {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  const int64_t total_runnable_tasks = runnable_fair_task_count_ + runnable_deadline_task_count_;
  return static_cast<size_t>(total_runnable_tasks);
}

// Performs an augmented binary search for the task with the earliest finish
// time that is also equal to or later than the given eligible time.
//
// The tree is ordered by start time and is augmented by maintaining an
// additional invariant: each task node in the tree stores the minimum finish
// time of its descendents, including itself, in addition to its own start and
// finish time. The combination of these three values permits traversinng the
// tree along a perfect partition of minimum finish times with eligible start
// times.
//
// See kernel/scheduler_internal.h for an explanation of how the augmented
// invariant is maintained.
thread_t* Scheduler::FindEarliestEligibleThread(RunQueue* run_queue, SchedTime eligible_time) {
  // Early out if there is no eligible thread.
  if (run_queue->is_empty() || run_queue->front().scheduler_state.start_time_ > eligible_time) {
    return nullptr;
  }

  auto node = run_queue->root();
  auto subtree = run_queue->end();
  auto path = run_queue->end();

  // Descend the tree, with |node| following the path from the root to a leaf,
  // such that the path partitions the tree into two parts: the nodes on the
  // left represent eligible tasks, while the nodes on the right represent tasks
  // that are not eligible. Eligible tasks are both in the left partition and
  // along the search path, tracked by |path|.
  while (node) {
    if (node->scheduler_state.start_time_ <= eligible_time) {
      if (!path || path->scheduler_state.finish_time_ > node->scheduler_state.finish_time_) {
        path = node;
      }

      if (auto left = node.left();
          !subtree || (left && subtree->scheduler_state.min_finish_time_ >
                                   left->scheduler_state.min_finish_time_)) {
        subtree = left;
      }

      node = node.right();
    } else {
      node = node.left();
    }
  }

  if (!subtree || subtree->scheduler_state.min_finish_time_ >= path->scheduler_state.finish_time_) {
    return path.CopyPointer();
  }

  // Find the node with the earliest finish time among the decendents of the
  // subtree with the smallest minimum finish time.
  node = subtree;
  do {
    if (subtree->scheduler_state.min_finish_time_ == node->scheduler_state.finish_time_) {
      return node.CopyPointer();
    }

    if (auto left = node.left();
        left && node->scheduler_state.min_finish_time_ == left->scheduler_state.min_finish_time_) {
      node = left;
    } else {
      node = node.right();
    }
  } while (node);

  return nullptr;
}

Scheduler* Scheduler::Get() { return Get(arch_curr_cpu_num()); }

Scheduler* Scheduler::Get(cpu_num_t cpu) { return &percpu::Get(cpu).scheduler; }

void Scheduler::InitializeThread(thread_t* thread, int priority) {
  new (&thread->scheduler_state) SchedulerState{PriorityToWeight(priority)};
  thread->base_priority = priority;
  thread->effec_priority = priority;
  thread->inherited_priority = -1;
  thread->priority_boost = 0;
  thread->scheduler_state.expected_runtime_ns_ = kDefaultTargetLatency;
}

void Scheduler::InitializeThread(thread_t* thread, const zx_sched_deadline_params_t& params) {
  new (&thread->scheduler_state) SchedulerState{params};
  // Set the numeric priority of the deadline task to the highest as a temporary
  // workaround for the rest of the kernel not knowing about deadlines. This
  // will cause deadline tasks to exert maximum fair scheduler pressure on fair
  // tasks during PI interactions.
  // TODO(eieio): Fix this with an abstraction that the higher layers can use
  // to express priority / deadline more abstractly for PI and etc...
  thread->base_priority = HIGHEST_PRIORITY;
  thread->effec_priority = HIGHEST_PRIORITY;
  thread->inherited_priority = -1;
  thread->priority_boost = 0;
  thread->scheduler_state.expected_runtime_ns_ = SchedDuration{params.capacity};
}

// Dequeues the eligible thread with the earliest virtual finish time. The
// caller must ensure that there is at least one thread in the queue.
thread_t* Scheduler::DequeueFairThread() {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_fair_thread"_stringref};

  // Snap the virtual clock to the earliest start time.
  const auto& earliest_thread = fair_run_queue_.front();
  const auto earliest_start = earliest_thread.scheduler_state.start_time_;
  const SchedTime eligible_time = std::max(virtual_time_, earliest_start);

  // Find the eligible thread with the earliest virtual finish time.
  // Note: Currently, fair tasks are always eligible when added to the run
  // queue, such that this search is equivalent to taking the front element of
  // a tree sorted by finish time, instead of start time. However, when moving
  // to the WF2Q algorithm, eligibility becomes a factor. Using the eligibility
  // query now prepares for migrating the algorithm and also avoids having two
  // different template instantiations of fbl::WAVLTree to support the fair and
  // deadline disciplines.
  thread_t* const eligible_thread = FindEarliestEligibleThread(&fair_run_queue_, eligible_time);
  DEBUG_ASSERT_MSG(eligible_thread != nullptr,
                   "virtual_time=%" PRId64 ", eligible_time=%" PRId64 " , start_time=%" PRId64
                   ", finish_time=%" PRId64 ", min_finish_time=%" PRId64 "!",
                   virtual_time_.raw_value(), eligible_time.raw_value(),
                   earliest_thread.scheduler_state.start_time_.raw_value(),
                   earliest_thread.scheduler_state.finish_time_.raw_value(),
                   earliest_thread.scheduler_state.min_finish_time_.raw_value());

  virtual_time_ = eligible_time;
  return fair_run_queue_.erase(*eligible_thread);
}

// Dequeues the eligible thread with the earliest deadline. The caller must
// ensure that there is at least one eligible thread in the queue.
thread_t* Scheduler::DequeueDeadlineThread(SchedTime eligible_time) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_deadline_thread"_stringref};

  thread_t* const eligible_thread = FindEarliestEligibleThread(&deadline_run_queue_, eligible_time);
  DEBUG_ASSERT_MSG(eligible_thread != nullptr,
                   "eligible_time=%" PRId64 ", start_time=%" PRId64 ", finish_time=%" PRId64
                   ", min_finish_time=%" PRId64 "!",
                   eligible_time.raw_value(),
                   eligible_thread->scheduler_state.start_time_.raw_value(),
                   eligible_thread->scheduler_state.finish_time_.raw_value(),
                   eligible_thread->scheduler_state.min_finish_time_.raw_value());

  deadline_run_queue_.erase(*eligible_thread);

  const SchedulerState& state = eligible_thread->scheduler_state;
  trace.End(Round<uint64_t>(state.start_time_), Round<uint64_t>(state.finish_time_));
  return eligible_thread;
}

// Returns the eligible thread with the earliest deadline that is also earlier
// than the given deadline. Returns nullptr if no threads meet this criteria or
// the run queue is empty.
thread_t* Scheduler::FindEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time) {
  thread_t* const eligible_thread = FindEarliestEligibleThread(&deadline_run_queue_, eligible_time);
  const bool found_earlier_deadline =
      eligible_thread && eligible_thread->scheduler_state.finish_time_ < finish_time;
  return found_earlier_deadline ? eligible_thread : nullptr;
}

// Returns the time that the next deadline task will become eligible or infinite
// if there are no ready deadline tasks.
SchedTime Scheduler::GetNextEligibleTime() {
  return deadline_run_queue_.is_empty() ? SchedTime{ZX_TIME_INFINITE}
                                        : deadline_run_queue_.front().scheduler_state.start_time_;
}

// Dequeues the eligible thread with the earliest deadline that is also earlier
// than the given deadline. Returns nullptr if no threads meet the criteria or
// the run queue is empty.
thread_t* Scheduler::DequeueEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"dequeue_earlier_deadline_thread"_stringref};
  thread_t* const eligible_thread = FindEarlierDeadlineThread(eligible_time, finish_time);
  return eligible_thread ? deadline_run_queue_.erase(*eligible_thread) : nullptr;
}

// Updates the system load metrics. Updates happen only when the active thread
// changes or the time slice expires.
void Scheduler::UpdateCounters(SchedDuration queue_time_ns) {
  demand_counter.Add(weight_total_.raw_value());
  runnable_counter.Add(runnable_fair_task_count_ + runnable_deadline_task_count_);
  latency_counter.Add(queue_time_ns.raw_value());
  samples_counter.Add(1);
}

// Selects a thread to run. Performs any necessary maintenanace if the current
// thread is changing, depending on the reason for the change.
thread_t* Scheduler::EvaluateNextThread(SchedTime now, thread_t* current_thread,
                                        bool timeslice_expired, SchedDuration total_runtime_ns) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"find_thread"_stringref};

  const bool is_idle = thread_is_idle(current_thread);
  const bool is_active = current_thread->state == THREAD_READY;
  const bool is_deadline = IsDeadlineThread(current_thread);
  const bool is_new_deadline_eligible = IsDeadlineThreadEligible(now);

  const cpu_num_t current_cpu = arch_curr_cpu_num();
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();

  const bool needs_migration =
      (GetEffectiveCpuMask(active_mask, current_thread) & current_cpu_mask) == 0;

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
    if (timeslice_expired) {
      // If the timeslice expired insert the current thread into the run queue.
      QueueThread(current_thread, Placement::Insertion, now, total_runtime_ns);
    } else if (is_new_deadline_eligible && is_deadline) {
      // The current thread is deadline scheduled and there is at least one
      // eligible deadline thread in the run queue: select the eligible thread
      // with the earliest deadline, which may still be the current thread.
      const SchedTime deadline_ns = current_thread->scheduler_state.finish_time_;
      if (thread_t* const earlier_thread = DequeueEarlierDeadlineThread(now, deadline_ns);
          earlier_thread != nullptr) {
        QueueThread(current_thread, Placement::Preemption, now, total_runtime_ns);
        return earlier_thread;
      }

      // The current thread still has the earliest deadline.
      return current_thread;
    } else if (is_new_deadline_eligible && !is_deadline) {
      // The current thread is fair scheduled and there is at least one eligible
      // deadline thread in the run queue: return this thread to the run queue.
      QueueThread(current_thread, Placement::Preemption, now, total_runtime_ns);
    } else {
      // The current thread has remaining time and no eligible contender.
      return current_thread;
    }
  } else if (!is_active && likely(!is_idle)) {
    // The current thread is no longer ready, remove its accounting.
    Remove(current_thread);
  }

  // The current thread is no longer running or has returned to the run queue,
  // select another thread to run. If there is an eligible deadline thread, it
  // takes precedence over available fair threads.
  //
  // Note the that predicates in this block must be evaluated here, since the
  // operations above may change the queues and invalidate the predicates
  // evaluated at the start of this method.
  if (IsDeadlineThreadEligible(now)) {
    return DequeueDeadlineThread(now);
  } else if (likely(!fair_run_queue_.is_empty())) {
    return DequeueFairThread();
  } else {
    return &percpu::Get(this_cpu()).idle_thread;
  }
}

cpu_num_t Scheduler::FindTargetCpu(thread_t* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"find_target: cpu,avail"_stringref};

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  const cpu_mask_t last_cpu_mask = cpu_num_to_mask(thread->last_cpu);
  const cpu_mask_t active_mask = mp_get_active_mask();
  const cpu_mask_t idle_mask = mp_get_idle_mask();

  // Determine the set of CPUs the thread is allowed to run on.
  //
  // Threads may be created and resumed before the thread init level. Work around
  // an empty active mask by assuming the current cpu is scheduleable.
  const cpu_mask_t available_mask =
      active_mask != 0 ? GetEffectiveCpuMask(active_mask, thread) : current_cpu_mask;
  DEBUG_ASSERT_MSG(available_mask != 0,
                   "thread=%s affinity=%#x soft_affinity=%#x active=%#x "
                   "idle=%#x arch_ints_disabled=%d",
                   thread->name, thread->hard_affinity, thread->soft_affinity, active_mask,
                   mp_get_idle_mask(), arch_ints_disabled());

  LOCAL_KTRACE(KTRACE_DETAILED, "target_mask: online,active", mp_get_online_mask(), active_mask);

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
  // TODO(eieio): Add some sort of threshold to terminate search when a
  // sufficiently unloaded target is found.

  const auto compare_fair = [](Scheduler* const queue_a,
                               Scheduler* const queue_b) TA_REQ(thread_lock) {
    if (queue_a->total_expected_runtime_ns_ == queue_b->total_expected_runtime_ns_) {
      return queue_a->total_deadline_utilization_ < queue_b->total_deadline_utilization_;
    }
    return queue_a->total_expected_runtime_ns_ < queue_b->total_expected_runtime_ns_;
  };
  const auto is_idle_fair = [](Scheduler* const queue) TA_REQ(thread_lock) {
    return queue->total_expected_runtime_ns_ == SchedDuration{0};
  };

  const auto compare_deadline = [](Scheduler* const queue_a,
                                   Scheduler* const queue_b) TA_REQ(thread_lock) {
    if (queue_a->total_deadline_utilization_ == queue_b->total_deadline_utilization_) {
      return queue_a->total_expected_runtime_ns_ < queue_b->total_expected_runtime_ns_;
    }
    return queue_a->total_deadline_utilization_ < queue_b->total_deadline_utilization_;
  };
  const auto is_idle_deadline = [](Scheduler* const queue) TA_REQ(thread_lock) {
    return queue->total_deadline_utilization_ == SchedUtilization{0} &&
           queue->total_expected_runtime_ns_ == SchedDuration{0};
  };

  const auto compare = IsFairThread(thread) ? compare_fair : compare_deadline;
  const auto is_idle = IsFairThread(thread) ? is_idle_fair : is_idle_deadline;

  cpu_mask_t remaining_mask = available_mask & ~cpu_num_to_mask(target_cpu);
  while (remaining_mask != 0 && !is_idle(target_queue)) {
    const cpu_num_t candidate_cpu = lowest_cpu_set(remaining_mask);
    Scheduler* const candidate_queue = Get(candidate_cpu);

    if (compare(candidate_queue, target_queue)) {
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
  LocalTraceDuration<KTRACE_DETAILED> trace{"update_vtime"_stringref};

  const auto runtime_ns = now - last_update_time_ns_;
  last_update_time_ns_ = now;

  if (weight_total_ > SchedWeight{0}) {
    virtual_time_ += runtime_ns;
  }

  trace.End(Round<uint64_t>(runtime_ns), Round<uint64_t>(virtual_time_));
}

void Scheduler::RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"reschedule_common"_stringref};

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

  const SchedDuration total_runtime_ns = now - start_of_current_time_slice_ns_;
  const SchedDuration actual_runtime_ns = now - current_thread->last_started_running;
  current_thread->last_started_running = now.raw_value();

  // Update the runtime accounting for the thread that just ran.
  current_thread->runtime_ns += actual_runtime_ns;

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
      const SchedTime slice_deadline_ns = start_of_current_time_slice_ns_ + remaining_time_slice_ns;
      absolute_deadline_ns_ = ClampToDeadline(slice_deadline_ns);
      timer_preempt_reset(absolute_deadline_ns_.raw_value());
    }

    current_state->fair_.initial_time_slice_ns = time_slice_ns;
    current_state->time_slice_ns_ = remaining_time_slice_ns;
    trace_adjust_rate.End(Round<uint64_t>(remaining_time_slice_ns),
                          Round<uint64_t>(total_runtime_ns));
  }

  const bool timeslice_expired = total_runtime_ns >= current_state->time_slice_ns_;

  // Select a thread to run.
  thread_t* const next_thread =
      EvaluateNextThread(now, current_thread, timeslice_expired, total_runtime_ns);
  DEBUG_ASSERT(next_thread != nullptr);

  SCHED_LTRACEF("current={%s, %s} next={%s, %s} expired=%d total_runtime_ns=%" PRId64
                " fair_front=%s deadline_front=%s\n",
                current_thread->name, ToString(current_thread->state), next_thread->name,
                ToString(next_thread->state), timeslice_expired, total_runtime_ns.raw_value(),
                fair_run_queue_.is_empty() ? "[none]" : fair_run_queue_.front().name,
                deadline_run_queue_.is_empty() ? "[none]" : deadline_run_queue_.front().name);

  // Update the state of the current and next thread.
  current_thread->preempt_pending = false;
  next_thread->state = THREAD_RUNNING;
  next_thread->last_cpu = current_cpu;
  next_thread->curr_cpu = current_cpu;

  active_thread_ = next_thread;

  // Update the expected runtime of the current thread and the per-CPU totals.
  if (!thread_is_idle(current_thread) && (timeslice_expired || current_thread != next_thread)) {
    LocalTraceDuration<KTRACE_DETAILED> update_ema_trace{"update_expected_runtime: rt, drt"_stringref};

    // The expected runtime is an exponential moving average updated as follows:
    //
    // a = 1 / 2**d
    // Sn = Sn-1 + a * (Yn - Sn-1)
    //    = Sn-1 + (Yn - Sn-1) >> d
    //
    const SchedDuration delta_ns = total_runtime_ns - current_state->expected_runtime_ns_;
    const SchedDuration scaled_ns = delta_ns / (1 << kExpectedRuntimeAdjustmentRateShift);
    const SchedDuration clamped_ns =
        std::max<SchedDuration>(scaled_ns, -current_state->expected_runtime_ns_);
    current_state->expected_runtime_ns_ += clamped_ns;

    // Update the CPU totals if the current thread is still associated with this
    // CPU.
    if (current_state->active() && current_thread->curr_cpu == current_cpu) {
      total_expected_runtime_ns_ += clamped_ns;
    }

    update_ema_trace.End(Round<uint64_t>(total_expected_runtime_ns_),
                         Round<uint64_t>(total_deadline_utilization_));
  }

  // Always call to handle races between reschedule IPIs and changes to the run
  // queue.
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
    LocalTraceDuration<KTRACE_DETAILED> trace_stop_preemption{"idle"_stringref};
    SCHED_LTRACEF("Idle: current=%s next=%s\n", current_thread->name, next_thread->name);
    UpdateCounters(SchedDuration{0});
    next_thread->last_started_running = now.raw_value();

    // If there are no tasks to run in the future, disable the preemption timer.
    // Otherwise, set the preemption time to the earliest eligible time.
    if (deadline_run_queue_.is_empty()) {
      timer_preempt_cancel();
    } else {
      const auto& earliest_thread = deadline_run_queue_.front();
      absolute_deadline_ns_ = earliest_thread.scheduler_state.start_time_;
      timer_preempt_reset(absolute_deadline_ns_.raw_value());
    }
  } else if (timeslice_expired || next_thread != current_thread) {
    LocalTraceDuration<KTRACE_DETAILED> trace_start_preemption{"next_slice: now,deadline"_stringref};

    // Re-compute the time slice and deadline for the new thread based on the
    // latest state.
    absolute_deadline_ns_ = NextThreadTimeslice(next_thread, now);

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

    SCHED_LTRACEF("Start preempt timer: current=%s next=%s now=%" PRId64 " deadline=%" PRId64 "\n",
                  current_thread->name, next_thread->name, now.raw_value(),
                  absolute_deadline_ns_.raw_value());
    timer_preempt_reset(absolute_deadline_ns_.raw_value());

    trace_start_preemption.End(Round<uint64_t>(now), Round<uint64_t>(absolute_deadline_ns_));

    // Emit a flow end event to match the flow begin event emitted when the
    // thread was enqueued. Emitting in this scope ensures that thread just
    // came from the run queue (and is not the idle thread).
    LOCAL_KTRACE_FLOW_END(KTRACE_FLOW, "sched_latency", FlowIdFromThreadGeneration(next_thread));
  } else if (const SchedTime eligible_time_ns = GetNextEligibleTime();
             eligible_time_ns < absolute_deadline_ns_) {
    absolute_deadline_ns_ = eligible_time_ns;
    timer_preempt_reset(absolute_deadline_ns_.raw_value());
  }

  if (next_thread != current_thread) {
    LOCAL_KTRACE(KTRACE_DETAILED, "reschedule current: count,slice",
                 runnable_fair_task_count_ + runnable_deadline_task_count_,
                 Round<uint64_t>(current_thread->scheduler_state.time_slice_ns_));
    LOCAL_KTRACE(KTRACE_DETAILED, "reschedule next: wsum,slice", weight_total_.raw_value(),
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
  LocalTraceDuration<KTRACE_DETAILED> trace{"update_period"_stringref};

  DEBUG_ASSERT(runnable_fair_task_count_ >= 0);
  DEBUG_ASSERT(minimum_granularity_ns_ > 0);
  DEBUG_ASSERT(peak_latency_grans_ > 0);
  DEBUG_ASSERT(target_latency_grans_ > 0);

  const int64_t num_tasks = runnable_fair_task_count_;
  const int64_t peak_tasks = Round<int64_t>(peak_latency_grans_);
  const int64_t normal_tasks = Round<int64_t>(target_latency_grans_);

  // The scheduling period stretches when there are too many tasks to fit
  // within the target latency.
  scheduling_period_grans_ = SchedDuration{num_tasks > normal_tasks ? num_tasks : normal_tasks};

  SCHED_LTRACEF("num_tasks=%" PRId64 " peak_tasks=%" PRId64 " normal_tasks=%" PRId64
                " period_grans=%" PRId64 "\n",
                num_tasks, peak_tasks, normal_tasks, scheduling_period_grans_.raw_value());

  trace.End(Round<uint64_t>(scheduling_period_grans_), num_tasks);
}

SchedDuration Scheduler::CalculateTimeslice(thread_t* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"calculate_timeslice: w,wt"_stringref};
  SchedulerState* const state = &thread->scheduler_state;

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
  return std::min(completion_time, GetNextEligibleTime());
}

SchedTime Scheduler::ClampToEarlierDeadline(SchedTime completion_time, SchedTime finish_time) {
  thread_t* const thread = FindEarlierDeadlineThread(completion_time, finish_time);
  return thread ? std::min(completion_time, thread->scheduler_state.start_time_) : completion_time;
}

SchedTime Scheduler::NextThreadTimeslice(thread_t* thread, SchedTime now) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"next_timeslice: t,abs"_stringref};

  SchedulerState* const state = &thread->scheduler_state;
  SchedTime absolute_deadline_ns;

  if (IsFairThread(thread)) {
    // Calculate the next time slice and the deadline when the time slice is
    // completed.
    const SchedDuration time_slice_ns = CalculateTimeslice(thread);
    const SchedDuration remaining_time_slice_ns =
        time_slice_ns * state->fair_.normalized_timeslice_remainder;

    DEBUG_ASSERT(time_slice_ns > SchedDuration{0});
    DEBUG_ASSERT(remaining_time_slice_ns > SchedDuration{0});

    state->fair_.initial_time_slice_ns = time_slice_ns;
    state->time_slice_ns_ = remaining_time_slice_ns;

    const SchedTime slice_deadline_ns = now + remaining_time_slice_ns;
    absolute_deadline_ns = ClampToDeadline(slice_deadline_ns);

    DEBUG_ASSERT_MSG(state->time_slice_ns_ > SchedDuration{0} && absolute_deadline_ns > now,
                     "time_slice_ns=%" PRId64 " now=%" PRId64 " absolute_deadline_ns=%" PRId64,
                     state->time_slice_ns_.raw_value(), now.raw_value(),
                     absolute_deadline_ns.raw_value());

    SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x time_slice_ns=%" PRId64 "\n", thread->name,
                  static_cast<uint32_t>(weight_total_.raw_value()),
                  static_cast<uint32_t>(state->fair_.weight.raw_value()),
                  state->time_slice_ns_.raw_value());
  } else {
    // Calculate the deadline when the remaining time slice is completed. The
    // time slice is maintained by the deadline queuing logic, no need to update
    // it here.
    const SchedTime slice_deadline_ns = now + state->time_slice_ns_;
    absolute_deadline_ns = ClampToEarlierDeadline(slice_deadline_ns, state->finish_time_);

    SCHED_LTRACEF("name=%s capacity=%" PRId64 " deadline=%" PRId64 " period=%" PRId64
                  " time_slice_ns=%" PRId64 "\n",
                  thread->name, state->deadline_.capacity_ns.raw_value(),
                  state->deadline_.deadline_ns.raw_value(), state->deadline_.period_ns.raw_value(),
                  state->time_slice_ns_.raw_value());
  }

  trace.End(Round<uint64_t>(state->time_slice_ns_), Round<uint64_t>(absolute_deadline_ns));
  return absolute_deadline_ns;
}

void Scheduler::QueueThread(thread_t* thread, Placement placement, SchedTime now,
                            SchedDuration total_runtime_ns) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"queue_thread: s,f"_stringref};

  DEBUG_ASSERT(thread->state == THREAD_READY);
  DEBUG_ASSERT(!thread_is_idle(thread));
  SCHED_LTRACEF("QueueThread: thread=%s\n", thread->name);

  SchedulerState* const state = &thread->scheduler_state;

  // Account for the consumed time slice. The consumed time is zero when the
  // thread is unblocking, migrating, or adjusting queue position. The
  // remaining time slice may become negative due to scheduler overhead.
  state->time_slice_ns_ -= total_runtime_ns;

  if (IsFairThread(thread)) {
    // Compute the ratio of remaining time slice to ideal time slice. This may
    // be less than 1.0 due to time slice consumed or due to previous preemption
    // by a deadline task or both.
    const SchedRemainder normalized_timeslice_remainder =
        state->time_slice_ns_ / std::max(state->fair_.initial_time_slice_ns, SchedDuration{1});

    DEBUG_ASSERT_MSG(
        normalized_timeslice_remainder <= SchedRemainder{1},
        "time_slice_ns=%" PRId64 " initial_time_slice_ns=%" PRId64 " remainder=%" PRId64 "\n",
        state->time_slice_ns_.raw_value(), state->fair_.initial_time_slice_ns.raw_value(),
        normalized_timeslice_remainder.raw_value());

    if (placement == Placement::Insertion || normalized_timeslice_remainder <= SchedRemainder{0}) {
      state->start_time_ = std::max(state->finish_time_, virtual_time_);
      state->fair_.normalized_timeslice_remainder = SchedRemainder{1};
    } else if (placement == Placement::Preemption) {
      DEBUG_ASSERT(state->time_slice_ns_ > SchedDuration{0});
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

    fair_run_queue_.insert(thread);
    LOCAL_KTRACE(KTRACE_DETAILED, "queue_thread");
  } else {
    if (placement == Placement::Insertion) {
      LocalTraceDuration<KTRACE_DETAILED> insert_trace{"insert_deadline: r,c"_stringref};

      // Determine how much time is left before the deadline. This might be less
      // than the remaining time slice or negative if the thread blocked.
      const SchedDuration time_until_deadline_ns = state->finish_time_ - now;
      if (time_until_deadline_ns <= SchedDuration{0} || state->time_slice_ns_ <= SchedDuration{0}) {
        const SchedTime period_finish_ns = state->start_time_ + state->deadline_.period_ns;

        state->start_time_ = now >= period_finish_ns ? now : period_finish_ns;
        state->finish_time_ = state->start_time_ + state->deadline_.deadline_ns;
        state->time_slice_ns_ = state->deadline_.capacity_ns;
      } else if (state->time_slice_ns_ >= time_until_deadline_ns) {
        state->time_slice_ns_ = time_until_deadline_ns;
      }
      DEBUG_ASSERT(state->time_slice_ns_ >= SchedDuration{0});

      insert_trace.End(Round<uint64_t>(time_until_deadline_ns),
                       Round<uint64_t>(state->time_slice_ns_));
    } else if (placement == Placement::Preemption) {
      LocalTraceDuration<KTRACE_DETAILED> preemption_trace{"preemption_deadline: r,c"_stringref};
      const SchedDuration time_until_deadline_ns = state->finish_time_ - now;
      preemption_trace.End(Round<uint64_t>(time_until_deadline_ns),
                           Round<uint64_t>(state->time_slice_ns_));
    }

    DEBUG_ASSERT_MSG(state->start_time_ < state->finish_time_,
                     "start=%" PRId64 " finish=%" PRId64 " capacity=%" PRId64 "\n",
                     state->start_time_.raw_value(), state->finish_time_.raw_value(),
                     state->time_slice_ns_.raw_value());

    deadline_run_queue_.insert(thread);
    LOCAL_KTRACE(KTRACE_DETAILED, "queue_thread");
  }

  // Only update the generation, enqueue time, and emit a flow event if this
  // is an insertion or preemption. In constrast, an adjustment only changes the
  // queue position due to a parameter change and should not perform these
  // actions.
  if (placement != Placement::Adjustment) {
    // Reuse this member to track the time the thread enters the run queue.
    // It is not read outside of the scheduler unless the thread state is
    // THREAD_RUNNING.
    thread->last_started_running = now.raw_value();
    thread->scheduler_state.generation_ = ++generation_count_;
    LOCAL_KTRACE_FLOW_BEGIN(KTRACE_FLOW, "sched_latency", FlowIdFromThreadGeneration(thread));
  }

  trace.End(Round<uint64_t>(state->start_time_), Round<uint64_t>(state->finish_time_));
}

void Scheduler::Insert(SchedTime now, thread_t* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"insert"_stringref};

  DEBUG_ASSERT(thread->state == THREAD_READY);
  DEBUG_ASSERT(!thread_is_idle(thread));

  SchedulerState* const state = &thread->scheduler_state;

  // Ensure insertion happens only once, even if Unblock is called multiple times.
  if (state->OnInsert()) {
    // Insertion can happen from a different CPU. Set the thread's current
    // CPU to the one this scheduler instance services.
    thread->curr_cpu = this_cpu();

    total_expected_runtime_ns_ += state->expected_runtime_ns_;
    DEBUG_ASSERT(total_expected_runtime_ns_ > SchedDuration{0});

    if (IsFairThread(thread)) {
      runnable_fair_task_count_++;
      DEBUG_ASSERT(runnable_fair_task_count_ > 0);

      UpdateTimeline(now);
      UpdatePeriod();

      weight_total_ += state->fair_.weight;
      DEBUG_ASSERT(weight_total_ > SchedWeight{0});
    } else {
      total_deadline_utilization_ += state->deadline_.utilization;
      DEBUG_ASSERT(total_deadline_utilization_ > SchedUtilization{0});

      runnable_deadline_task_count_++;
      DEBUG_ASSERT(runnable_deadline_task_count_ != 0);
    }

    QueueThread(thread, Placement::Insertion, now);
  }
}

void Scheduler::Remove(thread_t* thread) {
  LocalTraceDuration<KTRACE_DETAILED> trace{"remove"_stringref};

  DEBUG_ASSERT(!thread_is_idle(thread));

  SchedulerState* const state = &thread->scheduler_state;
  DEBUG_ASSERT(!state->InQueue());

  // Ensure that removal happens only once, even if Block() is called multiple times.
  if (state->OnRemove()) {
    thread->curr_cpu = INVALID_CPU;

    DEBUG_ASSERT(total_expected_runtime_ns_ > SchedDuration{0});
    total_expected_runtime_ns_ -= state->expected_runtime_ns_;

    if (IsFairThread(thread)) {
      DEBUG_ASSERT(runnable_fair_task_count_ > 0);
      runnable_fair_task_count_--;

      UpdatePeriod();

      state->start_time_ = SchedNs(0);
      state->finish_time_ = SchedNs(0);

      weight_total_ -= state->fair_.weight;
      DEBUG_ASSERT(weight_total_ >= SchedWeight{0});

      SCHED_LTRACEF("name=%s weight_total=%#x weight=%#x\n", thread->name,
                    static_cast<uint32_t>(weight_total_.raw_value()),
                    static_cast<uint32_t>(state->fair_.weight.raw_value()));
    } else {
      DEBUG_ASSERT(total_deadline_utilization_ > SchedUtilization{0});
      total_deadline_utilization_ -= state->deadline_.utilization;

      DEBUG_ASSERT(runnable_deadline_task_count_ > 0);
      runnable_deadline_task_count_--;
    }
  }
}

void Scheduler::Block() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_block"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* const current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state != THREAD_RUNNING);

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%" PRId64 "\n", current_thread->name, now.raw_value());

  Scheduler::Get()->RescheduleCommon(now, trace.Completer());
}

bool Scheduler::Unblock(thread_t* thread) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_unblock"_stringref};

  DEBUG_ASSERT(thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("thread=%s now=%" PRId64 "\n", thread->name, now.raw_value());

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
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_unblock_list"_stringref};

  DEBUG_ASSERT(list);
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  const SchedTime now = CurrentTime();

  cpu_mask_t cpus_to_reschedule_mask = 0;
  thread_t* thread;
  while ((thread = list_remove_tail_type(list, thread_t, queue_node)) != nullptr) {
    DEBUG_ASSERT(thread->magic == THREAD_MAGIC);
    DEBUG_ASSERT(!thread_is_idle(thread));

    SCHED_LTRACEF("thread=%s now=%" PRId64 "\n", thread->name, now.raw_value());

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

  SCHED_LTRACEF("thread=%s now=%" PRId64 "\n", thread->name, current_time());

  thread->state = THREAD_READY;
  thread->curr_cpu = lowest_cpu_set(thread->hard_affinity);
}

void Scheduler::Yield() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_yield"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* const current_thread = get_current_thread();
  SchedulerState* const current_state = &current_thread->scheduler_state;
  DEBUG_ASSERT(!thread_is_idle(current_thread));

  Scheduler* const current = Get();
  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%" PRId64 "\n", current_thread->name, now.raw_value());

  // Set the time slice to expire now.
  current_thread->state = THREAD_READY;
  current_state->time_slice_ns_ = now - current->start_of_current_time_slice_ns_;
  DEBUG_ASSERT(current_state->time_slice_ns_ >= 0);

  if (IsFairThread(current_thread)) {
    // Update the virtual timeline in preparation for snapping the thread's
    // virtual finish time to the current virtual time.
    current->UpdateTimeline(now);

    // The thread is re-evaluated with zero lag against other competing threads
    // and may skip lower priority threads with similar arrival times.
    current_state->finish_time_ = current->virtual_time_;
    current_state->fair_.initial_time_slice_ns = current_state->time_slice_ns_;
    current_state->fair_.normalized_timeslice_remainder = SchedRemainder{1};
  }

  current->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Preempt() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_preempt"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  thread_t* current_thread = get_current_thread();
  const cpu_num_t current_cpu = arch_curr_cpu_num();

  DEBUG_ASSERT(current_thread->curr_cpu == current_cpu);
  DEBUG_ASSERT(current_thread->last_cpu == current_thread->curr_cpu);

  const SchedTime now = CurrentTime();
  SCHED_LTRACEF("current=%s now=%" PRId64 "\n", current_thread->name, now.raw_value());

  current_thread->state = THREAD_READY;
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::Reschedule() {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_reschedule"_stringref};

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
  SCHED_LTRACEF("current=%s now=%" PRId64 "\n", current_thread->name, now.raw_value());

  current_thread->state = THREAD_READY;
  Get()->RescheduleCommon(now, trace.Completer());
}

void Scheduler::RescheduleInternal() { Get()->RescheduleCommon(CurrentTime()); }

void Scheduler::Migrate(thread_t* thread) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_migrate"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  cpu_mask_t cpus_to_reschedule_mask = 0;

  if (thread->state == THREAD_RUNNING) {
    const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
    if (!(GetEffectiveCpuMask(mp_get_active_mask(), thread) & thread_cpu_mask)) {
      // Mark the CPU the thread is running on for reschedule. The
      // scheduler on that CPU will take care of the actual migration.
      cpus_to_reschedule_mask |= thread_cpu_mask;
    }
  } else if (thread->state == THREAD_READY) {
    const cpu_mask_t thread_cpu_mask = cpu_num_to_mask(thread->curr_cpu);
    if (!(GetEffectiveCpuMask(mp_get_active_mask(), thread) & thread_cpu_mask)) {
      Scheduler* current = Get(thread->curr_cpu);

      DEBUG_ASSERT(thread->scheduler_state.InQueue());
      current->GetRunQueue(thread).erase(*thread);
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
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_migrate_unpinned"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  DEBUG_ASSERT(current_cpu == arch_curr_cpu_num());

  // Prevent this CPU from being selected as a target for scheduling threads.
  mp_set_curr_cpu_active(false);

  const SchedTime now = CurrentTime();
  Scheduler* const current = Get(current_cpu);
  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(current_cpu);

  RunQueue pinned_threads;
  cpu_mask_t cpus_to_reschedule_mask = 0;
  while (!current->fair_run_queue_.is_empty()) {
    thread_t* const thread = current->fair_run_queue_.pop_front();

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

  // Return the pinned threads to the fair run queue.
  current->fair_run_queue_ = ktl::move(pinned_threads);

  while (!current->deadline_run_queue_.is_empty()) {
    thread_t* const thread = current->deadline_run_queue_.pop_front();

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

  // Return the pinned threads to the deadline run queue.
  current->deadline_run_queue_ = ktl::move(pinned_threads);

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
      state->discipline_ = SchedDiscipline::Fair;
      state->fair_.weight = weight;
      break;

    case THREAD_RUNNING:
    case THREAD_READY: {
      DEBUG_ASSERT(is_valid_cpu_num(thread->curr_cpu));
      Scheduler* const current = Get(thread->curr_cpu);

      // If the thread is in a run queue, remove it before making subsequent
      // changes to the properties of the thread. Erasing and enqueuing depend
      // on having the currect discipline set before hand.
      if (thread->state == THREAD_READY) {
        DEBUG_ASSERT(state->InQueue());
        DEBUG_ASSERT(state->active());
        current->GetRunQueue(thread).erase(*thread);
      }

      if (IsDeadlineThread(thread)) {
        // Changed to the fair discipline and update the task counts. Changing
        // from deadline to fair behaves similarly to a yield.
        current->total_deadline_utilization_ -= state->deadline_.utilization;
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
      if (thread->state == THREAD_READY) {
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
      state->discipline_ = SchedDiscipline::Fair;
      state->fair_.weight = weight;
      if (thread->blocking_wait_queue) {
        wait_queue_priority_changed(thread, original_priority, propagate);
      }
      break;

    default:
      break;
  }
}

void Scheduler::UpdateDeadlineCommon(thread_t* thread, int original_priority,
                                     const SchedDeadlineParams& params,
                                     cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate) {
  SchedulerState* const state = &thread->scheduler_state;

  switch (thread->state) {
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
      DEBUG_ASSERT(is_valid_cpu_num(thread->curr_cpu));
      Scheduler* const current = Get(thread->curr_cpu);

      // If the thread is running or is already a deadline task, keep the
      // original arrival time. Otherwise, when moving a ready task from the
      // fair run queue to the deadline run queue, use the current time as the
      // arrival time.
      SchedTime effective_start_time;
      if (IsDeadlineThread(thread)) {
        effective_start_time = state->start_time_;
      } else if (thread->state == THREAD_RUNNING) {
        effective_start_time = current->start_of_current_time_slice_ns_;
      } else {
        effective_start_time = CurrentTime();
      }

      // If the thread is in a run queue, remove it before making subsequent
      // changes to the properties of the thread. Erasing and enqueuing depend
      // on having the currect discipline set before hand.
      if (thread->state == THREAD_READY) {
        DEBUG_ASSERT(state->InQueue());
        DEBUG_ASSERT(state->active());
        current->GetRunQueue(thread).erase(*thread);
      }

      if (IsFairThread(thread)) {
        // Changed to the deadline discipline and update the task counts and
        // queue weight.
        current->weight_total_ -= state->fair_.weight;
        state->discipline_ = SchedDiscipline::Deadline;
        current->runnable_fair_task_count_--;
        current->runnable_deadline_task_count_++;
      } else {
        // Remove old utilization from the run queue.
        current->total_deadline_utilization_ -= state->deadline_.utilization;
      }

      // Update the deadline params and the run queue.
      state->deadline_ = params;
      state->start_time_ = effective_start_time;
      state->finish_time_ = state->start_time_ + params.deadline_ns;
      state->time_slice_ns_ = std::min(state->time_slice_ns_, params.capacity_ns);
      current->total_deadline_utilization_ += state->deadline_.utilization;

      // Adjust the position of the thread in the run queue based on the new
      // deadline.
      if (thread->state == THREAD_READY) {
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
      state->discipline_ = SchedDiscipline::Deadline;
      state->deadline_ = params;
      if (thread->blocking_wait_queue) {
        wait_queue_priority_changed(thread, original_priority, propagate);
      }
      break;

    default:
      break;
  }
}

void Scheduler::ChangeWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_change_weight"_stringref};

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
  thread->priority_boost = 0;
  thread->base_priority = priority;
  thread->effec_priority = std::max(thread->base_priority, thread->inherited_priority);

  // Perform the state-specific updates if the discipline or effective priority
  // changed.
  if (IsDeadlineThread(thread) || thread->effec_priority != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(thread->effec_priority),
                       cpus_to_reschedule_mask, PropagatePI::Yes);
  }

  trace.End(original_priority, thread->effec_priority);
}

void Scheduler::ChangeDeadline(thread_t* thread, const SchedDeadlineParams& params,
                               cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_change_deadline"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d\n", thread->name,
                ToString(thread->state), thread->base_priority, thread->effec_priority,
                thread->inherited_priority);

  if (thread_is_idle(thread) || thread->state == THREAD_DEATH) {
    return;
  }

  SchedulerState* const state = &thread->scheduler_state;
  const bool changed = IsFairThread(thread) || state->deadline_ != params;

  // Always set deadline threads to the highest fair priority. This is a
  // workaround until deadline priority inheritance is worked out.
  // TODO(eieio): Replace this with actual deadline PI.
  const int original_priority = thread->effec_priority;
  thread->base_priority = HIGHEST_PRIORITY;
  thread->priority_boost = 0;
  thread->inherited_priority = -1;
  thread->effec_priority = thread->base_priority;

  // Perform the state-specific updates if the discipline or deadline params changed.
  if (changed) {
    UpdateDeadlineCommon(thread, original_priority, params, cpus_to_reschedule_mask,
                         PropagatePI::Yes);
  }

  trace.End(original_priority, thread->effec_priority);
}

void Scheduler::InheritWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_inherit_weight"_stringref};

  DEBUG_ASSERT(spin_lock_held(&thread_lock));
  SCHED_LTRACEF("thread={%s, %s} base=%d effective=%d inherited=%d\n", thread->name,
                ToString(thread->state), thread->base_priority, thread->effec_priority,
                thread->inherited_priority);

  // For now deadline threads are logically max weight for the purposes of
  // priority inheritance.
  if (IsDeadlineThread(thread)) {
    return;
  }

  const int original_priority = thread->effec_priority;
  thread->priority_boost = 0;
  thread->inherited_priority = priority;
  thread->effec_priority = std::max(thread->base_priority, thread->inherited_priority);

  // Perform the state-specific updates if the effective priority changed.
  if (thread->effec_priority != original_priority) {
    UpdateWeightCommon(thread, original_priority, PriorityToWeight(thread->effec_priority),
                       cpus_to_reschedule_mask, PropagatePI::No);
  }

  trace.End(original_priority, thread->effec_priority);
}

void Scheduler::TimerTick(SchedTime now) {
  LocalTraceDuration<KTRACE_COMMON> trace{"sched_timer_tick"_stringref};
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

void sched_change_deadline(thread_t* thread, const zx_sched_deadline_params_t& params) {
  cpu_mask_t cpus_to_reschedule_mask = 0;
  Scheduler::ChangeDeadline(thread, params, &cpus_to_reschedule_mask);

  const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
  if (cpus_to_reschedule_mask & current_cpu_mask) {
    Scheduler::Reschedule();
  }
  if (cpus_to_reschedule_mask & ~current_cpu_mask) {
    mp_reschedule(cpus_to_reschedule_mask, 0);
  }
}

void sched_preempt_timer_tick(zx_time_t now) { Scheduler::TimerTick(SchedTime{now}); }
