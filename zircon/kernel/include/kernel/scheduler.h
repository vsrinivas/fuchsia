// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_

#include <platform.h>
#include <stdint.h>
#include <zircon/syscalls/scheduler.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/intrusive_pointer_traits.h>
#include <fbl/intrusive_wavl_tree.h>
#include <ffl/fixed.h>
#include <kernel/sched.h>
#include <kernel/scheduler_state.h>
#include <kernel/thread.h>
#include <kernel/wait.h>

// Forward declaration.
struct percpu;

// Guard the definition of this class because the Traits types directly refer to
// thread_t::scheduler_state, which is not present when the new scheduler is
// disabled.
#if WITH_UNIFIED_SCHEDULER

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
  static constexpr size_t kExpectedRuntimeAdjustmentRateShift = 8;

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

 private:
  // Allow percpu to init our cpu number.
  friend struct percpu;

  // Befriend the sched API wrappers to enable calling the static methods
  // below.
  friend void sched_init_thread(thread_t* thread, int priority);
  friend void sched_block();
  friend void sched_yield();
  friend void sched_preempt();
  friend void sched_reschedule();
  friend void sched_resched_internal();
  friend void sched_unblock_idle(thread_t* thread);
  friend void sched_migrate(thread_t* thread);
  friend void sched_inherit_priority(thread_t* thread, int priority, bool* local_reschedule,
                                     cpu_mask_t* cpus_to_reschedule_mask);
  friend void sched_change_priority(thread_t* thread, int priority);
  friend void sched_change_deadline(thread_t* thread, const zx_sched_deadline_params_t& params);
  friend bool sched_unblock(thread_t* t);
  friend bool sched_unblock_list(struct list_node* list);
  friend void sched_transition_off_cpu(cpu_num_t old_cpu);
  friend void sched_preempt_timer_tick(zx_time_t now);

  // Static scheduler methods called by the wrapper API above.
  static void InitializeThread(thread_t* thread, int priority);
  static void InitializeThread(thread_t* thread, const zx_sched_deadline_params_t& params);
  static void Block() TA_REQ(thread_lock);
  static void Yield() TA_REQ(thread_lock);
  static void Preempt() TA_REQ(thread_lock);
  static void Reschedule() TA_REQ(thread_lock);
  static void RescheduleInternal() TA_REQ(thread_lock);
  static bool Unblock(thread_t* thread) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static bool Unblock(list_node* thread_list) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static void UnblockIdle(thread_t* idle_thread) TA_REQ(thread_lock);
  static void Migrate(thread_t* thread) TA_REQ(thread_lock);
  static void MigrateUnpinnedThreads(cpu_num_t current_cpu) TA_REQ(thread_lock);
  static void ChangeWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);
  static void ChangeDeadline(thread_t* thread, const SchedDeadlineParams& params,
                             cpu_mask_t* cpus_to_reschedule_mask) TA_REQ(thread_lock);
  static void InheritWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);
  static void TimerTick(SchedTime now);

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
  static cpu_num_t FindTargetCpu(thread_t* thread) TA_REQ(thread_lock);

  // Updates the system load metrics.
  void UpdateCounters(SchedDuration queue_time_ns) TA_REQ(thread_lock);

  // Updates the thread's weight and updates state-dependent bookkeeping.
  static void UpdateWeightCommon(thread_t* thread, int original_priority, SchedWeight weight,
                                 cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate)
      TA_REQ(thread_lock);

  // Updates the thread's deadline and updates state-dependent bookkeeping.
  static void UpdateDeadlineCommon(thread_t* thread, int original_priority,
                                   const SchedDeadlineParams& params,
                                   cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate)
      TA_REQ(thread_lock);

  using EndTraceCallback = fbl::InlineFunction<void(), sizeof(void*)>;

  // Common logic for reschedule API.
  void RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace = nullptr)
      TA_REQ(thread_lock);

  // Evaluates the schedule and returns the thread that should execute,
  // updating the run queue as necessary.
  thread_t* EvaluateNextThread(SchedTime now, thread_t* current_thread, bool timeslice_expired,
                               SchedDuration total_runtime_ns) TA_REQ(thread_lock);

  // Adds a thread to the run queue tree. The thread must be active on this
  // CPU.
  void QueueThread(thread_t* thread, Placement placement, SchedTime now = SchedTime{0},
                   SchedDuration total_runtime_ns = SchedDuration{0}) TA_REQ(thread_lock);

  // Removes the thread at the head of the fair run queue and returns it.
  thread_t* DequeueFairThread() TA_REQ(thread_lock);

  // Removes the eligible thread with the earliest deadline in the deadline run
  // queue and returns it.
  thread_t* DequeueDeadlineThread(SchedTime eligible_time) TA_REQ(thread_lock);

  // Returns the eligible thread in the run queue with a deadline earlier than
  // the given deadline, or nullptr if one does not exist.
  thread_t* FindEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time)
      TA_REQ(thread_lock);

  // Removes the eligible thread with a deadline earlier than the given deadline
  // and returns it or nullptr if one does not exist.
  thread_t* DequeueEarlierDeadlineThread(SchedTime eligible_time, SchedTime finish_time)
      TA_REQ(thread_lock);

  // Returns the time that the next deadline task will become eligible or infinite
  // if there are no ready deadline tasks.
  SchedTime GetNextEligibleTime() TA_REQ(thread_lock);

  // Calculates the timeslice of the thread based on the current run queue
  // state.
  SchedDuration CalculateTimeslice(thread_t* thread) TA_REQ(thread_lock);

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
  SchedTime NextThreadTimeslice(thread_t* thread, SchedTime now) TA_REQ(thread_lock);

  // Updates the scheduling period based on the number of active threads.
  void UpdatePeriod() TA_REQ(thread_lock);

  // Updates the global virtual timeline.
  void UpdateTimeline(SchedTime now) TA_REQ(thread_lock);

  // Makes a thread active on this CPU's scheduler and inserts it into the
  // run queue tree.
  void Insert(SchedTime now, thread_t* thread) TA_REQ(thread_lock);

  // Removes the thread from this CPU's scheduler. The thread must not be in
  // the run queue tree.
  void Remove(thread_t* thread) TA_REQ(thread_lock);

  // Returns true if there is at least one eligible deadline thread in the
  // run queue.
  inline bool IsDeadlineThreadEligible(SchedTime eligible_time) TA_REQ(thread_lock) {
    return !deadline_run_queue_.is_empty() &&
           deadline_run_queue_.front().scheduler_state.start_time_ <= eligible_time;
  }

  // Traits type to adapt the WAVLTree to thread_t with node state in the
  // scheduler_state member.
  struct TaskTraits {
    using KeyType = SchedulerState::KeyType;
    static KeyType GetKey(const thread_t& thread) { return thread.scheduler_state.key(); }
    static bool LessThan(KeyType a, KeyType b) { return a < b; }
    static bool EqualTo(KeyType a, KeyType b) { return a == b; }
    static auto& node_state(thread_t& thread) { return thread.scheduler_state.run_queue_node_; }
  };

  // Observer that maintains the subtree invariant min_finish_time as nodes are
  // added to and removed from the run queue.
  struct SubtreeMinObserver {
    template <typename Iter>
    static void RecordInsert(Iter node);
    template <typename Iter>
    static void RecordInsertCollision(thread_t* node, Iter collision);
    template <typename Iter>
    static void RecordInsertReplace(Iter node, thread_t* replacement);
    template <typename Iter>
    static void RecordInsertTraverse(thread_t* node, Iter ancestor);
    template <typename Iter>
    static void RecordRotation(Iter pivot, Iter lr_child, Iter rl_child, Iter parent, Iter sibling);
    template <typename Iter>
    static void RecordErase(thread_t* node, Iter invalidated);

    static void RecordInsertPromote() {}
    static void RecordInsertRotation() {}
    static void RecordInsertDoubleRotation() {}
    static void RecordEraseDemote() {}
    static void RecordEraseRotation() {}
    static void RecordEraseDoubleRotation() {}
  };

  // Alias of the WAVLTree type for the run queue.
  using RunQueue = fbl::WAVLTree<SchedTime, thread_t*, TaskTraits, TaskTraits,
                                 fbl::DefaultObjectTag, SubtreeMinObserver>;

  // Finds the next eligible thread in the given run queue.
  static thread_t* FindEarliestEligibleThread(RunQueue* run_queue, SchedTime eligible_time)
      TA_REQ(thread_lock);

  // Returns the run queue for the given thread's scheduling discipline.
  inline RunQueue& GetRunQueue(thread_t* thread) {
    return thread->scheduler_state.discipline() == SchedDiscipline::Fair ? fair_run_queue_
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
  thread_t* active_thread_{nullptr};

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
  SchedDuration total_expected_runtime_ns_{0};

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

  // The scheduling period threshold over which the CPU is considered
  // oversubscribed.
  TA_GUARDED(thread_lock)
  SchedDuration peak_latency_grans_{kDefaultPeakLatency / kDefaultMinimumGranularity};

  // The CPU this scheduler instance is associated with.
  // NOTE: This member is not initialized to prevent clobbering the value set
  // by sched_early_init(), which is called before the global ctors that
  // initialize the rest of the members of this class.
  // TODO(eieio): Figure out a better long-term solution to determine which
  // CPU is associated with each instance of this class. This is needed by
  // non-static methods that are called from arbitrary CPUs, namely Insert().
  cpu_num_t this_cpu_;
};

