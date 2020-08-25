// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_

#include <lib/relaxed_atomic.h>
#include <platform.h>
#include <stdint.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/intrusive_pointer_traits.h>
#include <fbl/intrusive_wavl_tree.h>
#include <ffl/fixed.h>
#include <kernel/scheduler_state.h>
#include <kernel/thread.h>
#include <kernel/wait.h>

// Forward declaration.
struct percpu;

// Ensure this define has a value when not defined globally by the build system.
#ifndef SCHEDULER_TRACING_LEVEL
#define SCHEDULER_TRACING_LEVEL 0
#endif

// Performance scale of a CPU relative to the highest performance CPU in the
// system. The precision accommodates the 8bit performance values available for
// ARM and x86.
using SchedPerformanceScale = ffl::Fixed<int32_t, 8>;

// Implements fair and deadline scheduling algorithms and manages the associated
// per-CPU state.
class Scheduler {
 public:
  // Default minimum granularity of time slices.
  static constexpr SchedDuration kDefaultMinimumGranularity = SchedUs(750);

  // Default target latency for a scheduling period.
  static constexpr SchedDuration kDefaultTargetLatency = SchedMs(16);

  // Default peak latency for a scheduling period.
  static constexpr SchedDuration kDefaultPeakLatency = SchedMs(24);

  static_assert(kDefaultPeakLatency >= kDefaultTargetLatency);

  // The adjustment rate of the exponential moving average tracking the expected
  // runtime of each thread.
  static constexpr ffl::Fixed<int32_t, 2> kExpectedRuntimeAlpha = ffl::FromRatio(3, 4);

  Scheduler() = default;
  ~Scheduler() = default;

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  // Accessors for total weight and number of runnable tasks.
  SchedWeight GetTotalWeight() const TA_EXCL(thread_lock);
  size_t GetRunnableTasks() const TA_EXCL(thread_lock);

  // Dumps the state of the run queue to the debug log.
  void Dump() TA_REQ(thread_lock);

  // Returns the number of the CPU this scheduler instance is associated with.
  cpu_num_t this_cpu() const { return this_cpu_; }

  // Returns the index of the logical cluster of the CPU this scheduler instance
  // is associated with.
  size_t cluster() const { return cluster_; }

  // Returns the lock-free value of the predicted queue time for the CPU this
  // scheduler instance is associated with.
  SchedDuration predicted_queue_time_ns() const {
    return exported_total_expected_runtime_ns_.load();
  }

  // Returns the lock-free value of the predicted deadline utilization for the
  // CPU this scheduler instance is associated with.
  SchedUtilization predicted_deadline_utilization() const {
    return exported_total_deadline_utilization_.load();
  }

  // Returns the performance scale of the CPU this scheduler instance is
  // associated with.
  SchedPerformanceScale performance_scale() const { return performance_scale_; }

  // Returns the reciprocal performance scale of the CPU this scheduler instance
  // is associated with.
  SchedPerformanceScale performance_scale_reciprocal() const {
    return performance_scale_reciprocal_;
  }

  // Public entry points.

  static void InitializeThread(Thread* thread, int priority);
  static void InitializeThread(Thread* thread, const zx_sched_deadline_params_t& params);
  static void Block() TA_REQ(thread_lock);
  static void Yield() TA_REQ(thread_lock);
  static void Preempt() TA_REQ(thread_lock);
  static void Reschedule() TA_REQ(thread_lock);
  static void RescheduleInternal() TA_REQ(thread_lock);

  // Return true if the thread was placed on the current cpu's run queue.
  // This usually means the caller should locally reschedule soon.
  static bool Unblock(Thread* thread) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static bool Unblock(WaitQueueSublist thread_list) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static void UnblockIdle(Thread* idle_thread) TA_REQ(thread_lock);

  static void Migrate(Thread* thread) TA_REQ(thread_lock);
  static void MigrateUnpinnedThreads() TA_REQ(thread_lock);

  // TimerTick is called when the preemption timer for a CPU has fired.
  //
  // This function is logically private and should only be called by timer.cc.
  static void TimerTick(SchedTime now);

  // Set the inherited priority of a thread.
  //
  // Update a mask of affected CPUs along with a flag indicating whether or not a
  // local reschedule is needed.  After the caller has finished any batch update
  // operations, it is their responsibility to trigger reschedule operations on
  // the local CPU (if needed) as well as any other CPUs.  This allows callers to
  // bacth update the state of several threads in a priority inheritance chain
  // before finally rescheduling.
  static void InheritPriority(Thread* t, int pri, bool* local_resched, cpu_mask_t* accum_cpu_mask)
      TA_REQ(thread_lock);

