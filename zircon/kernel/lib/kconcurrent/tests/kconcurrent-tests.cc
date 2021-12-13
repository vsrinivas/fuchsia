// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/kconcurrent/seqlock.h>
#include <lib/stdcompat/bit.h>
#include <lib/unittest/unittest.h>

#include <kernel/mp.h>
#include <ktl/type_traits.h>

namespace {

struct SeqLockWrapper {
  DECLARE_SEQLOCK(SeqLockWrapper) seq;
};

class CurrentCpuPinner {
 public:
  CurrentCpuPinner() {
    interrupt_saved_state_t interrupt_state = arch_interrupt_save();

    prev_affinity_ = Thread::Current::Get()->GetCpuAffinity();
    pin_mask_ = cpu_num_to_mask(arch_curr_cpu_num());
    Thread::Current::Get()->SetCpuAffinity(pin_mask_);

    arch_interrupt_restore(interrupt_state);
  }

  ~CurrentCpuPinner() { ReleasePin(); }

  void ReleasePin() {
    if (pin_mask_ != 0) {
      Thread::Current::Get()->SetCpuAffinity(prev_affinity_);
      pin_mask_ = 0;
    }
  }

  cpu_mask_t other_cpus_mask() const { return ~pin_mask_; }

 private:
  cpu_mask_t prev_affinity_{0};
  cpu_mask_t pin_mask_{0};
};

template <typename LockPolicy>
static bool UncontestedRead() {
  BEGIN_TEST;

  static_assert(ktl::is_same_v<LockPolicy, SharedIrqSave> ||
                ktl::is_same_v<LockPolicy, SharedNoIrqSave>);
  constexpr bool kExpectIrqsDisabled = ktl::is_same_v<LockPolicy, SharedIrqSave>;

  SeqLockWrapper wrapper;
  auto& seq = wrapper.seq;

  // Observe the lock's initial sequence number.  It should not change over the
  // course of these tests.
  SeqLock::SequenceNumber initial_num = seq.lock().seq_num();

  // Deliberately initialize this as true.  We want to test to be sure that the
  // guard unconditionally sets it's state to false as we enter the guard.
  bool transaction_success{true};
  {
    ASSERT_FALSE(arch_ints_disabled());  // interrupts should be enabled before we enter the guard.
    ASSERT_TRUE(transaction_success);

    // Enter the guard.  Interrupt enabled/disabled state should match what is
    // expected based on the policy. Transaction_success should be now have been
    // explicitly set to false.
    lockdep::Guard<SeqLock, LockPolicy> guard{&seq, transaction_success};
    ASSERT_EQ(kExpectIrqsDisabled, arch_ints_disabled());
    ASSERT_FALSE(transaction_success);
    ASSERT_EQ(initial_num, seq.lock().seq_num());

    // Now let the guard go out of scope.
  }

  // Interrupts should be enabled (if they had been disabled), and the
  // transaction should have succeeded.
  ASSERT_FALSE(arch_ints_disabled());
  ASSERT_TRUE(transaction_success);
  ASSERT_EQ(initial_num, seq.lock().seq_num());

  END_TEST;
}

template <typename LockPolicy>
static bool UncontestedWrite() {
  BEGIN_TEST;

  static_assert(ktl::is_same_v<LockPolicy, ExclusiveIrqSave> ||
                ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>);
  SeqLockWrapper wrapper;
  auto& seq = wrapper.seq;

  // Observe the lock's initial sequence number.  It should go up by exactly one
  // every time we enter or exit the lock.
  SeqLock::SequenceNumber initial_num = seq.lock().seq_num();

  // interrupts should be enabled and blocking should be allowed before we
  // enter the guard.
  ASSERT_FALSE(arch_ints_disabled());
  ASSERT_FALSE(arch_blocking_disallowed());
  ASSERT_EQ(initial_num, seq.lock().seq_num());

  // If we are using the IRQ save version of the guard, then we expect it to
  // disable interrupts and disallow blocking for us.  The NoIrqSave version
  // is expected to DEBUG_ASSERT if interrupts are not already disabled,
  // meaning that we need to take care of this ourselves, but we expect it to
  // make sure that blocking is disallowed while we are in the guard.
  [[maybe_unused]] interrupt_saved_state_t interrupt_state;
  if constexpr (ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>) {
    interrupt_state = arch_interrupt_save();
  }

  {
    // Enter the guard. Interrupts should now be disabled, and blocking
    // disallowed. The lock's sequence number should have gone up by one.
    lockdep::Guard<SeqLock, LockPolicy> guard{&seq};
    ASSERT_TRUE(arch_ints_disabled());
    ASSERT_TRUE(arch_blocking_disallowed());
    ASSERT_EQ(initial_num + 1, seq.lock().seq_num());

    // Now let the guard go out of scope.
  }

  // Restore interrupts if we had manually disabled them.
  if constexpr (ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>) {
    arch_interrupt_restore(interrupt_state);
  }

  // Interrupts should now be enabled and blocking allowed again.  The seq
  // number should be 2 more than the initial number.
  ASSERT_FALSE(arch_ints_disabled());
  ASSERT_FALSE(arch_blocking_disallowed());
  ASSERT_EQ(initial_num + 2, seq.lock().seq_num());

  END_TEST;
}

template <typename LockPolicy>
static bool ContestedTest() {
  BEGIN_TEST;

  static_assert(ktl::is_same_v<LockPolicy, SharedIrqSave> ||
                ktl::is_same_v<LockPolicy, SharedNoIrqSave> ||
                ktl::is_same_v<LockPolicy, ExclusiveIrqSave> ||
                ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>);

  constexpr bool SharedTest =
      ktl::is_same_v<LockPolicy, SharedIrqSave> || ktl::is_same_v<LockPolicy, SharedNoIrqSave>;

  SeqLockWrapper wrapper;
  auto& seq = wrapper.seq;

  if constexpr (SharedTest) {
    // If we are testing shared contention, start with the simple test.  Start a
    // read transaction, but then have a "writer" enter the lock exclusively
    // during the read transaction.  The transaction should fail.  Note: to keep
    // things simple, we don't actually need or want to spin a thread for the
    // writer.  Instead, we simply simulate one by accessing the lock directly
    // with static thread analysis checking disabled.
    bool transaction_success{true};
    {
      // Enter the guard.
      ASSERT_TRUE(transaction_success);
      lockdep::Guard<SeqLock, LockPolicy> guard{&seq, transaction_success};
      ASSERT_FALSE(transaction_success);

      // Have a "writer" enter the lock exclusively
      [&seq]() __TA_NO_THREAD_SAFETY_ANALYSIS { seq.lock().Acquire(); }();

      // Let the guard go out of scope.
    }

    // Go ahead and release the exclusive access to the lock.
    [&seq]() __TA_NO_THREAD_SAFETY_ANALYSIS { seq.lock().Release(); }();

    // The transaction should have failed.
    ASSERT_FALSE(transaction_success);
  }

  // Now check to make sure that guards (either shared or exclusive) cannot be
  // entered when the lock is already held exclusively. Create a thread, and
  // wait until know that the thread is about to enter the guard. Then wait just
  // a bit longer and verify that the thread has still not managed to enter the
  // guard.  Finally, we release the exclusive hold we have on the lock and
  // verify that the thread is able to make it through the guard, and in the
  // case that the thread is using a shared guard, that the read transaction is
  // reported as a success.
  //
  // Note: This is a best effort test, and contains false-negative potential.
  // Just because the thread had not managed to make it into the guard in X
  // units of time, does not mean that it won't eventually make it in.  There is
  // simply no way with a runtime unit-test to _prove_ that exclusion will occur
  // until the lock is released.
  //
  if (uint32_t cpus_online = cpp20::popcount(mp_get_online_mask()); cpus_online < 2) {
    printf("Skipping Contested %s SeqLock test.  There is only %u CPU online\n",
           SharedTest ? "Read" : "Write", cpus_online);
  } else {
    enum class State : uint32_t {
      NotStarted,
      EnteringGuard,
      GuardEntered,
    };
    static_assert(ktl::atomic<State>::is_always_lock_free);

    using InstrumentedLockType = decltype(seq);
    struct TestParams {
      InstrumentedLockType& seq;
      ktl::atomic<State> state{State::NotStarted};
    } params{seq};

    // Pin ourselves to our current CPU during the test.
    CurrentCpuPinner cpu_pinner;

    // Create and resume the thread, making certain that it must run on a CPU other than ours.
    Thread* test_thread;
    if constexpr (SharedTest) {
      test_thread = Thread::Create(
          "SeqLock ContestedRead Test",
          +[](void* arg) -> int {
            TestParams& params = *reinterpret_cast<TestParams*>(arg);

            params.state.store(State::EnteringGuard);
            bool transaction_success;
            {
              lockdep::Guard<SeqLock, LockPolicy> guard{&params.seq, transaction_success};
              params.state.store(State::GuardEntered);
            }

            return transaction_success ? 1 : 0;
          },
          &params, DEFAULT_PRIORITY);
    } else {
      test_thread = Thread::Create(
          "SeqLock ContestedWrite Test",
          +[](void* arg) -> int {
            TestParams& params = *reinterpret_cast<TestParams*>(arg);

            params.state.store(State::EnteringGuard);

            // The NoIrqSave version of this guard is going to demand that
            // interrupts have already been disabled with a DEBUG_ASSERT.  If
            // that is the version we are using, make sure to manually disable
            // and re-enable interrupts.
            [[maybe_unused]] interrupt_saved_state_t interrupt_state;
            if constexpr (ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>) {
              interrupt_state = arch_interrupt_save();
            }

            {
              lockdep::Guard<SeqLock, LockPolicy> guard{&params.seq};
              params.state.store(State::GuardEntered);
            }

            if constexpr (ktl::is_same_v<LockPolicy, ExclusiveNoIrqSave>) {
              arch_interrupt_restore(interrupt_state);
            }

            return 1;
          },
          &params, DEFAULT_PRIORITY);
    }
    ASSERT_NONNULL(test_thread);
    test_thread->SetCpuAffinity(cpu_pinner.other_cpus_mask());

    // Hold the lock exclusively
    lockdep::Guard<SeqLock, ExclusiveIrqSave> guard{&params.seq};
    test_thread->Resume();

    // Wait for the thread to start to enter the guard.
    while (params.state.load() != State::EnteringGuard) {
      arch::Yield();
    }

    // Wait for a bit longer, then verify that the thread is still attempting to
    // enter the guard.
    zx_time_t deadline = current_time() + ZX_MSEC(500);
    while (deadline > current_time()) {
      arch::Yield();
    }
    EXPECT_EQ(State::EnteringGuard, params.state.load());

    // Release the lock and wait for the thread to indicate that it has entered
    // the guard.
    guard.Release();
    while (params.state.load() != State::GuardEntered) {
      arch::Yield();
    }

    // Join the thread, and make sure that the read transaction was successful
    // (if this was a shared guard test)
    int retcode;
    cpu_pinner.ReleasePin();
    test_thread->Join(&retcode, ZX_TIME_INFINITE);
    ASSERT_EQ(1, retcode);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(seqlock_tests)
UNITTEST("UncontestedRead<IrqSave>", UncontestedRead<SharedIrqSave>)
UNITTEST("UncontestedRead<NoIrqSave>", UncontestedRead<SharedNoIrqSave>)
UNITTEST("UncontestedWrite<IrqSave>", UncontestedWrite<ExclusiveIrqSave>)
UNITTEST("UncontestedWrite<NoIrqSave>", UncontestedWrite<ExclusiveNoIrqSave>)
UNITTEST("ContestedRead<IrqSave>", ContestedTest<SharedIrqSave>)
UNITTEST("ContestedRead<NoIrqSave>", ContestedTest<SharedNoIrqSave>)
UNITTEST("ContestedWrite<IrqSave>", ContestedTest<ExclusiveIrqSave>)
UNITTEST("ContestedWrite<NoIrqSave>", ContestedTest<ExclusiveNoIrqSave>)
UNITTEST_END_TESTCASE(seqlock_tests, "seqlock", "SeqLock Guard Tests")
