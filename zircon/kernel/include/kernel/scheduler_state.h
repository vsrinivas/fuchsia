// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/intrusive_wavl_tree.h>
#include <ffl/fixed.h>

// Forward declaration.
struct thread_t;

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

// Fixed-point types wrapping time and duration types to make time expressions
// cleaner in the scheduler code.
using SchedDuration = ffl::Fixed<zx_duration_t, 0>;
using SchedTime = ffl::Fixed<zx_time_t, 0>;

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

#if WITH_UNIFIED_SCHEDULER

// Per-thread state used by the unified version of Scheduler.
class SchedulerState {
 public:
  // The key type of this node operated on by WAVLTree.
  using KeyType = std::pair<SchedTime, uint64_t>;

  SchedulerState() = default;

  explicit SchedulerState(SchedWeight weight) : weight_{weight} {}

  SchedulerState(const SchedulerState&) = delete;
  SchedulerState& operator=(const SchedulerState&) = delete;

  SchedWeight weight() const { return weight_; }

  // Returns the key used to order the run queue.
  KeyType key() const { return {finish_time_, generation_}; }

  // Returns the generation count from the last time the thread was enqueued
  // in the runnable tree.
  uint64_t generation() const { return generation_; }

 private:
  friend class Scheduler;

  // Returns true of the task state is currently enqueued in the runnable tree.
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
  fbl::WAVLTreeNodeState<thread_t*> run_queue_node_;

  // The current weight of the thread.
  SchedWeight weight_{0};

  // Flag indicating whether this thread is associated with a run queue.
  bool active_{false};

  // TODO(eieio): Some of the values below are only relevant when running,
  // while others only while ready. Consider using a union to save space.

  // The virtual time of the thread's current bandwidth request.
  SchedTime start_time_{0};

  // The virtual finish time of the thread's current bandwidth request.
  SchedTime finish_time_{0};

  // The current timeslice allocated to the thread.
  SchedDuration time_slice_ns_{0};

  // Takes the value of Scheduler::generation_count_ + 1 at the time this node
  // is added to the run queue.
  uint64_t generation_{0};
};

#elif WITH_FAIR_SCHEDULER

// Per-thread state used by the fair-only version of Scheduler.
// TODO(eieio): Remove this once the unified scheduler rolls out.
class SchedulerState {
 public:
  // The key type of this node operated on by WAVLTree.
  using KeyType = std::pair<SchedTime, uint64_t>;

  SchedulerState() = default;

  explicit SchedulerState(SchedWeight weight) : weight_{weight} {}

  SchedulerState(const SchedulerState&) = delete;
  SchedulerState& operator=(const SchedulerState&) = delete;

  SchedWeight weight() const { return weight_; }

  // Returns the key used to order the run queue.
  KeyType key() const { return {virtual_finish_time_, generation_}; }

  // Returns true of the task state is currently enqueued in the runnable tree.
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

  // Returns the generation count from the last time the thread was enqueued
  // in the runnable tree.
  uint64_t generation() const { return generation_; }

 private:
  friend class Scheduler;

  // WAVLTree node state.
  fbl::WAVLTreeNodeState<thread_t*> run_queue_node_;

  // The current weight of the thread.
  SchedWeight weight_{0};

  // Flag indicating whether this thread is associated with a run queue.
  bool active_{false};

  // TODO(eieio): Some of the values below are only relevant when running,
  // while others only while ready. Consider using a union to save space.

  // The virtual time of the thread's current bandwidth request.
  SchedTime virtual_start_time_{0};

  // The virtual finish time of the thread's current bandwidth request.
  SchedTime virtual_finish_time_{0};

  // The current timeslice allocated to the thread.
  SchedDuration time_slice_ns_{0};

  // The remainder of timeslice allocated to the thread when it blocked.
  SchedDuration lag_time_ns_{0};

  // Takes the value of Scheduler::generation_count_ + 1 at the time this node
  // is added to the run queue.
  uint64_t generation_{0};
};

#endif  // WITH_FAIR_SCHEDULER

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_STATE_H_