  // Set the priority of a thread and reset the boost value. This function might reschedule.
  // pri should be 0 <= to <= MAX_PRIORITY.
  static void ChangePriority(Thread* t, int pri) TA_REQ(thread_lock);

  // Set the deadline of a thread. This function might reschedule.
  // This requires: 0 < capacity <= relative_deadline <= period.
  static void ChangeDeadline(Thread* t, const zx_sched_deadline_params_t& params)
      TA_REQ(thread_lock);

 private:
  // Allow percpu to init our cpu number and performance scale.
  friend struct percpu;
  // Load balancer test.
  friend struct LoadBalancerTestAccess;
  // Allow tests to modify our state.
  friend class LoadBalancerTest;

  static void ChangeWeight(Thread* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);
  static void ChangeDeadline(Thread* thread, const SchedDeadlineParams& params,
                             cpu_mask_t* cpus_to_reschedule_mask) TA_REQ(thread_lock);
  static void InheritWeight(Thread* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);

  // Specifies how to place a thread in the virtual timeline and run queue.
  enum class Placement {
    // Selects a place in the queue based on the current insertion time and
    // thread weight or deadline.
    Insertion,

    // Selects a place in the queue based on the original insertion time and
    // the updated (inherited or changed) weight or deadline.
    Adjustment,

    // Selects a place in the queue based on the original insertion time and
    // the updated time slice due to being preempted by another thread.
    Preemption,
  };

  // Returns the current system time as a SchedTime value.
  static SchedTime CurrentTime() { return SchedTime{current_time()}; }

  // Returns the Scheduler instance for the current CPU.
  static Scheduler* Get();

  // Returns the Scheduler instance for the given CPU.
  static Scheduler* Get(cpu_num_t cpu);

  // Returns a CPU to run the given thread on.
  static cpu_num_t FindTargetCpu(Thread* thread) TA_REQ(thread_lock);

  // Updates the system load metrics.
  void UpdateCounters(SchedDuration queue_time_ns) TA_REQ(thread_lock);

  // Updates the thread's weight and updates state-dependent bookkeeping.
  static void UpdateWeightCommon(Thread* thread, int original_priority, SchedWeight weight,
                                 cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate)
      TA_REQ(thread_lock);

  // Updates the thread's deadline and updates state-dependent bookkeeping.
  static void UpdateDeadlineCommon(Thread* thread, int original_priority,
                                   const SchedDeadlineParams& params,
                                   cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate)
      TA_REQ(thread_lock);

  using EndTraceCallback = fbl::InlineFunction<void(), sizeof(void*)>;

