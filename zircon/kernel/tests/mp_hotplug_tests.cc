// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <zircon/types.h>

#include <kernel/mp.h>
#include <kernel/thread.h>

#include "tests.h"

static int resume_cpu_test_thread(void* arg) {
  *reinterpret_cast<uint*>(arg) = arch_curr_cpu_num();
  return 0;
}

// "Unplug" online secondary (non-BOOT) cores
static zx_status_t unplug_all_cores(Thread** leaked_threads) {
  cpu_mask_t cpumask = mp_get_online_mask() & ~cpu_num_to_mask(BOOT_CPU_ID);
  return mp_unplug_cpu_mask(cpumask, leaked_threads);
}

static zx_status_t hotplug_core(uint i) {
  cpu_mask_t cpumask = cpu_num_to_mask(i);
  return mp_hotplug_cpu_mask(cpumask);
}

static unsigned get_num_cpus_online() {
  unsigned count = 0;
  cpu_mask_t online = mp_get_online_mask();
  while (online) {
    online >>= 1;
    ++count;
  }
  return count;
}

// Unplug all cores (except for Boot core), then hotplug
// the cores one by one and make sure that we can schedule
// tasks on that core.
[[maybe_unused]]
static bool mp_hotplug_test() {
  BEGIN_TEST;

// Hotplug is only implemented for x64.
#if !defined(__x86_64__)
  printf("skipping test mp_hotplug, hotplug only suported on x64\n");
  END_TEST;
#endif
  uint num_cores = get_num_cpus_online();
  if (num_cores < 2) {
    printf("skipping test mp_hotplug, not enough online cpus\n");
    END_TEST;
  }
  Thread::Current::MigrateToCpu(BOOT_CPU_ID);
  // "Unplug" online secondary (non-BOOT) cores
  Thread* leaked_threads[SMP_MAX_CPUS] = {};
  ASSERT_EQ(unplug_all_cores(leaked_threads), ZX_OK, "unplugging all cores failed");
  for (uint i = 0; i < num_cores; i++) {
    if (i == BOOT_CPU_ID) {
      continue;
    }
    // hotplug this core.
    ASSERT_EQ(hotplug_core(i), ZX_OK, "hotplugging core failed");
    // Create a thread, affine it to the core just hotplugged
    // and make sure the thread does get scheduled there.
    uint running_core;
    Thread* nt = Thread::Create("resume-test-thread", resume_cpu_test_thread, &running_core,
                                DEFAULT_PRIORITY);
    ASSERT_NE(nullptr, nt, "Thread create failed");
    nt->SetCpuAffinity(cpu_num_to_mask(i));
    nt->Resume();
    ASSERT_EQ(nt->Join(nullptr, ZX_TIME_INFINITE), ZX_OK, "thread join failed");
    ASSERT_EQ(i, running_core, "Thread not running on hotplugged core");
  }

  for (unsigned i = 0; i < fbl::count_of(leaked_threads); i++) {
    if (leaked_threads[i]) {
      leaked_threads[i]->Forget();
    }
  }

  END_TEST;
}

// The call to x86_bootstrap16_acquire() from the mp_hotplug_cpu_mask()
// fails because the PMM doesn't support allocations to low 4GB pages (ZX-978).
// Enable these tests once that issue is fixed.
// See FLK-387. (Call to x86_bootstrap16_acquire() from mp_hotplug_cpu_mask()).
// UNITTEST_START_TESTCASE(mp_hotplug_tests)
// UNITTEST("test unplug and hotplug cores one by one", mp_hotplug_test)
// UNITTEST_END_TESTCASE(mp_hotplug_tests, "hotplug",
//                       "Tests for unplugging and hotplugging cores");
