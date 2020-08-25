// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOAD_BALANCER_INCLUDE_LIB_LOAD_BALANCER_PERCPU_H_
#define ZIRCON_KERNEL_LIB_LOAD_BALANCER_INCLUDE_LIB_LOAD_BALANCER_PERCPU_H_

#include <lib/relaxed_atomic.h>
#include <zircon/types.h>

#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/thread_lock.h>
#include <ktl/array.h>

// TODO(edcoyne): delete this override and default these on.
#ifndef DISABLE_PERIODIC_LOAD_BALANCER
#define DISABLE_PERIODIC_LOAD_BALANCER 1
#endif

namespace load_balancer {

constexpr zx_duration_t kAllowedRuntimeDeviation = Scheduler::kDefaultTargetLatency.raw_value() / 4;

// State stored on a per-cpu basis for the load balancer system.
class CpuState {
 public:
  struct alignas(16) CpuSet {
    // This needs to be limited to 15 cpus to fit in a 128bit atomic.
    // This does not limit the total system cpus to 15 it simply limits the
    // choices of other processors a particular processor will evaluate when it
    // is overloaded and needs to send a thread elsewhere.
    ktl::array<uint8_t, 15> cpus;
    uint8_t cpu_count;

    bool AllValid() const {
      for (int i = 0; i < cpu_count; ++i) {
        if (!is_valid_cpu_num(cpus[i]))
          return false;
      }
      return true;
    }
  };
  // We need to stuff this in an atomic, 128bit is the largest we have.
  static_assert(alignof(CpuSet) == 16 && sizeof(CpuSet) == 16);

  void Update(const CpuSet& cpus, zx_duration_t threshold) {
    DEBUG_ASSERT(cpus.AllValid());
    queue_time_threshold_ = threshold;
    target_cpus_ = cpus;
  }

  zx_duration_t queue_time_threshold() const { return queue_time_threshold_.load(); }
  CpuSet target_cpus() const { return target_cpus_.load(); }

 private:
  // If our total_duration_ns_ exceeds this amount we will try to load shed.
  // We expect this to be managed by the global load balancer.
  RelaxedAtomic<zx_duration_t> queue_time_threshold_{ZX_TIME_INFINITE};