  // Common logic for reschedule API.
  void RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace = nullptr)
      TA_REQ(thread_lock);

  // Evaluates the schedule and returns the thread that should execute,
  // updating the run queue as necessary.
  Thread* EvaluateNextThread(SchedTime now, Thread* current_thread, bool timeslice_expired,
                             SchedDuration total_runtime_ns) TA_REQ(thread_lock);

  // Adds a thread to the run queue tree. The thread must be active on this
  // CPU.
  void QueueThread(Thread* thread, Placement placement, SchedTime now = SchedTime{0},
                   SchedDuration total_runtime_ns = SchedDuration{0}) TA_REQ(thread_lock);

  // Removes the thread at the head of the first eligible run queue.
  Thread* DequeueThread(SchedTime now) TA_REQ(thread_lock);

  // Removes the thread at the head of the fair run queue and returns it.
  Thread* DequeueFairThread() TA_REQ(thread_lock);

  // Removes the eligible thread with the earliest deadline in the deadline run
  // queue and returns it.
  Thread* DequeueDeadlineThread(SchedTime eligible_time) TA_REQ(thread_lock);

  // Returns the eligible thread in the run queue with a deadline earlier than
  // the given deadline, or nullptr if one does not exist.
  Thread* FindEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time)
      TA_REQ(thread_lock);

  // Removes the eligible thread with a deadline earlier than the given deadline
  // and returns it or nullptr if one does not exist.
  Thread* DequeueEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time)
      TA_REQ(thread_lock);

  // Returns the time that the next deadline task will become eligible or infinite
  // if there are no ready deadline tasks.
  SchedTime GetNextEligibleTime() TA_REQ(thread_lock);

  // Calculates the timeslice of the thread based on the current run queue
  // state.
  SchedDuration CalculateTimeslice(Thread* thread) TA_REQ(thread_lock);

  // Returns the completion time clamped to the start of the earliest deadline
  // thread that will become eligible in that time frame.
  SchedTime ClampToDeadline(SchedTime completion_time) TA_REQ(thread_lock);

  // Returns the completion time clamped to the start of the earliest deadline
  // thread that will become eligible in that time frame and also has an earlier
  // deadline than the given finish time.
  SchedTime ClampToEarlierDeadline(SchedTime completion_time, SchedTime finish_time)
      TA_REQ(thread_lock);

  // Updates the timeslice of the thread based on the current run queue state.
  // Returns the absolute deadline for the next time slice, which may be earlier
  // than the completion of the time slice if other threads could preempt the
  // given thread before the time slice is exhausted.
  SchedTime NextThreadTimeslice(Thread* thread, SchedTime now) TA_REQ(thread_lock);

  // Updates the scheduling period based on the number of active threads.
  void UpdatePeriod() TA_REQ(thread_lock);

  // Updates the global virtual timeline.
  void UpdateTimeline(SchedTime now) TA_REQ(thread_lock);

  // Makes a thread active on this CPU's scheduler and inserts it into the
  // run queue tree.
  void Insert(SchedTime now, Thread* thread) TA_REQ(thread_lock);

  // Removes the thread from this CPU's scheduler. The thread must not be in
  // the run queue tree.
  void Remove(Thread* thread) TA_REQ(thread_lock);

  // Returns true if there is at least one eligible deadline thread in the
  // run queue.
  inline bool IsDeadlineThreadEligible(SchedTime eligible_time) TA_REQ(thread_lock) {
    return !deadline_run_queue_.is_empty() &&
           deadline_run_queue_.front().scheduler_state().start_time_ <= eligible_time;
  }

  // Updates the total expected runtime estimator and exports the atomic shadow
  // variable for cross-CPU readers.
  inline void UpdateTotalExpectedRuntime(SchedDuration delta) TA_REQ(thread_lock);

  // Updates to total deadline utilization estimator and exports the atomic
  // shadow variable for cross-CPU readers.
  inline void UpdateTotalDeadlineUtilization(SchedUtilization delta) TA_REQ(thread_lock);

  // Update trace counters which track the total number of runnable threads for a CPU
  inline void TraceTotalRunnableThreads() const TA_REQ(thread_lock);

  // Traits type to adapt the WAVLTree to Thread with node state in the
  // scheduler_state member.
  struct TaskTraits {
    using KeyType = SchedulerState::KeyType;
    static KeyType GetKey(const Thread& thread) { return thread.scheduler_state().key(); }
    static bool LessThan(KeyType a, KeyType b) { return a < b; }
    static bool EqualTo(KeyType a, KeyType b) { return a == b; }
    static auto& node_state(Thread& thread) { return thread.scheduler_state().run_queue_node_; }
  };

  // Observer that maintains the subtree invariant min_finish_time as nodes are
  // added to and removed from the run queue.
  struct SubtreeMinObserver {
    template <typename Iter>
    static void RecordInsert(Iter node);
    template <typename Iter>
    static void RecordInsertCollision(Thread* node, Iter collision);
    template <typename Iter>
    static void RecordInsertReplace(Iter node, Thread* replacement);
    template <typename Iter>
    static void RecordInsertTraverse(Thread* node, Iter ancestor);
    template <typename Iter>
    static void RecordRotation(Iter pivot, Iter lr_child, Iter rl_child, Iter parent, Iter sibling);
    template <typename Iter>
    static void RecordErase(Thread* node, Iter invalidated);

    static void RecordInsertPromote() {}
    static void RecordInsertRotation() {}
    static void RecordInsertDoubleRotation() {}
    static void RecordEraseDemote() {}
    static void RecordEraseRotation() {}
    static void RecordEraseDoubleRotation() {}
  };

  // Alias of the WAVLTree type for the run queue.
  using RunQueue = fbl::WAVLTree<SchedTime, Thread*, TaskTraits, fbl::DefaultObjectTag, TaskTraits,
                                 SubtreeMinObserver>;

  // Finds the next eligible thread in the given run queue.
  static Thread* FindEarliestEligibleThread(RunQueue* run_queue, SchedTime eligible_time)
      TA_REQ(thread_lock);

  // Returns the run queue for the given thread's scheduling discipline.
  inline RunQueue& GetRunQueue(Thread* thread) {
    return thread->scheduler_state().discipline() == SchedDiscipline::Fair ? fair_run_queue_
                                                                           : deadline_run_queue_;
  }

  // The run queue of fair scheduled threads ready to run, but not currently running.
  TA_GUARDED(thread_lock)
  RunQueue fair_run_queue_;

  // The run queue of deadline scheduled threads ready to run, but not currently running.
  TA_GUARDED(thread_lock)
  RunQueue deadline_run_queue_;

  // Pointer to the thread actively running on this CPU.
  TA_GUARDED(thread_lock)
  Thread* active_thread_{nullptr};

  // Monotonically increasing counter to break ties when queuing tasks with
  // the same key. This has the effect of placing newly queued tasks behind
  // already queued tasks with the same key. This is also necessary to
  // guarantee uniqueness of the key as required by the WAVLTree container.
  TA_GUARDED(thread_lock)
  uint64_t generation_count_{0};

  // Count of the fair threads running on this CPU, including threads in the run
  // queue and the currently running thread. Does not include the idle thread.
  TA_GUARDED(thread_lock)
  int32_t runnable_fair_task_count_{0};

  // Count of the deadline threads running on this CPU, including threads in the
  // run queue and the currently running thread. Does not include the idle
  // thread.
  TA_GUARDED(thread_lock)
  int32_t runnable_deadline_task_count_{0};

  // Total weights of threads running on this CPU, including threads in the
  // run queue and the currently running thread. Does not include the idle
  // thread.
  TA_GUARDED(thread_lock)
  SchedWeight weight_total_{0};

  // The value of |weight_total_| when the current thread was scheduled.
  // Provides a reference for determining whether the total weights changed
  // since the last reschedule.
  TA_GUARDED(thread_lock)
  SchedWeight scheduled_weight_total_{0};

  // The global virtual time of this run queue.
  TA_GUARDED(thread_lock)
  SchedTime virtual_time_{0};

  // The system time since the last update to the global virtual time.
  TA_GUARDED(thread_lock)
  SchedTime last_update_time_ns_{0};

  // The system time that the current time slice started.
  TA_GUARDED(thread_lock)
  SchedTime start_of_current_time_slice_ns_{0};

  // The system time that the current thread should be preempted.
  TA_GUARDED(thread_lock)
  SchedTime absolute_deadline_ns_{0};

  // The sum of the expected runtimes of all active threads on this CPU. This
  // value is an estimate of the average queuimg time for this CPU, given the
  // current set of active threads.
  TA_GUARDED(thread_lock)
  SchedDuration total_expected_runtime_ns_{SchedNs(0)};

  // The sum of the worst case utilization of all active deadline threads on
  // this CPU.
  TA_GUARDED(thread_lock)
  SchedUtilization total_deadline_utilization_{0};

  // Scheduling period in which every runnable task executes once in units of
  // minimum granularity.
  TA_GUARDED(thread_lock)
  SchedDuration scheduling_period_grans_{kDefaultTargetLatency / kDefaultMinimumGranularity};

  // The smallest timeslice a thread is allocated in a single round.
  TA_GUARDED(thread_lock)
  SchedDuration minimum_granularity_ns_{kDefaultMinimumGranularity};

  // The target scheduling period. The scheduling period is set to this value
  // when the number of tasks is low enough for the sum of all timeslices to
  // fit within this duration. This has the effect of increasing the size of
  // the timeslices under nominal load to reduce scheduling overhead.
  TA_GUARDED(thread_lock)
  SchedDuration target_latency_grans_{kDefaultTargetLatency / kDefaultMinimumGranularity};

  // Performance scale of this CPU relative to the highest performance CPU. This
  // value is determined from the system topology, when available.
  SchedPerformanceScale performance_scale_{1};
  SchedPerformanceScale performance_scale_reciprocal_{1};

  // The CPU this scheduler instance is associated with.
  // NOTE: This member is not initialized to prevent clobbering the value set
  // by sched_early_init(), which is called before the global ctors that
  // initialize the rest of the members of this class.
  // TODO(eieio): Figure out a better long-term solution to determine which
  // CPU is associated with each instance of this class. This is needed by
  // non-static methods that are called from arbitrary CPUs, namely Insert().
  cpu_num_t this_cpu_;

  // The index of the logical cluster this CPU belongs to. CPUs with the same
  // logical cluster index have the best chance of good cache affinity with
  // respect to load distribution decisions.
  size_t cluster_{0};

  // Values exported for lock-free access across CPUs. These are mirrors of the
  // members of the same name without the exported_ prefix. This avoids
  // unnecessary atomic loads when updating the values using arithmetic
  // operations on the local CPU. These values are atomically readonly to other
  // CPUs.
  // TODO(eieio): Look at cache line alignment for these members to optimize
  // cache performance.
  RelaxedAtomic<SchedDuration> exported_total_expected_runtime_ns_{SchedNs(0)};
  RelaxedAtomic<SchedUtilization> exported_total_deadline_utilization_{SchedUtilization{0}};
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_