#elif WITH_FAIR_SCHEDULER

// TODO(eieio): Remove once the unified scheduler rolls out.
class Scheduler {
 public:
  // Default minimum granularity of time slices.
  static constexpr SchedDuration kDefaultMinimumGranularity = SchedUs(750);

  // Default target latency for a scheduling period.
  static constexpr SchedDuration kDefaultTargetLatency = SchedMs(16);

  // Default peak latency for a scheduling period.
  static constexpr SchedDuration kDefaultPeakLatency = SchedMs(24);

  static_assert(kDefaultPeakLatency >= kDefaultTargetLatency);

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

 private:
  // Allow percpu to init our cpu number.
  friend struct percpu;

  // Befriend the sched API wrappers to enable calling the static methods
  // below.
  friend void sched_init_thread(thread_t* thread, int priority);
  friend void sched_block();
  friend void sched_yield();
  friend void sched_preempt();
  friend void sched_reschedule();
  friend void sched_resched_internal();
  friend void sched_unblock_idle(thread_t* thread);
  friend void sched_migrate(thread_t* thread);
  friend void sched_inherit_priority(thread_t* thread, int priority, bool* local_reschedule,
                                     cpu_mask_t* cpus_to_reschedule_mask);
  friend void sched_change_priority(thread_t* thread, int priority);
  friend void sched_change_deadline(thread_t* thread, const zx_sched_deadline_params_t& params);
  friend bool sched_unblock(thread_t* t);
  friend bool sched_unblock_list(struct list_node* list);
  friend void sched_transition_off_cpu(cpu_num_t old_cpu);
  friend void sched_preempt_timer_tick(zx_time_t now);

