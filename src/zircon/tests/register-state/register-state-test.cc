// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#if defined(__x86_64__)

#include <cpuid.h>
#include <x86intrin.h>

static pthread_barrier_t g_barrier;

// Returns whether the CPU supports the {rd,wr}{fs,gs}base instructions.
static bool x86_feature_fsgsbase() {
  uint32_t eax, ebx, ecx, edx;
  __cpuid_count(7, 0, eax, ebx, ecx, edx);
  return ebx & bit_FSGSBASE;
}

__attribute__((target("fsgsbase"))) static void* gs_base_test_thread(void* thread_arg) {
  uintptr_t gs_base = (uintptr_t)thread_arg;
  uintptr_t fs_base = 0;
  if (x86_feature_fsgsbase()) {
    _writegsbase_u64(gs_base);
    // We don't want to modify fs_base because it is used by libc etc.,
    // but we might as well check that it is also preserved.
    fs_base = _readfsbase_u64();
  }

  // Wait until all the test threads reach this point.
  int rv = pthread_barrier_wait(&g_barrier);
  EXPECT_TRUE(rv == 0 || rv == PTHREAD_BARRIER_SERIAL_THREAD);

  if (x86_feature_fsgsbase()) {
    EXPECT_TRUE(_readgsbase_u64() == gs_base);
    EXPECT_TRUE(_readfsbase_u64() == fs_base);
  }

  return nullptr;
}

// This tests whether the gs_base register on x86 is preserved across
// context switches.
//
// We do this by launching multiple threads that set gs_base to different
// values.  After all the threads have set gs_base, the threads wake up and
// check that gs_base was preserved.
TEST(RegisterStateTest, ContextSwitchOfGsBase) {
  // We run the rest of the test even if the fsgsbase instructions aren't
  // available, so that at least the test's threading logic gets
  // exercised.
  printf("fsgsbase available = %d\n", x86_feature_fsgsbase());

  // We launch more threads than there are CPUs.  This ensures that there
  // should be at least one CPU that has >1 of our threads scheduled on
  // it, so saving and restoring gs_base between those threads should get
  // exercised.
  uint32_t thread_count = zx_system_get_num_cpus() * 2;
  ASSERT_GT(thread_count, 0);

  pthread_t tids[thread_count];
  ASSERT_EQ(pthread_barrier_init(&g_barrier, nullptr, thread_count), 0);
  for (uint32_t i = 0; i < thread_count; ++i) {
    // Give each thread a different test value for gs_base.
    void* gs_base = (void*)(uintptr_t)(i * 0x10004);
    ASSERT_EQ(pthread_create(&tids[i], nullptr, gs_base_test_thread, gs_base), 0);
  }
  for (uint32_t i = 0; i < thread_count; ++i) {
    ASSERT_EQ(pthread_join(tids[i], nullptr), 0);
  }
  ASSERT_EQ(pthread_barrier_destroy(&g_barrier), 0);
}

#define DEFINE_REGISTER_ACCESSOR(REG)                     \
  static inline void set_##REG(uint16_t value) {          \
    __asm__ volatile("mov %0, %%" #REG : : "r"(value));   \
  }                                                       \
  static inline uint16_t get_##REG(void) {                \
    uint16_t value;                                       \
    __asm__ volatile("mov %%" #REG ", %0" : "=r"(value)); \
    return value;                                         \
  }

DEFINE_REGISTER_ACCESSOR(ds)
DEFINE_REGISTER_ACCESSOR(es)
DEFINE_REGISTER_ACCESSOR(fs)
DEFINE_REGISTER_ACCESSOR(gs)

#undef DEFINE_REGISTER_ACCESSOR

