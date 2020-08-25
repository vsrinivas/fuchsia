// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOAD_BALANCER_LOAD_BALANCER_H_
#define ZIRCON_KERNEL_LIB_LOAD_BALANCER_LOAD_BALANCER_H_

#include <inttypes.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <lib/ktrace.h>
#include <lib/system-topology.h>
#include <zircon/types.h>

#include <trace.h>

#define LOCAL_TRACE 0

namespace load_balancer {
// Default context is passthrough to global functions.
class Context {
 public:
  template <typename Func>
  static void ForEachPercpu(Func&& func) {
    percpu::ForEach(func);
  }
};

// This class is responsible for taking a global look at the state of all cpus
// on the system and making global decisions about how to guide them towards a
// better balance than they have managed to find with local decisions.
// Currently it generates parameters used in cpu-local decisions everytime
// Cycle() is called.
// Context parameter allows overriding global functions for testing.
template<typename Context = load_balancer::Context>
class LoadBalancer {
 public:
  LoadBalancer() = default;
  LoadBalancer(const LoadBalancer&) = delete;
  LoadBalancer& operator=(const LoadBalancer&) = delete;

  // Run every period of load balancing.
  void Cycle() {
    LTraceDuration trace{"lb_cycle: cpus"_stringref};
    LTRACEF("Load Balancer Cycle Start\n");

    // Zero out the values to be sure we are getting fresh data.
    memset(cpus_.data(), 0, sizeof(Entry) * cpus_.size());

    // Visit all cpus and gather expected runtime
    cpu_count_ = 0;
    Context::ForEachPercpu([&](cpu_num_t logical_id, percpu* cpu) {
      cpus_[cpu_count_++] = Entry{
        .performance = cpu->scheduler.performance_scale(),
        .logical_id = logical_id,
        .queue_time = cpu->scheduler.predicted_queue_time_ns().raw_value(),
      };
      LTRACEF("QueueTime cpu: %u time: %" PRId64 "\n", logical_id, cpus_[cpu_count_ - 1].queue_time);
    });

    threshold_ = CalcThreshold();
    LTRACEF("Threshold: %" PRId64 "\n", threshold_);

    SortEntries();

    CpuState::CpuSet cpus;
    cpus.cpu_count = static_cast<uint8_t>(ktl::min(cpu_count_, cpus.cpus.size()));
    for (int i = 0; i < cpus.cpu_count; i++) {
      LTRACEF("Setting cpu %d to %u\n", i, cpus_[i].logical_id);
      // For now take the globally best cpus, on larger machines we may want to add some
      // random-ness or per-cpu selection.
      cpus.cpus[i] = static_cast<uint8_t>(cpus_[i].logical_id);
    }

    // Visit all cpus (again) and update their parameters
    Context::ForEachPercpu([&](cpu_num_t, percpu* cpu) {
      cpu->load_balancer.Update(cpus, threshold_);
    });

    uint64_t cpus_raw[2];
    memcpy(cpus_raw, &cpus, sizeof(cpus_raw));
    static_assert(sizeof(cpus_raw) == sizeof(cpus));

    trace.End(cpus_raw[0], cpus_raw[1]);
  }

  void PrintState() const {
    printf("Cpu threshold: %" PRId64 "\n", threshold_);
    printf("Cpu Queue times: { ");
    for (uint8_t i = 0; i < cpu_count_; i++) {
      printf("cpu%02u=%8" PRId64 ":%c, ", cpus_[i].logical_id, cpus_[i].queue_time,
             cpus_[i].queue_time > threshold_ ? '^' : '_');
    }
    printf("}\n");
  }

 private:
  struct Entry {
    bool over_threshold = false;
    SchedPerformanceScale performance;
    cpu_num_t logical_id;
    // For a given thread on a cpu this is how long it should expect to queue
    // between each opportunity to run. This is our metric for cpu load.
    zx_duration_t queue_time;
  };

  using LTraceDuration = TraceDuration<TraceEnabled<SCHEDULER_TRACING_LEVEL >= 1>,
                                       KTRACE_GRP_SCHEDULER, TraceContext::Cpu>;


  zx_duration_t CalcThreshold() {
    LTraceDuration trace{"lb_calc_threshold"_stringref};
    // Sum values. Assuming a max of 255 cpus, each cpu could have a duration of
    // 2k years and still not overflow this so no need to do anything fancier.
    zx_duration_t total = 0;
    for (size_t i = 0; i < cpu_count_; i++) {
      total += cpus_[i].queue_time;
    }

    const zx_duration_t mean = total / cpu_count_;
    LTRACEF("Mean: %" PRId64 "   Total: %" PRId64 "   count: %zu\n", mean, total, cpu_count_);

    trace.End(mean, 0);
    return mean;
  }

  void SortEntries() {
    LTraceDuration trace{"lb_sort"_stringref};
    // We will bucket into "over threshold" and not, then sub-sort based on cpu
    // class to favor the most powerful cpus first.
    for (size_t i = 0; i < cpu_count_; i++) {
      cpus_[i].over_threshold = cpus_[i].queue_time > threshold_;
    }

    ktl::stable_sort(cpus_.begin(), cpus_.begin() + cpu_count_, [](const Entry& a, const Entry& b) {
      if (a.over_threshold != b.over_threshold) {
        return a.over_threshold < b.over_threshold;
      }

      return a.performance > b.performance;
    });
  }

  ktl::array<Entry, SMP_MAX_CPUS> cpus_{};
  size_t cpu_count_ = 0;
  zx_duration_t threshold_ = 0;
};

}  // namespace load_balancer

#undef LOCAL_TRACE

#endif  // ZIRCON_KERNEL_LIB_LOAD_BALANCER_LOAD_BALANCER_H_
