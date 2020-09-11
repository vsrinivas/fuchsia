// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

#include <fbl/intrusive_wavl_tree.h>
#include <ffl/fixed.h>
#include <kernel/cpu.h>
#include <ktl/pair.h>

// Forward declaration.
struct Thread;

// Fixed-point task weight.
//
// The 16bit fractional component accommodates the exponential curve defining
// the priority-to-weight relation:
//
//      Weight = 1.225^(Priority - 31)
//
// This yields roughly 10% bandwidth difference between adjacent priorities.
//
// Weights should not be negative, however, the value is signed for consistency
// with zx_time_t (SchedTime) and zx_duration_t (SchedDuration), which are the
// primary types used in conjunction with SchedWeight. This is to make it less
// likely that expressions involving weights are accidentally promoted to
// unsigned.
using SchedWeight = ffl::Fixed<int64_t, 16>;

// Fixed-point time slice remainder.
//
// The 20bit fractional component represents a fractional time slice with a
// precision of ~1us.
using SchedRemainder = ffl::Fixed<int64_t, 20>;

// Fixed-point utilization factor. Represents the ratio between capacity and
// period or capacity and relative deadline, depending on which type of
// utilization is being evaluated.
//
// The 20bit fractional component represents the utilization with a precision
// of ~1us.
using SchedUtilization = ffl::Fixed<int64_t, 20>;

// Fixed-point types wrapping time and duration types to make time expressions
// cleaner in the scheduler code.
using SchedDuration = ffl::Fixed<zx_duration_t, 0>;
using SchedTime = ffl::Fixed<zx_time_t, 0>;

// Represents the key deadline scheduler parameters using fixed-point types.
// This is a fixed point version of the ABI type zx_sched_deadline_params_t that
// makes expressions in the scheduler logic less verbose.
struct SchedDeadlineParams {
  SchedDuration capacity_ns{0};
  SchedDuration deadline_ns{0};
  SchedDuration period_ns{0};
  SchedUtilization utilization{0};

  constexpr SchedDeadlineParams() = default;
  constexpr SchedDeadlineParams(SchedDuration capacity_ns, SchedDuration deadline_ns,
                                SchedDuration period_ns)
      : capacity_ns{capacity_ns},
        deadline_ns{deadline_ns},
        period_ns{period_ns},
        utilization{capacity_ns / deadline_ns} {}

  constexpr SchedDeadlineParams(const SchedDeadlineParams&) = default;
  constexpr SchedDeadlineParams& operator=(const SchedDeadlineParams&) = default;

  constexpr SchedDeadlineParams(const zx_sched_deadline_params_t& params)
      : capacity_ns{params.capacity},
        deadline_ns{params.relative_deadline},
        period_ns{params.period},
        utilization{capacity_ns / deadline_ns} {}
  constexpr SchedDeadlineParams& operator=(const zx_sched_deadline_params_t& params) {
    *this = SchedDeadlineParams{params};
    return *this;
  }

  friend bool operator==(SchedDeadlineParams a, SchedDeadlineParams b) {
    return a.capacity_ns == b.capacity_ns && a.deadline_ns == b.deadline_ns &&
           a.period_ns == b.period_ns;
  }
  friend bool operator!=(SchedDeadlineParams a, SchedDeadlineParams b) { return !(a == b); }
};

// Utilities that return fixed-point Expression representing the given integer
// time units in terms of system time units (nanoseconds).
template <typename T>
constexpr auto SchedNs(T nanoseconds) {
  return ffl::FromInteger(ZX_NSEC(nanoseconds));
}
template <typename T>
constexpr auto SchedUs(T microseconds) {
  return ffl::FromInteger(ZX_USEC(microseconds));
}
template <typename T>
constexpr auto SchedMs(T milliseconds) {
  return ffl::FromInteger(ZX_MSEC(milliseconds));
}

// Specifies the type of scheduling algorithm applied to a thread.
enum class SchedDiscipline {
  Fair,
  Deadline,
};

// Per-thread state used by the unified version of Scheduler.
class SchedulerState {
 public:
  // The key type of this node operated on by WAVLTree.
  using KeyType = ktl::pair<SchedTime, uint64_t>;

  SchedulerState() {}

  explicit SchedulerState(SchedWeight weight)
      : discipline_{SchedDiscipline::Fair}, fair_{.weight = weight} {}
  explicit SchedulerState(SchedDeadlineParams params)
      : discipline_{SchedDiscipline::Deadline}, deadline_{params} {}

  SchedulerState(const SchedulerState&) = delete;
  SchedulerState& operator=(const SchedulerState&) = delete;

  // Returns the effective mask of CPUs a thread may run on, based on the
  // thread's affinity masks and CPUs currently active on the system.
  cpu_mask_t GetEffectiveCpuMask(cpu_mask_t active_mask) {
    // The thread may run on any active CPU allowed by both its hard and
    // soft CPU affinity.
    const cpu_mask_t available_mask = active_mask & soft_affinity_ & hard_affinity_;

    // Return the mask honoring soft affinity if it is viable, otherwise ignore
    // soft affinity and honor only hard affinity.
    if (likely(available_mask != 0)) {
      return available_mask;
    }

    return active_mask & hard_affinity_;
  }

  // Returns the type of scheduling discipline for this thread.
  SchedDiscipline discipline() const { return discipline_; }