// This test demonstrates that if the segment selector registers are set to
// 1, they will eventually be reset to 0 when an interrupt occurs.  This is
// mostly a property of the x86 architecture rather than the kernel: The
// IRET instruction has the side effect of resetting these registers when
// returning from the kernel to userland (but not when returning to kernel
// code).
TEST(RegisterStateTest, SegmentSelectorsZeroedOnInterrupt) {
  // Disable this test because some versions of non-KVM QEMU don't
  // implement the part of IRET described above.
  //
  // TODO(fxbug.dev/34369): Replace this return statement with ZXTEST_SKIP.
  return;

  // We skip setting %fs because that breaks libc's TLS.
  set_ds(1);
  set_es(1);
  set_gs(1);

  // This could be interrupted by an interrupt that causes a context
  // switch, but on an unloaded machine it is more likely to be
  // interrupted by an interrupt where the handler returns without doing
  // a context switch.
  while (get_gs() == 1)
    __asm__ volatile("pause");

  EXPECT_EQ(get_ds(), 0);
  EXPECT_EQ(get_es(), 0);
  EXPECT_EQ(get_gs(), 0);
}

__attribute__((target("fsgsbase"))) static uintptr_t read_gs_base() { return _readgsbase_u64(); }

__attribute__((target("fsgsbase"))) static uintptr_t read_fs_base() { return _readfsbase_u64(); }

__attribute__((target("fsgsbase"))) static void write_gs_base(uintptr_t gs_base) {
  return _writegsbase_u64(gs_base);
}

__attribute__((target("fsgsbase"))) static void write_fs_base(uintptr_t fs_base) {
  return _writefsbase_u64(fs_base);
}

// Test that the kernel also resets the segment selector registers on a
// context switch, to avoid leaking their values and to match what happens
// on an interrupt.
TEST(RegisterStateTest, SegmentSelectorsZeroedOnContextSwitch) {
  set_gs(1);
  uintptr_t orig_fs_base = 0;
  if (x86_feature_fsgsbase()) {
    // libc uses fs_base so we must save its original value before setting it.  Also, once we've set
    // it, we must be very careful to not call any code that might use fs_base (transitively) until
    // we have restored the original value.
    orig_fs_base = read_fs_base();

    set_gs(1);
    write_gs_base(1);
    set_fs(1);
    write_fs_base(1);

    // Now that we've set fs_base, we must not touch any TLS (thread-local storage) call anything
    // that might touch TLS until we have stored the original value.
  }
  set_es(1);
  set_ds(1);

  // Now that all the registers have now been set to 1, sleep repeatedly until
  // the segment selector registers have been cleared. Of course it's possible
  // that a context switch has already occured and cleared some or all of them
  // so be sure to only terminate the loop once we have observed the last one
  // set (ds) was cleared.
  //
  // Sleeping should cause a context switch away from this thread (to the
  // kernel's idle thread) and another context switch back.
  //
  // Why loop? A single short sleep may not be sufficient to trigger a context
  // switch. By the time this thread has entered the kernel, the duration may
  // have already elapsed.
  //
  // This test is not as precise as we'd like it to be. It is possible that
  // this thread will be interrupted by an interrupt, which would also clear
  // the segment selector registers. Keep the sleep duration short to reduce
  // the chance of that happening.
  zx_duration_t duration = ZX_MSEC(1);
  zx_status_t status = ZX_OK;
  while (get_ds() == 1 && duration < ZX_SEC(10)) {
    status = zx_nanosleep(zx_deadline_after(duration));
    if (status != ZX_OK) {
      break;
    }
    duration *= 2;
  }

  if (x86_feature_fsgsbase()) {
    // Save gs_base and fs_base.  We'll verify they are 0 after we've restored the original fs_base.
    uintptr_t gs_base = read_gs_base();
    uintptr_t fs_base = read_fs_base();
    // Restore fs_base.
    write_fs_base(orig_fs_base);

    // See that gs_base and fs_base are preserved across a context switch.
    EXPECT_EQ(gs_base, 1);
    EXPECT_EQ(fs_base, 1);
  }
  ASSERT_OK(status);

  // See that ds, es, fs, and gs are cleared by a context switch.
  EXPECT_EQ(get_ds(), 0);
  EXPECT_EQ(get_es(), 0);
  EXPECT_EQ(get_fs(), 0);
  EXPECT_EQ(get_gs(), 0);
}

#endif
