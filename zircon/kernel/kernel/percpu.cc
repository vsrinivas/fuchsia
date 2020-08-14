// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/percpu.h"

#include <debug.h>
#include <lib/counters.h>
#include <lib/system-topology.h>

#include <arch/ops.h>
#include <fbl/alloc_checker.h>
#include <ffl/string.h>
#include <kernel/align.h>
#include <kernel/cpu_distance_map.h>
#include <kernel/lockdep.h>
#include <ktl/algorithm.h>
#include <ktl/unique_ptr.h>
#include <lk/init.h>
#include <lockdep/lockdep.h>

decltype(percpu::boot_processor_) percpu::boot_processor_{};
percpu* percpu::secondary_processors_{nullptr};

percpu* percpu::boot_index_[1]{&percpu::boot_processor_};
percpu** percpu::processor_index_{percpu::boot_index_};

size_t percpu::processor_count_{1};

percpu::percpu(cpu_num_t cpu_num) {
  scheduler.this_cpu_ = cpu_num;

#if WITH_LOCK_DEP
  // Initialize the lockdep tracking state for irq context.
  auto* state = reinterpret_cast<lockdep::ThreadLockState*>(&lock_state);
  lockdep::SystemInitThreadLockState(state);
#endif

  counters = CounterArena().CpuData(cpu_num);
}

void percpu::InitializeBoot() { boot_processor_.Initialize(0); }

void percpu::InitializeSecondary(uint32_t /*init_level*/) {
  processor_count_ = CpuDistanceMap::Get().cpu_count();
  DEBUG_ASSERT(processor_count_ != 0);

  const size_t index_size = sizeof(percpu*) * processor_count_;
  processor_index_ = static_cast<percpu**>(memalign(MAX_CACHE_LINE, index_size));
  processor_index_[0] = &boot_processor_;

  static_assert((MAX_CACHE_LINE % alignof(struct percpu)) == 0);

  const size_t bytes = sizeof(percpu) * (processor_count_ - 1);
  secondary_processors_ = static_cast<percpu*>(memalign(MAX_CACHE_LINE, bytes));

  // TODO: Remove the need to zero memory by fully initializing all of percpu
  // members in the constructor / default initializers.
  memset(secondary_processors_, 0, bytes);

  // Construct the secondary percpu instances and add them to the index.
  for (cpu_num_t i = 1; i < processor_count_; i++) {
    processor_index_[i] = &secondary_processors_[i - 1];
    new (&secondary_processors_[i - 1]) percpu{i};
  }

  // Compute the performance scale of each CPU.
  {
    fbl::AllocChecker checker;
    ktl::unique_ptr<uint8_t[]> performance_class{new (&checker) uint8_t[processor_count_]};
    if (checker.check()) {
      uint8_t max_performance_class = 0;
      for (cpu_num_t i = 0; i < processor_count_; i++) {
        performance_class[i] = system_topology::GetPerformanceClass(i);
        max_performance_class = ktl::max(performance_class[i], max_performance_class);
      }

      dprintf(INFO, "CPU performance scales:\n");
      for (cpu_num_t i = 0; i < processor_count_; i++) {
        PerformanceScale scale =
            ffl::FromRatio(performance_class[i] + 1, max_performance_class + 1);
        processor_index_[i]->performance_scale = scale;
        processor_index_[i]->performance_scale_reciprocal = 1 / scale;
        dprintf(INFO, "CPU %2u: %s\n", i, Format(scale).c_str());
      }
    } else {
      dprintf(INFO, "Failed to allocate temp buffer, using default performance for all CPUs\n");
    }
  }

  // Determine the clusters before initializing the CPU search sets.
  CpuSearchSet::AutoCluster(processor_count_);
  CpuSearchSet::DumpClusters();

  // Initialize the search set for each CPU.
  dprintf(INFO, "CPU search order:\n");
  for (cpu_num_t i = 0; i < processor_count_; i++) {
    processor_index_[i]->search_set.Initialize(i, processor_count_);
    processor_index_[i]->search_set.Dump();
  }
}

// Allocate secondary percpu instances before booting other processors, after
// vm and system topology are initialized.
LK_INIT_HOOK(percpu_heap_init, percpu::InitializeSecondary, LK_INIT_LEVEL_KERNEL)