  // Returns the key used to order the run queue.
  KeyType key() const { return {start_time_, generation_}; }

  // Returns the generation count from the last time the thread was enqueued
  // in the runnable tree.
  uint64_t generation() const { return generation_; }

  zx_time_t last_started_running() const { return last_started_running_.raw_value(); }
  zx_duration_t time_slice_ns() const { return time_slice_ns_.raw_value(); }
  zx_duration_t runtime_ns() const { return runtime_ns_.raw_value(); }
  zx_duration_t expected_runtime_ns() const { return expected_runtime_ns_.raw_value(); }

  cpu_mask_t hard_affinity() const { return hard_affinity_; }
  cpu_mask_t soft_affinity() const { return soft_affinity_; }

  int base_priority() const { return base_priority_; }
  int effective_priority() const { return effective_priority_; }
  int inherited_priority() const { return inherited_priority_; }

  cpu_num_t curr_cpu() const { return curr_cpu_; }
  cpu_num_t last_cpu() const { return last_cpu_; }

  void set_next_cpu(cpu_num_t next_cpu) {
    next_cpu_ = next_cpu;
  }

 private:
  friend class Scheduler;

  // Allow tests to modify our state.
  friend class LoadBalancerTest;

  // TODO(eieio): Remove these once all of the members accessed by Thread are
  // moved to accessors.
  friend Thread;
  friend void thread_construct_first(Thread*, const char*);
  friend void dump_thread_locked(Thread*, bool);

  // Returns true of the task state is currently enqueued in the run queue.
  bool InQueue() const { return run_queue_node_.InContainer(); }

  // Returns true if the task is active (queued or running) on a run queue.
  bool active() const { return active_; }

  // Sets the task state to active (on a run queue). Returns true if the task
  // was not previously active.
  bool OnInsert() {
    const bool was_active = active_;
    active_ = true;
    return !was_active;
  }

  // Sets the task state to inactive (not on a run queue). Returns true if the
  // task was previously active.
  bool OnRemove() {
    const bool was_active = active_;
    active_ = false;
    return was_active;
  }

  // WAVLTree node state.
  fbl::WAVLTreeNodeState<Thread*> run_queue_node_;

  // The time the thread last ran. The exact point in time this value represents
  // depends on the thread state:
  //   * THREAD_RUNNING: The time of the last reschedule that selected the thread.
  //   * THREAD_READY: The time the thread entered the run queue.
  //   * Otherwise: The time the thread last ran.
  SchedTime last_started_running_{0};

  // The total time in THREAD_RUNNING state. If the thread is currently in
  // THREAD_RUNNING state, this excludes the time accrued since it last left the
  // scheduler.
  SchedDuration runtime_ns_{0};

  // The legacy base, effective, and inherited priority values.
  // TODO(eieio): Move initialization of these members to the constructor. It is
  // currently handled by Scheduler::InitializeThread.
  int base_priority_;
  int effective_priority_;
  int inherited_priority_;

  // The current CPU the thread is READY or RUNNING on, INVALID_CPU otherwise.
  cpu_num_t curr_cpu_{INVALID_CPU};

  // The last CPU the thread ran on. INVALID_CPU before it first runs.
  cpu_num_t last_cpu_{INVALID_CPU};

  // The next CPU the thread should run on after the thread's migrate function
  // is called.
  cpu_num_t next_cpu_{INVALID_CPU};

  // The set of CPUs the thread is permitted to run on. The thread is never
  // assigned to CPUs outside of this set.
  cpu_mask_t hard_affinity_{CPU_MASK_ALL};

  // The set of CPUs the thread should run on if possible. The thread may be
  // assigned to CPUs outside of this set if necessary.
  cpu_mask_t soft_affinity_{CPU_MASK_ALL};

  // The scheduling discipline of this thread. Determines whether the thread is
  // enqueued on the fair or deadline run queues and whether the weight or
  // deadline parameters are used.
  SchedDiscipline discipline_{SchedDiscipline::Fair};

  // The current fair or deadline parameters of the thread.
  union {
    struct {
      SchedWeight weight{0};
      SchedDuration initial_time_slice_ns{0};
      SchedRemainder normalized_timeslice_remainder{0};
    } fair_;
    SchedDeadlineParams deadline_;
  };

  // The start time of the thread's current bandwidth request. This is the
  // virtual start time for fair tasks and the period start for deadline tasks.
  SchedTime start_time_{0};

  // The finish time of the thread's current bandwidth request. This is the
  // virtual finish time for fair tasks and the absolute deadline for deadline
  // tasks.
  SchedTime finish_time_{0};

  // Mimimum finish time of all the descendents of this node in the run queue.
  // This value is automatically maintained by the WAVLTree observer hooks. The
  // value is used to perform a partition search in O(log n) time, to find the
  // thread with the earliest finish time that also has an eligible start time.
  SchedTime min_finish_time_{0};

  // The current timeslice allocated to the thread.
  SchedDuration time_slice_ns_{0};

  // Tracks the exponential moving average of the runtime of the thread.
  SchedDuration expected_runtime_ns_{0};

  // Takes the value of Scheduler::generation_count_ + 1 at the time this node
  // is added to the run queue.
  uint64_t generation_{0};

  // Flag indicating whether this thread is associated with a run queue.
  bool active_{false};
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_
