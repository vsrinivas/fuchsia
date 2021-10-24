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

// Test that we can handle an mp_sync_exec callback while half the kernel stack is used.
bool kstack_mp_sync_exec_test() {
  BEGIN_TEST;

  // We need 2 or more CPUs for this test, CPU-A and CPU-B.  The thread calling into this test will
  // be pinned to CPU-A and the thread spawned by this test will be pinned to CPU-B.
  cpu_mask_t mask = mp_get_active_mask();
  const cpu_num_t cpu_a = remove_cpu_from_mask(mask);
  const cpu_num_t cpu_b = remove_cpu_from_mask(mask);
  if (cpu_a == INVALID_CPU || cpu_b == INVALID_CPU) {
    printf("not enough active cpus; skipping test\n");
    END_TEST;
  }

  struct Context {
    ktl::atomic<bool> ready;
    ktl::atomic<bool> done;
  };
  Context context = {};

  thread_start_routine spin_fn = [](void* arg) {
    // Ensure that no other thread runs on this CPU for the duration of the test.  The goal here is
    // have the IPI interrupt *this* thread and push its frame onto *this* thread's stack.
    AutoPreemptDisabler preempt_disable;

    constexpr size_t kSize = DEFAULT_STACK_SIZE / 2;
    volatile uint8_t buffer[kSize] = {};

    Context* const context = reinterpret_cast<Context*>(arg);
    context->ready.store(true);
    buffer[1] = buffer[0];  // Touch the buffer to ensure it is not optimized out
    while (!context->done.load()) {
      // Wait for a bit while we have a large, active buffer on the kernel stack.
      // mp_sync_exec()'s callback will run on our CPU; we want to check that it succeeds even when
      // kSize bytes of kernel stack space are consumed by (this) thread context.
      arch::Yield();
    }
    return 0;
  };

  // Current thread runs on CPU-A...
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(cpu_a));

  // and |spin_fn| runs on CPU-B.
  Thread* const thread =
      Thread::CreateEtc(nullptr, "waiter", spin_fn, &context, DEFAULT_PRIORITY, nullptr);
  thread->SetCpuAffinity(cpu_num_to_mask(cpu_b));
  thread->Resume();

  while (!context.ready.load()) {
    arch::Yield();
  }
  mp_sync_exec(
      MP_IPI_TARGET_MASK, cpu_num_to_mask(cpu_b),
      [](void* arg) { reinterpret_cast<Context*>(arg)->done.store(true); }, &context);

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
