// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/auto_preempt_disabler.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <lib/arch/intrin.h>

namespace {

// Test that the kernel is able to handle interrupts when half the kernel stack is used.
bool kstack_interrupt_depth_test() {
  BEGIN_TEST;
  constexpr size_t kSize = DEFAULT_STACK_SIZE / 2;
  volatile uint8_t buffer[kSize] = {};

  // Spin for a bit while we have a large, active buffer on the kernel stack.
  // This gives us a window for interrupts to occur; any that do will have to make do with half
  // the stack consumed.
  int i = 0;
  zx_time_t now = current_time();
  while (current_time() < now + ZX_MSEC(100)) {
    buffer[i++ % kSize] = buffer[0];  // Touch the buffer to ensure it is not optimized out
    arch::Yield();
  }

  END_TEST;
}

#if __has_feature(safe_stack)
__attribute__((no_sanitize("safe-stack")))
bool kstack_interrupt_depth_test_no_safestack() {
  BEGIN_TEST;
  constexpr size_t kSize = DEFAULT_STACK_SIZE / 2;
  volatile uint8_t buffer[kSize] = {};

  // Spin for a bit while we have a large, active buffer on the kernel stack.
  // Just like the above test, though with safe-stack disabled.
  int i = 0;
  zx_time_t now = current_time();
  while (current_time() < now + ZX_MSEC(100)) {
    buffer[i++ % kSize] = buffer[0];  // Touch the buffer to ensure it is not optimized out
    arch::Yield();
  }

  END_TEST;
}
#endif

static unsigned get_num_cpus_online() {
  unsigned count = 0;
  cpu_mask_t online = mp_get_online_mask();
  while (online) {
    online >>= 1;
    ++count;
  }
  return count;
}

struct context {
  cpu_num_t cpu_to_wake;
  ktl::atomic<bool> cpu_to_wake_ok;
  ktl::atomic<bool> wake;
};
static int waiter_thread(void* arg) {
  constexpr size_t kSize = DEFAULT_STACK_SIZE / 2;
  volatile uint8_t buffer[kSize] = {};

  context* const deferred = reinterpret_cast<context*>(arg);
  AutoPreemptDisabler preempt_disable;
  // Lock our thread to the current CPU.
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(arch_curr_cpu_num()));
  deferred->cpu_to_wake = arch_curr_cpu_num();
  deferred->cpu_to_wake_ok.store(true);
  buffer[1] = buffer[0];  // Touch the buffer to ensure it is not optimized out
  for (;;) {
    // Wait for a bit while we have a large, active buffer on the kernel stack.
    // mp_sync_exec()'s callback will run on our CPU; we want to check that it succeeds even when
    // kSize bytes of kernel stack space are consumed by (this) thread context.
    if (deferred->wake.load() == true)
      break;
    arch::Yield();
  }

  return 0;
}
// Test that we can handle an mp_sync_exec callback while half the kernel stack is used.
bool kstack_mp_sync_exec_test() {
  BEGIN_TEST;
  // We need 2 or more CPUs for this test.
  if (get_num_cpus_online() < 2) {
    printf("not enough online cpus\n");
    END_TEST;
  }

  context deferred = { };
  Thread* const thread = Thread::CreateEtc(nullptr, "waiter", waiter_thread, &deferred,
                                           DEFAULT_PRIORITY, nullptr);
  thread->Resume();
  while (deferred.cpu_to_wake_ok.load() == false);
  mp_sync_exec(MP_IPI_TARGET_MASK, cpu_num_to_mask(deferred.cpu_to_wake),
               [](void* arg) { reinterpret_cast<ktl::atomic<bool>*>(arg)->store(true); }, &deferred.wake);

  thread->Join(nullptr, ZX_TIME_INFINITE);
  END_TEST;
}

}  // anonymous namespace

UNITTEST_START_TESTCASE(kstack_tests)
UNITTEST("kstack-interrupt-depth", kstack_interrupt_depth_test)
#if __has_feature(safe_stack)
UNITTEST("kstack-interrupt-depth-no-safestack", kstack_interrupt_depth_test_no_safestack)
#endif
UNITTEST("kstack-mp-sync-exec", kstack_mp_sync_exec_test)
UNITTEST_END_TESTCASE(kstack_tests, "kstack", "kernel stack tests")
