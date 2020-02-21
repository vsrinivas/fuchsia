// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_PERCPU_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_PERCPU_H_

#include <lib/lazy_init/lazy_init.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <arch/ops.h>
#include <kernel/align.h>
#include <kernel/event.h>
#include <kernel/scheduler.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <ktl/forward.h>
#include <lockdep/thread_lock_state.h>
#include <vm/page_state.h>

struct percpu {
  percpu(cpu_num_t cpu_num);

  percpu(const percpu&) = delete;
  percpu operator=(const percpu&) = delete;

  // per cpu timer queue
  struct list_node timer_queue;

  // per cpu preemption timer; ZX_TIME_INFINITE means not set
  zx_time_t preempt_timer_deadline;

  // deadline of this cpu's platform timer or ZX_TIME_INFINITE if not set
  zx_time_t next_timer_deadline;

  // per cpu run queue and bitmap to indicate which queues are non empty
  struct list_node run_queue[NUM_PRIORITIES];
  uint32_t run_queue_bitmap;

#if WITH_FAIR_SCHEDULER || WITH_UNIFIED_SCHEDULER
  Scheduler scheduler;
#endif

#if WITH_LOCK_DEP
  // state for runtime lock validation when in irq context
  lockdep::ThreadLockState lock_state;
#endif

  // thread/cpu level statistics
  struct cpu_stats stats;

  // per cpu idle thread
  Thread idle_thread;

  // kernel counters arena
  int64_t* counters;

  // dpc context
  list_node_t dpc_list;
  event_t dpc_event;
  // request the dpc thread to stop by setting to true; guarded by dpc_lock
  bool dpc_stop;
  // each cpu has a dedicated thread for processing dpcs
  Thread* dpc_thread;

  // Page state counts are percpu because they change frequently and we don't want to pay for
  // synchronization, including atomic load/add/subtract.
  //
  // While it's OK for an observer to temporarily see incorrect values, the counts need to
  // eventually quiesce. It's important that we don't "drop" changes and that the values don't
  // drift over time.
  //
  // When modifying, use |WithCurrentPreemptDisable|.
  //
  // When reading, use |ForEachPreemptDisable|. Although it is not possible to guarantee a
  // consistent snapshot of these counters, it should be good enough for diagnostic uses.
  vm_page_counts_t vm_page_counts;

  // Returns a reference to the percpu instance for given CPU number.
  static percpu& Get(cpu_num_t cpu_num) {
    DEBUG_ASSERT(cpu_num < processor_count());
    return *processor_index_[cpu_num];
  }

  // Returns the number of percpu instances.
  static size_t processor_count() { return processor_count_; }

  // Called once during early init to initalize the percpu data for the boot
  // processor.
  static void InitializeBoot();

  // Called once after heap init to initialize the percpu data for the
  // secondary processors.
  static void InitializeSecondary(uint32_t init_level);

  // Call |Func| with the current CPU's percpu struct with preemption disabled.
  //
  // |Func| should accept a |percpu*|.
  template <typename Func>
  static void WithCurrentPreemptDisable(Func&& func) {
    Thread::Current::PreemptDisable();
    ktl::forward<Func>(func)(&Get(arch_curr_cpu_num()));
    Thread::Current::PreemptReenable();
  }

  // Call |Func| once per CPU with each CPU's percpu struct with preemption disabled.
  //
  // |Func| should accept a |percpu*|.
  template <typename Func>
  static void ForEachPreemptDisable(Func&& func) {
    Thread::Current::PreemptDisable();
    for (cpu_num_t cpu_num = 0; cpu_num < processor_count(); ++cpu_num) {
      ktl::forward<Func>(func)(&Get(cpu_num));
    }
    Thread::Current::PreemptReenable();
  }

 private:
  // Number of percpu entries.
  static size_t processor_count_;

  // The percpu for the boot processor.
  static lazy_init::LazyInit<percpu, lazy_init::CheckType::Basic> boot_processor_
      __CPU_ALIGN_EXCLUSIVE;

  // Pointer to heap memory allocated for additional percpu instances.
  static percpu* secondary_processors_;

  // Translates from CPU number to percpu instance. Some or all instances
  // of percpu may be discontiguous.
  static percpu** processor_index_;

  // Temporary translation table with one entry for use in early boot.
  static percpu* boot_index_[1];
} __CPU_ALIGN;

// make sure the bitmap is large enough to cover our number of priorities
static_assert(NUM_PRIORITIES <= sizeof(((percpu*)0)->run_queue_bitmap) * CHAR_BIT, "");

// TODO(edcoyne): rename this to c++, or wait for travis's work to integrate
// into arch percpus.
static inline struct percpu* get_local_percpu(void) { return &percpu::Get(arch_curr_cpu_num()); }

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_PERCPU_H_
