// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/percpu.h>

#include <arch/ops.h>
#include <kernel/align.h>
#include <lib/counters.h>
#include <lk/init.h>

percpu Percpus::boot_percpu_;
percpu* Percpus::other_percpus_ = nullptr;

percpu* Percpus::boot_index_[1]{&Percpus::boot_percpu_};
percpu** Percpus::index_ = Percpus::boot_index_;
size_t Percpus::count_ = 1;

void percpu::Init(cpu_num_t cpu_num) {
    list_initialize(&timer_queue);
    for (unsigned int i = 0; i < NUM_PRIORITIES; i++) {
        list_initialize(&run_queue[i]);
    }

    preempt_timer_deadline = ZX_TIME_INFINITE;
    next_timer_deadline = ZX_TIME_INFINITE;

#if WITH_FAIR_SCHEDULER
    fair_runqueue.this_cpu_ = cpu_num;
#endif

    counters = CounterArena().CpuData(cpu_num);
}

void Percpus::HeapInit(uint32_t) {
    // TODO(ZX-3433) Use a dynamic size.
    count_ = SMP_MAX_CPUS;

    index_ = static_cast<percpu**>(memalign(MAX_CACHE_LINE, sizeof(percpu*) * count_));
    index_[0] = &boot_percpu_;

    static_assert((MAX_CACHE_LINE % alignof(struct percpu)) == 0);

    const size_t bytes = sizeof(percpu) * (count_ - 1);
    other_percpus_ = static_cast<percpu*>(memalign(MAX_CACHE_LINE, bytes));

    // Zero memory out, this replicates the environment of BSS the boot_pecpu is
    // in.
    memset(other_percpus_, 0, bytes);

    // Placement new the structs into the memory to construct embedded objects.
    new (other_percpus_) percpu[count_ - 1];

    for (cpu_num_t i = 1; i < count_; i++) {
        index_[i] = &other_percpus_[i - 1];
        index_[i]->Init(i);
    }
}

// We need to bring up the heap percpus before booting any other cores. We expect the heap to come
// up before threading.
LK_INIT_HOOK(percpu_heap_init, Percpus::HeapInit, LK_INIT_LEVEL_HEAP)