  // Static scheduler methods called by the wrapper API above.
  static void InitializeThread(thread_t* thread, int priority);
  static void Block() TA_REQ(thread_lock);
  static void Yield() TA_REQ(thread_lock);
  static void Preempt() TA_REQ(thread_lock);
  static void Reschedule() TA_REQ(thread_lock);
  static void RescheduleInternal() TA_REQ(thread_lock);
  static bool Unblock(thread_t* thread) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static bool Unblock(list_node* thread_list) __WARN_UNUSED_RESULT TA_REQ(thread_lock);
  static void UnblockIdle(thread_t* idle_thread) TA_REQ(thread_lock);
  static void Migrate(thread_t* thread) TA_REQ(thread_lock);
  static void MigrateUnpinnedThreads(cpu_num_t current_cpu) TA_REQ(thread_lock);
  static void ChangeWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);
  static void InheritWeight(thread_t* thread, int priority, cpu_mask_t* cpus_to_reschedule_mask)
      TA_REQ(thread_lock);
  static void TimerTick(SchedTime now);

  // Specifies how to place a thread in the virtual timeline and run queue.
  enum class Placement {
    // Selects a place in the queue based on the current insertion time and
    // thread weight.
    Insertion,

    // Selects a place in the queue based on the original insertion time and
    // the updated (inherited or changed) thread weight.
    Adjustment,
  };

  // Returns the current system time as a SchedTime value.
  static SchedTime CurrentTime() { return SchedTime{current_time()}; }

  // Returns the Scheduler instance for the current CPU.
  static Scheduler* Get();

  // Returns the Scheduler instance for the given CPU.
  static Scheduler* Get(cpu_num_t cpu);

  // Returns a CPU to run the given thread on.
  static cpu_num_t FindTargetCpu(thread_t* thread) TA_REQ(thread_lock);

  // Updates the system load metrics.
  void UpdateCounters(SchedDuration queue_time_ns) TA_REQ(thread_lock);

  // Updates the thread's weight and updates state-dependent bookkeeping.
  static void UpdateWeightCommon(thread_t*, int original_priority, SchedWeight weight,
                                 cpu_mask_t* cpus_to_reschedule_mask, PropagatePI propagate)
      TA_REQ(thread_lock);

  using EndTraceCallback = fbl::InlineFunction<void(), sizeof(void*)>;

  // Common logic for reschedule API.
  void RescheduleCommon(SchedTime now, EndTraceCallback end_outer_trace = nullptr)
      TA_REQ(thread_lock);

  // Evaluates the schedule and returns the thread that should execute,
  // updating the runqueue as necessary.
  thread_t* EvaluateNextThread(SchedTime now, thread_t* current_thread, bool timeslice_expired)
      TA_REQ(thread_lock);

  // Adds a thread to the runqueue tree. The thread must be active on this
  // CPU.
  void QueueThread(thread_t* thread, Placement placement, SchedTime now = SchedTime{0})
      TA_REQ(thread_lock);

  // Removes the thread at the head of the runqueue and returns it.
  thread_t* DequeueThread() TA_REQ(thread_lock);

  // Calculates the timeslice of the thread based on the current runqueue
  // state.
  SchedDuration CalculateTimeslice(thread_t* thread) TA_REQ(thread_lock);

  // Updates the timeslice of the thread based on the current runqueue state.
  void NextThreadTimeslice(thread_t* thread) TA_REQ(thread_lock);

  // Updates the thread virtual timeline based on the current runqueue state.
  void UpdateThreadTimeline(thread_t* thread, Placement placement) TA_REQ(thread_lock);

  // Updates the scheduling period based on the number of active threads.
  void UpdatePeriod() TA_REQ(thread_lock);

  // Updates the global virtual timeline.
  void UpdateTimeline(SchedTime now) TA_REQ(thread_lock);

  // Makes a thread active on this CPU's scheduler and inserts it into the
  // runqueue tree.
  void Insert(SchedTime now, thread_t* thread) TA_REQ(thread_lock);

  // Removes the thread from this CPU's scheduler. The thread must not be in
  // the runqueue tree.
  void Remove(thread_t* thread) TA_REQ(thread_lock);

  // Traits type to adapt the WAVLTree to thread_t with node state in the
  // fair_task_state member.
  struct TaskTraits {
    using KeyType = SchedulerState::KeyType;
    static KeyType GetKey(const thread_t& thread) { return thread.scheduler_state.key(); }
    static bool LessThan(KeyType a, KeyType b) { return a < b; }
    static bool EqualTo(KeyType a, KeyType b) { return a == b; }
    static auto& node_state(thread_t& thread) { return thread.scheduler_state.run_queue_node_; }
  };

  // Alias of the WAVLTree type for the runqueue.
  using RunQueue = fbl::WAVLTree<SchedTime, thread_t*, TaskTraits, TaskTraits>;

  // The runqueue of threads ready to run, but not currently running.
  TA_GUARDED(thread_lock)
  RunQueue run_queue_;

  // Pointer to the thread actively running on this CPU.
  TA_GUARDED(thread_lock)
  thread_t* active_thread_{nullptr};

  // Monotonically increasing counter to break ties when queuing tasks with
  // the same virtual finish time. This has the effect of placing newly
  // queued tasks behind already queued tasks with the same virtual finish
  // time. This is also necessary to guarantee uniqueness of the key as
  // required by the WAVLTree container.
  TA_GUARDED(thread_lock)
  uint64_t generation_count_{0};

  // Count of the threads running on this CPU, including threads in the run
  // queue and the currently running thread. Does not include the idle thread.
  TA_GUARDED(thread_lock)
  int32_t runnable_task_count_{0};

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

  // The global virtual time of this runqueue.
  TA_GUARDED(thread_lock)
  SchedTime virtual_time_{0};

  // The system time since the last update to the global virtual time.
  TA_GUARDED(thread_lock)
  SchedTime last_update_time_ns_{0};

  // The system time that the current time slice started.
  TA_GUARDED(thread_lock)
  SchedTime start_of_current_time_slice_ns_{0};

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
  SchedDuration target_latency_ns_{kDefaultTargetLatency};

  // The scheduling period threshold over which the CPU is considered
  // oversubscribed.
  TA_GUARDED(thread_lock)
  SchedDuration peak_latency_ns_{kDefaultPeakLatency};

  // The CPU this scheduler instance is associated with.
  // NOTE: This member is not initialized to prevent clobbering the value set
  // by sched_early_init(), which is called before the global ctors that
  // initialize the rest of the members of this class.
  // TODO(eieio): Figure out a better long-term solution to determine which
  // CPU is associated with each instance of this class. This is needed by
  // non-static methods that are called from arbitrary CPUs, namely Insert().
  cpu_num_t this_cpu_;
};

#endif  // WITH_FAIR_SCHEDULER

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_SCHEDULER_H_
