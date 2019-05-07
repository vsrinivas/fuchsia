// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <arch/ops.h>
#include <kernel/align.h>
#include <kernel/event.h>
#include <kernel/fair_scheduler.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <list.h>
#include <sys/types.h>
#include <vm/page_state.h>
#include <zircon/compiler.h>

struct percpu;

// Static collection of the percpu objects for each cpu on the system.
class Percpus {
 public:
    // Pure static class.
    Percpus() = delete;

    // Get the |cpu_num|th percpu entry.
    static inline percpu& Get(cpu_num_t cpu_num) {
        DEBUG_ASSERT_MSG(cpu_num < count_,
                         "%u < %zu", cpu_num, count_);
        return *index_[cpu_num];
    }

    // Number of percpu entries.
    static size_t Count() {
        return count_;
    }

    // Called once after the heap is up to initialize the secondary percpu
    // entries.
    // Unused argument is so this can be passed to LK_INIT_HOOK() directly.
    static void HeapInit(uint32_t);

    // Call |Func| with the current CPU's percpu struct with preemption disabled.
    //
    // |Func| should accept a |percpu*| and should not block.
    template <typename Func>
    static void WithCurrentPreemptDisable(Func&& func);

    // Call |Func| once per CPU with each CPU's percpu struct with preemption disabled.
    //
    // |Func| should accept a |percpu*| and should not block.
    template <typename Func>
    static void ForEachPreemptDisable(Func&& func);

private:
    // Number of percpu entries.
    static size_t count_;

    // The percpu for the boot core, lives in static memory.
    static percpu boot_percpu_ __CPU_ALIGN_EXCLUSIVE;

    // Pointer to heap memory allocated for additional percpus.
    static percpu* other_percpus_;

    // Index that points to all available percpus by cpu_num.
    static percpu** index_;

    // Index used in early boot when only the boot core's percpu is available.
    static percpu* boot_index_[1];
};

struct percpu {
    percpu() = default;
    percpu(const percpu &) = delete;
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

#if WITH_FAIR_SCHEDULER
    FairScheduler fair_runqueue;
#endif

#if WITH_LOCK_DEP
    // state for runtime lock validation when in irq context
    lockdep_state_t lock_state;
#endif

    // thread/cpu level statistics
    struct cpu_stats stats;

    // per cpu idle thread
    thread_t idle_thread;

    // kernel counters arena
    int64_t* counters;

    // dpc context
    list_node_t dpc_list;
    event_t dpc_event;
    // request the dpc thread to stop by setting to true; guarded by dpc_lock
    bool dpc_stop;
    // each cpu has a dedicated thread for processing dpcs
    thread_t* dpc_thread;

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

    // Initialize this percpu object, |cpu_num| will be used to initialize
    // embedded objects.
    void Init(cpu_num_t cpu_num);

    // Get the |cpu_num|th percpu entry, this is a conveinence method for
    // interfacing with Percpus directly.
    static inline percpu& Get(cpu_num_t cpu_num) {
        return Percpus::Get(cpu_num);
    }

    // Number of percpu entries.
    static size_t Count() {
        return Percpus::Count();
    }

} __CPU_ALIGN;

// make sure the bitmap is large enough to cover our number of priorities
static_assert(NUM_PRIORITIES <= sizeof(((percpu*)0)->run_queue_bitmap) * CHAR_BIT, "");

// TODO(edcoyne): rename this to c++, or wait for travis's work to integrate
// into arch percpus.
static inline struct percpu* get_local_percpu(void) {
    return &Percpus::Get(arch_curr_cpu_num());
}

template <typename Func>
void Percpus::WithCurrentPreemptDisable(Func&& func) {
    thread_preempt_disable();
    func(&Percpus::Get(arch_curr_cpu_num()));
    thread_preempt_reenable();
}

template <typename Func>
void Percpus::ForEachPreemptDisable(Func&& func) {
    thread_preempt_disable();
    const size_t count = Percpus::Count();
    for (cpu_num_t cpu_num = 0; cpu_num < count; ++cpu_num) {
        func(&Percpus::Get(cpu_num));
    }
    thread_preempt_reenable();
}
