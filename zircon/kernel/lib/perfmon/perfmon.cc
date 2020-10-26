// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// A note on the distribution of code between us and the userspace driver:
// The default location for code is the userspace driver. Reasons for
// putting code here are: implementation requirement (need ring zero to write
// MSRs), stability, and performance. The device driver should do as much
// error checking as possible before calling us.
// Note that we do a lot of verification of the input configuration:
// We don't want to be compromised if the userspace driver gets compromised.

// A note on terminology: "events" vs "counters": A "counter" is an
// "event", but some events are not counters. Internally, we use the
// term "counter" when we know the event is a counter.

#include <ktl/atomic.h>
#include <lib/perfmon.h>

bool perfmon_supported = false;

ktl::atomic<int> perfmon_active;

PerfmonStateBase::PerfmonStateBase(unsigned n_cpus) : num_cpus(n_cpus) {}

PerfmonStateBase::~PerfmonStateBase() {
  DEBUG_ASSERT(!perfmon_active.load());

  if (cpu_data) {
    for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
      auto data = &cpu_data[cpu];
      data->~PerfmonCpuData();
    }
    // This frees the memory allocated by |memalign()|.
    free(cpu_data);
  }
}

bool PerfmonStateBase::AllocatePerCpuData() {
  DEBUG_ASSERT(cpu_data == nullptr);

  size_t space_needed = sizeof(PerfmonCpuData) * num_cpus;
  auto cdata = reinterpret_cast<PerfmonCpuData*>(memalign(alignof(PerfmonCpuData), space_needed));
  if (!cdata) {
    return false;
  }

  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    new (&cdata[cpu]) PerfmonCpuData();
  }

  cpu_data = cdata;
  return true;
}