  // If we start shedding load this is an ordered list of other cpus we will
  // consider.
  // We expect this to be set by the global load balancer.
  RelaxedAtomic<CpuSet> target_cpus_{{.cpus = {}, .cpu_count = 0}};
};

// Determines where a newly unblocked thread should run given its last cpu, the
// current cpu and the state of the system.
// This version is unsafe in that it doesn't require the thread_lock. As long as
// the PerCpuProvider it is using is providing isolated thread-safe percpus
// this is safe. If you are using "percpu" as the PerCpuProvider (it is
// confusingly its own container) then you should use the FindTargetCpu()
// function below that requires the thread_lock.
template <typename PerCpuProvider, cpu_num_t curr_cpu_num() = arch_curr_cpu_num>
static cpu_num_t FindTargetCpuLocked(Thread* thread) {
  // Like cpu_num_to_mask but skips validation branching, assumes validated cpu
  // numbers. We validate when we accept the data.
  constexpr auto ToMask = [](cpu_num_t num) { return ((cpu_mask_t)1u << num); };

  constexpr auto GetScheduler = [](cpu_num_t cpu) -> const Scheduler& {
    return PerCpuProvider::Get(cpu).scheduler;
  };
  constexpr auto Get = [](cpu_num_t cpu) -> const CpuState& {
    return PerCpuProvider::Get(cpu).load_balancer;
  };

  // Start on either the last cpu for the thread or the primary load-shed target
  // for the CPU running this logic, it wasn't heavily loaded during the last
  // rebalance.
  const cpu_num_t last_cpu = thread->scheduler_state().last_cpu();
  DEBUG_ASSERT(last_cpu != INVALID_CPU || Get(curr_cpu_num()).target_cpus().cpu_count > 0 ||
               curr_cpu_num() == 0);
  // It is possible the target_cpus is unset in early boot, in this case
  // the cpus[] is initialized to 0, and the initial_cpu is the boot cpu
  // "0", this is a reasonable choice.
  const cpu_num_t initial_cpu =
      last_cpu != INVALID_CPU ? last_cpu : Get(curr_cpu_num()).target_cpus().cpus[0];

  const auto& initial = Get(initial_cpu);
  const CpuState::CpuSet cpus = initial.target_cpus();

  // We lower the threshold by the new thread's expected runtime, this takes into
  // account the new thread's contribution to any core it ends up on and helps
  // keep interactive threads from being excessively moved in the face of cpu-bound
  // threads.
  const zx_duration_t new_thread_runtime = thread->scheduler_state().expected_runtime_ns();
  const zx_duration_t load_shed_threshold =
      zx_duration_sub_duration(initial.queue_time_threshold(), new_thread_runtime);

  const cpu_mask_t available_mask =
      thread->scheduler_state().GetEffectiveCpuMask(mp_get_active_mask());
  const bool initial_cpu_available = ToMask(initial_cpu) & available_mask;
  const zx_duration_t initial_runtime =
      GetScheduler(initial_cpu).predicted_queue_time_ns().raw_value();
  // See if we are ready to shed load.
  if (initial_cpu_available && initial_runtime <= load_shed_threshold) {
    // If we are under the load-shed threshold then stick with this cpu.
    return initial_cpu;
  }

  cpu_num_t lowest_cpu = INVALID_CPU;
  zx_duration_t lowest_runtime = ZX_TIME_INFINITE;

  // Otherwise search the cpu list, in order, to find one that is underloaded.
  // Keep track of least loaded so we can return that if everything is over.
  for (int i = 0; i < cpus.cpu_count && lowest_runtime > load_shed_threshold; ++i) {
    // Skip cpus not available to this task.
    if (unlikely((ToMask(cpus.cpus[i]) & available_mask) == 0))
      continue;

    const zx_duration_t candidate_runtime =
        GetScheduler(cpus.cpus[i]).predicted_queue_time_ns().raw_value();
    if (candidate_runtime < lowest_runtime) {
      lowest_cpu = cpus.cpus[i];
      lowest_runtime = candidate_runtime;
    }
  }

  // If no target cpus are available fallback.
  if (unlikely(lowest_cpu == INVALID_CPU)) {
    if (available_mask != 0) {
      // Fallback to any available cpu.
      lowest_cpu = lowest_cpu_set(available_mask);
    } else {
      DEBUG_ASSERT(mp_get_active_mask() == 0);
      // There are no available cpus we can use, fall back to the cpu this logic
      // is running on, it is clearly up. This violates the threads affinity,
      // but that is inevitable at this point.
      lowest_cpu = arch_curr_cpu_num();
    }
  }

  if (initial_cpu_available &&
      (zx_duration_sub_duration(initial_runtime, lowest_runtime) < kAllowedRuntimeDeviation)) {
    // If the difference between the current cpu and the selected cpu's runtimes
    // is so low that there won't be a significant impact on the system's
    // balance by placing it on that cpu don't move it.
    return initial_cpu;
  }

  if (unlikely(last_cpu != INVALID_CPU && last_cpu != lowest_cpu &&
               thread->has_migrate_fn() && (mp_get_active_mask() & ToMask(last_cpu)))) {
    // Stay where we are, the migrate_fn_ will migrate us later.
    thread->scheduler_state().set_next_cpu(lowest_cpu);
    return last_cpu;
  }

  return lowest_cpu;
}

// Determines where a newly unblocked thread should run given it's last cpu, the
// current cpu and the state of the system.
static inline cpu_num_t FindTargetCpu(Thread* thread) TA_REQ(thread_lock) {
  return FindTargetCpuLocked<percpu>(thread);
}

}  // namespace load_balancer

#endif  // ZIRCON_KERNEL_LIB_LOAD_BALANCER_INCLUDE_LIB_LOAD_BALANCER_PERCPU_H_
