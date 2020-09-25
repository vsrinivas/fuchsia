// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <iterator>
#include <limits>

#include <fbl/auto_call.h>
#include <fbl/futex.h>
#include <zxtest/zxtest.h>

#include "bad-handle.h"
#include "utils.h"

namespace {

// A constant which is guaranteed to be an invalid handle, but not equal to the
// special value ZX_HANDLE_INVALID.  We use the INVALID sentinel to mean other
// things is certain contexts (like passing nullptr to a function), and for some
// of these tests, we just want a handle which is guaranteed to be simply bad.
//
// The FIXED_BITS_MASK specifies a pair of bits which are guaranteed to be 1 in
// any valid user-mode representation of a handle.  We can generate a
// guaranteed-to-be-bad handle by simply inverting this mask.
constexpr zx_handle_t ZX_HANDLE_BAD_BUT_NOT_INVALID =
    static_cast<zx_handle_t>(~ZX_HANDLE_FIXED_BITS_MASK);
static_assert(ZX_HANDLE_BAD_BUT_NOT_INVALID != ZX_HANDLE_INVALID,
              "ZX_HANDLE_BAD_BUT_NOT_INVALID must not match ZX_HANDLE_INVALID");

// Templated operation adapters which allow us to test the wake operation using
// the same code for zx_wake and zx_requeue
enum class OpType {
  kStandard,
  kRequeue,
};

template <OpType OPERATION>
struct WakeOperation;

template <>
struct WakeOperation<OpType::kStandard> {
  static zx_status_t wake(const fbl::futex_t& wake_futex, uint32_t count) {
    return zx_futex_wake(&wake_futex, count);
  }

  static zx_status_t wake_single_owner(const fbl::futex_t& wake_futex) {
    return zx_futex_wake_single_owner(&wake_futex);
  }
};

template <>
struct WakeOperation<OpType::kRequeue> {
  static zx_status_t wake(const fbl::futex_t& wake_futex, uint32_t count) {
    const fbl::futex_t& requeue_futex(0);
    return zx_futex_requeue(&wake_futex, count, 0, &requeue_futex, 0u, ZX_HANDLE_INVALID);
  }

  static zx_status_t wake_single_owner(const fbl::futex_t& wake_futex) {
    const fbl::futex_t& requeue_futex(0);
    return zx_futex_requeue_single_owner(&wake_futex, 0, &requeue_futex, 0u, ZX_HANDLE_INVALID);
  }
};

}  // namespace

TEST(FutexOwnershipTestCase, GetOwner) {
  fbl::futex_t the_futex(0);

  // No one should own our brand new futex right now.
  zx_status_t res;
  zx_koid_t koid = ~ZX_KOID_INVALID;
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, ZX_KOID_INVALID);

  // Passing a bad pointer for koid is an error.
  res = zx_futex_get_owner(&the_futex, nullptr);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);

  // Passing a misaligned pointer for the futex is an error.
  res = zx_futex_get_owner(
      reinterpret_cast<zx_futex_t*>(reinterpret_cast<uintptr_t>(&the_futex) + 1), &koid);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);

  // Passing a null pointer for the futex is an error.
  res = zx_futex_get_owner(nullptr, &koid);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
}

TEST(FutexOwnershipTestCase, Wait) {
  fbl::futex_t the_futex(0);
  ExternalThread external;
  Thread thread1, thread2, thread3;
  zx_status_t res;
  std::atomic<zx_status_t> t1_res, t2_res;

  zx_handle_t test_thread_handle = zx_thread_self();
  zx_koid_t test_thread_koid = CurrentThreadKoid();
  zx_koid_t koid;

  // If things go wrong, and we bail out early, do out best to shut down all
  // of the threads we may have started before unwinding our stack state out
  // from under them.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    external.Stop();
    thread1.Stop();
    thread2.Stop();
    thread3.Stop();
  });

  // Attempt to fetch the owner of the futex.  It should be no-one right now.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, ZX_KOID_INVALID);

  // Start a thread and have it declare us to be the owner of the futex.
  koid = ~ZX_KOID_INVALID;
  t1_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread1.Start("thread_1", [&]() -> int {
    t1_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_TRUE(WaitFor(kLongTimeout, [&]() -> bool {
    res = zx_futex_get_owner(&the_futex, &koid);
    // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
    return ((res != ZX_OK) || (koid == test_thread_koid));
  }));
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t1_res.load(), ZX_ERR_INTERNAL);  // thread1 is still waiting.

  // Start another thread and have it fail to set the futex owner to no one because of
  // an expected futex value mismatch.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.0", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 1, ZX_HANDLE_INVALID, ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_BAD_STATE);

  // Start a thread and attempt to set the futex owner to the thread doing the
  // wait (thread2).  This should fail.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.1", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, thread2.handle().get(), ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

  // Start a thread and attempt to set the futex owner to the thread which is
  // already waiting (thread1).  This should fail.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.2", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, thread1.handle().get(), ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

  // Start a thread and attempt to set the futex owner to a handle which is valid, but is not
  // actually a thread.
  zx::event not_a_thread;
  res = zx::event::create(0, &not_a_thread);
  ASSERT_OK(res);

  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.3", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, not_a_thread.get(), ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_WRONG_TYPE);

  // Start a thread and attempt to set the futex owner to the handle to a thread in another
  // process.
  ASSERT_NO_FATAL_FAILURES(external.Start());
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.4", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, external.thread().get(), ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());
  external.Stop();

  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_INVALID_ARGS);

  // Start thread3, just so we have a different owner to assign.  Then start
  // up thread2 and have it declare thread3 to be the new owner of the futex,
  // and finally timeout.  Verify that the ownership changes properly, and
  // that it does not change when thread2 times out.
  ASSERT_NO_FATAL_FAILURES(thread3.Start("thread_3", [&]() -> int { return 0; }));

  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.5", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, thread3.handle().get(),
                               zx::deadline_after(zx::msec(50)).get()));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());
  ASSERT_EQ(t2_res.load(), ZX_ERR_TIMED_OUT);

  ASSERT_TRUE(WaitFor(kLongTimeout, [&]() -> bool {
    res = zx_futex_get_owner(&the_futex, &koid);
    // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
    return ((res != ZX_OK) || (koid == thread3.koid()));
  }));
  ASSERT_OK(res);
  ASSERT_EQ(koid, thread3.koid());

  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, thread3.koid());

  // Start thread2 again and have it reset ownership back to the main test
  // thread.  This time, do so with a timeout which has already expired.
  // Ownership should be changed even if we wait with a timeout which has
  // already expired.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.6", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, 0));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());
  ASSERT_EQ(t2_res.load(), ZX_ERR_TIMED_OUT);

  ASSERT_TRUE(WaitFor(kLongTimeout, [&]() -> bool {
    res = zx_futex_get_owner(&the_futex, &koid);
    // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
    return ((res != ZX_OK) || (koid == test_thread_koid));
  }));

  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);

  // Start a thread and have it attempt to set the futex owner to a value
  // which is just a bad handle (but not ZX_HANDLE_INVALID).  Attempting to
  // wait like this should result in an error of ZX_ERR_BAD_HANDLE
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.7", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, ZX_HANDLE_BAD_BUT_NOT_INVALID, ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed, the error should be bad handle.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_BAD_HANDLE);

  // Do the same test, but this time, pass a bad state value.  The state needs
  // to be checked and return BAD_STATE before the proposed owner handle is
  // validated.  Failure to do this in the proper order can lead to a race
  // which can cause a job policy exception to fire in mutex code which
  // implements priority inheritance; see fxbug.dev/34382.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.8", [&]() -> int {
    zx_handle_t bad_handle = test_thread_handle & ~ZX_HANDLE_FIXED_BITS_MASK;
    t2_res.store(zx_futex_wait(&the_futex, 1, bad_handle, ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_OK(thread2.Stop());

  // The futex owner should not have changed, the error should be bad state.
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t2_res.load(), ZX_ERR_BAD_STATE);

  // Finally, start second thread and have it succeed in waiting, setting
  // the owner of the futex to nothing in the process.
  t2_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread2.Start("thread_2.9", [&]() -> int {
    t2_res.store(zx_futex_wait(&the_futex, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE));
    return 0;
  }));
  ASSERT_TRUE(WaitFor(kLongTimeout, [&]() -> bool {
    res = zx_futex_get_owner(&the_futex, &koid);
    // Stop waiting if we fail to fetch the owner, or if the koid matches what we expect.
    return ((res != ZX_OK) || (koid == ZX_KOID_INVALID));
  }));
  ASSERT_OK(res);
  ASSERT_EQ(koid, ZX_KOID_INVALID);

  // Wakeup all of the threads and join
  res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);
  ASSERT_OK(thread1.Stop());
  ASSERT_OK(thread2.Stop());
  ASSERT_OK(thread3.Stop());
  ASSERT_OK(t1_res.load());
  ASSERT_OK(t2_res.load());

  cleanup.cancel();
}

template <OpType OPERATION>
static void WakeOwnershipTest() {
  using do_op = WakeOperation<OPERATION>;

  fbl::futex_t the_futex(0);
  zx_handle_t test_thread_handle = zx_thread_self();
  zx_koid_t test_thread_koid = CurrentThreadKoid();
  zx_koid_t koid;
  zx_status_t res;

  struct WaiterState {
    Thread thread;
    std::atomic<zx_status_t> res;
    bool woken;
  } WAITERS[8];

  // If things go wrong, and we bail out early, do out best to shut down all
  // of the threads we may have started before unwinding our stack state out
  // from under them.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    for (auto& waiter : WAITERS) {
      waiter.thread.Stop();
    }
  });

  // Run this test 2 times.  The first time, use a traditional wake which
  // should always set the futex to "unowned".  The second time, use the
  // wake_single_owner variant which should assign ownership to the thread
  // which was woken.
  for (uint32_t pass = 0; pass < 2; ++pass) {
    // Start a bunch of threads and have them all declare us to be
    // the_futex's owner.
    for (auto& waiter : WAITERS) {
      waiter.res.store(ZX_ERR_INTERNAL);
      waiter.woken = false;
      ASSERT_NO_FATAL_FAILURES(waiter.thread.Start(
          "wake_test_waiter", [&waiter, &the_futex, test_thread_handle]() -> int {
            waiter.res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
            return 0;
          }));
    }

    // Wait until all of the threads are blocked.
    res = ZX_ERR_INTERNAL;
    ASSERT_TRUE(WaitFor(kLongTimeout, [&WAITERS, &res]() -> bool {
      for (const auto& waiter : WAITERS) {
        // If we fail to fetch thread state, stop waiting.
        uint32_t state;
        res = waiter.thread.GetRunState(&state);
        if (res != ZX_OK) {
          return true;
        }

        // If this thread is not blocked yet, keep waiting.
        if (state != ZX_THREAD_STATE_BLOCKED_FUTEX) {
          return false;
        }

        // If this thread is blocked, but is not in the RUNNING state,
        // then it is blocked on the wrong futex (in this case, the
        // Thread's stop_event's futex).  Stop waiting and report the
        // error.
        if (waiter.thread.state() != Thread::State::RUNNING) {
          res = ZX_ERR_BAD_STATE;
          return true;
        }
      }

      // All threads are blocked, we are finished.
      return true;
    }));
    ASSERT_OK(res);

    // We should currently be the owner of the futex.
    res = zx_futex_get_owner(&the_futex, &koid);
    ASSERT_OK(res);
    ASSERT_EQ(koid, test_thread_koid);

    // If we are testing the wake behavior of zx_futex_requeue_*, then make
    // sure that attempting to do a wake op when the wake-futex value verification
    // fails does nothing to change the ownership of the futex.
    if constexpr (OPERATION == OpType::kRequeue) {
      fbl::futex_t requeue_futex(1);
      if (pass == 0) {
        res = zx_futex_requeue(&the_futex, 1u, 1, &requeue_futex, 0u, ZX_HANDLE_INVALID);
      } else {
        res = zx_futex_requeue_single_owner(&the_futex, 1, &requeue_futex, 0u, ZX_HANDLE_INVALID);
      }
      ASSERT_EQ(res, ZX_ERR_BAD_STATE);

      // We should still be the owner of the futex.
      res = zx_futex_get_owner(&the_futex, &koid);
      ASSERT_OK(res);
      ASSERT_EQ(koid, test_thread_koid);

      // All waiters should still be blocked on our futex.
      for (const auto& waiter : WAITERS) {
        uint32_t state;
        res = waiter.thread.GetRunState(&state);
        ASSERT_OK(res);
        ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);
      }
    }

    // Now wake all of the threads.  We don't know or have any guarantee as
    // to which thread the kernel is going to choose to wake, so we cannot
    // make any assumptions here, just that some thread will be woken.
    //
    // ++ Pass 0 validation uses the traditonal wake and should result in no
    //    owner.
    // ++ Pass 1 validation uses wake_single_owner and should assign
    //    ownership to the thread which was woken, until the last thread is
    //    woken (at which point, there should be no owner as there are no
    //    waiters).
    //
    for (uint32_t i = 0; i < std::size(WAITERS); ++i) {
      if (!pass) {
        // Wake a thread.
        res = do_op::wake(the_futex, 1u);
      } else {
        res = do_op::wake_single_owner(the_futex);
      }
      ASSERT_OK(res);

      // Wait until at least one thread has finished its lambda, which we
      // have not noticed before.
      WaiterState* woken_waiter = nullptr;
      res = ZX_ERR_INTERNAL;

      ASSERT_TRUE(WaitFor(kLongTimeout, [&WAITERS, &woken_waiter]() -> bool {
        for (auto& waiter : WAITERS) {
          if (!waiter.woken) {
            if (waiter.thread.state() == Thread::State::WAITING_TO_STOP) {
              waiter.woken = true;
              woken_waiter = &waiter;
              return true;
            }
          }
        }

        return false;
      }));

      ASSERT_NOT_NULL(woken_waiter);
      ASSERT_OK(woken_waiter->res.load());

      // Now check to be sure that ownership was updated properly.  It
      // should be INVALID if this is pass 0, or if we just woke up the
      // last thread.
      zx_koid_t expected_koid = (!pass || ((i + 1) == std::size(WAITERS)))
                                    ? ZX_KOID_INVALID
                                    : woken_waiter->thread.koid();

      res = zx_futex_get_owner(&the_futex, &koid);

      ASSERT_OK(res);
      ASSERT_EQ(koid, expected_koid);

      // Recycle our thread for the next pass.
      ASSERT_OK(woken_waiter->thread.Stop());
    }
  }

  cleanup.cancel();
}

TEST(FutexOwnershipTestCase, Wake) {
  ASSERT_NO_FATAL_FAILURES(WakeOwnershipTest<OpType::kStandard>());
}

TEST(FutexOwnershipTestCase, RequeueWake) {
  ASSERT_NO_FATAL_FAILURES(WakeOwnershipTest<OpType::kRequeue>());
}

template <OpType OPERATION>
static void WakeZeroOwnershipTest() {
  using do_op = WakeOperation<OPERATION>;

  fbl::futex_t the_futex(0);
  zx_status_t res;
  Thread thread1;
  std::atomic<zx_status_t> t1_res;

  zx_handle_t test_thread_handle = zx_thread_self();
  zx_koid_t test_thread_koid = CurrentThreadKoid();
  zx_koid_t koid;
  uint32_t state;

  // If things go wrong, and we bail out early, do out best to shut down all
  // of the threads we may have started before unwinding our stack state out
  // from under them.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    thread1.Stop();
  });

  // Start a thread and have it declare us to be the owner of the futex.
  koid = ~ZX_KOID_INVALID;
  t1_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(thread1.Start("thread_1", [&]() -> int {
    t1_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
    return 0;
  }));

  // Wait until the thread has become blocked on the futex
  ASSERT_TRUE(WaitFor(kLongTimeout, [&]() -> bool {
    res = thread1.GetRunState(&state);
    // Stop waiting if we fail to fetch the run state, or the thread has
    // reached our desired state.
    return ((res != ZX_OK) || (state == ZX_THREAD_STATE_BLOCKED_FUTEX));
  }));
  ASSERT_OK(res);
  ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);

  // We should now be the owner of the futex
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, test_thread_koid);
  ASSERT_EQ(t1_res.load(), ZX_ERR_INTERNAL);  // thread1 is still waiting.

  // Attempt to wake zero threads.  This should succeed, thread1 should still
  // blocked on the futex, and the owner of the futex should now be no one.
  res = do_op::wake(the_futex, 0);
  ASSERT_OK(res);

  // Wait up to 100mSec for the thread to unblock.  If it is still blocked on
  // the futex after 100mSec, then assume that it is going to remain blocked.
  //
  // TODO(johngro): Look into changing the need for this.  The issue here is
  // that the run state of user mode threads is tracked using a helper class
  // in ThreadDispatcher called "AutoBlocked".  When a thread blocks on a
  // futex (for example), it puts an AutoBlocked(BY_FUTEX) on its local stack,
  // joins a wait queue, and is suspended.  When it resumes and the AutoBlock
  // destructor runs, it restores the thread's previous run state.
  //
  // Because of this, when Thread A wakes Thread B from a futex wait queue,
  // the user-mode run state state of thread B is not updated atomically as
  // the thread is removed from the wait queue by thread A.  If it takes a bit
  // of time for thread B to be scheduled again (and run the AutoBlocked
  // destructor), then it will appear to be blocked by a futex still, even
  // though the thread is actually run-able.  Failure to wait for a little bit
  // here can lead to a flaky test (esp. under qemu).
  //
  // Still, as long as this state is not atomically updated by the wake
  // operation, the test is always has the potential to flaky, which is why
  // the TODO.
  ASSERT_FALSE(WaitFor(zx::msec(100), [&]() -> bool {
    res = thread1.GetRunState(&state);
    return ((res != ZX_OK) || (state != ZX_THREAD_STATE_BLOCKED_FUTEX));
  }));
  ASSERT_OK(res);
  ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);

  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, ZX_KOID_INVALID);

  // Finished.  Wake up the thread and shut down.
  res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);
  ASSERT_OK(thread1.Stop());
  ASSERT_OK(t1_res.load());

  cleanup.cancel();
}

TEST(FutexOwnershipTestCase, WakeZero) {
  ASSERT_NO_FATAL_FAILURES(WakeZeroOwnershipTest<OpType::kStandard>());
}

TEST(FutexOwnershipTestCase, RequeueWakeZero) {
  ASSERT_NO_FATAL_FAILURES(WakeZeroOwnershipTest<OpType::kRequeue>());
}

TEST(FutexOwnershipTestCase, Requeue) {
  fbl::futex_t wake_futex(0);
  fbl::futex_t requeue_futex(1);
  ExternalThread external;
  zx::event not_a_thread;
  zx_handle_t test_thread_handle = zx_thread_self();
  zx_koid_t test_thread_koid = CurrentThreadKoid();
  zx_koid_t koid;
  zx_status_t res;

  struct WaiterState {
    Thread thread;
    std::atomic<zx_status_t> res;
    bool woken;
  } WAITERS[8];

  // If things go wrong, and we bail out early, do out best to shut down all
  // of the threads we may have started before unwinding our stack state out
  // from under them.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&wake_futex, std::numeric_limits<uint32_t>::max());
    zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
    external.Stop();
    for (auto& waiter : WAITERS) {
      waiter.thread.Stop();
    }
  });

  // Start a bunch of threads and have them all declare us to be
  // the_futex's owner.
  for (auto& waiter : WAITERS) {
    waiter.res.store(ZX_ERR_INTERNAL);
    waiter.woken = false;
    ASSERT_NO_FATAL_FAILURES(waiter.thread.Start(
        "requeue_test_waiter", [&waiter, &wake_futex, test_thread_handle]() -> int {
          waiter.res.store(zx_futex_wait(&wake_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
          return 0;
        }));
  }

  // Wait until all of the threads are blocked.
  res = ZX_ERR_INTERNAL;
  ASSERT_TRUE(WaitFor(kLongTimeout, [&WAITERS, &res]() -> bool {
    for (const auto& waiter : WAITERS) {
      // If we fail to fetch thread state, stop waiting.
      uint32_t state;
      res = waiter.thread.GetRunState(&state);
      if (res != ZX_OK) {
        return true;
      }

      // If this thread is not blocked yet, keep waiting.
      if (state != ZX_THREAD_STATE_BLOCKED_FUTEX) {
        return false;
      }
    }

    // All threads are blocked, we are finished.
    return true;
  }));
  ASSERT_OK(res);

  // Create a valid handle which is not a thread.  We will need it to make
  // sure that it is illegal to set the requeue target to something which is a
  // valid handle, but not a thread.
  res = zx::event::create(0, &not_a_thread);
  ASSERT_OK(res);

  // Start a thread in another process.  We will need one to make sure that we
  // are not allowed to change the owner of the requeue futex to a thread from
  // a another process.
  ASSERT_NO_FATAL_FAILURES(external.Start());

  // A small helper lambda we use to reduce the boilerplate state checks we
  // are about to do a number of times.
  auto VerifyState = [&](zx_koid_t expected_wake_owner, zx_koid_t expected_requeue_owner) -> void {
    zx_koid_t koid;
    zx_status_t res;

    // Check the owners.
    res = zx_futex_get_owner(&wake_futex, &koid);
    ASSERT_OK(res);
    ASSERT_EQ(koid, expected_wake_owner);

    res = zx_futex_get_owner(&requeue_futex, &koid);
    ASSERT_OK(res);
    ASSERT_EQ(koid, expected_requeue_owner);

    // Check each of the waiters.
    for (const auto& waiter : WAITERS) {
      uint32_t state;
      res = waiter.thread.GetRunState(&state);
      ASSERT_OK(res);

      if (!waiter.woken) {
        ASSERT_EQ(state, ZX_THREAD_STATE_BLOCKED_FUTEX);
      }
    }
  };

  // OK, basic setup is complete.  We should be the owner of the wait futex, no one
  // should own the requeue futex, and all threads should be blocked waiting
  // on the wait futex (although, at this point in the test, we can only check
  // to be sure that the are all blocked by a futex... we don't know which
  // one).
  ASSERT_NO_FATAL_FAILURES(VerifyState(test_thread_koid, ZX_KOID_INVALID));

  // Wake a single thread assigning ownership of the wake thread to it in the
  // process, and requeue a single thread from the wake futex to the requeue
  // futex (we have no good way to know which one gets requeued, just that it
  // has been).  Assign ownership of the requeue futex to ourselves in the
  // process.
  res = zx_futex_requeue_single_owner(&wake_futex, 0, &requeue_futex, 1, test_thread_handle);
  ASSERT_OK(res);

  // Find the thread we just woke up.
  const WaiterState* woken_waiter = nullptr;
  res = zx_futex_get_owner(&wake_futex, &koid);
  ASSERT_OK(res);
  ASSERT_NE(koid, ZX_KOID_INVALID);
  ASSERT_NE(koid, test_thread_koid);
  for (auto& waiter : WAITERS) {
    if (!waiter.woken && (waiter.thread.koid() == koid)) {
      waiter.woken = true;
      woken_waiter = &waiter;
    }
  }
  ASSERT_NOT_NULL(woken_waiter);

  // Wait until it has finished its lambda and waiting for our permission to stop.
  ASSERT_TRUE(WaitFor(kLongTimeout, [woken_waiter]() -> bool {
    return (woken_waiter->thread.state() == Thread::State::WAITING_TO_STOP);
  }));

  zx_koid_t woken_thread_koid = woken_waiter->thread.koid();
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Next, start a sequence of failure tests.  In each of the tests, attempt
  // to wake no threads, but requeue a single thread declaring the owner of
  // the requeue futex to be no one.
  //
  // After each of these tests, nothing should have changed.  We should own
  // the requeue futex, the thread which was woken during setup should own the
  // wake futex, and all of our threads (except the woken thread) should be
  // blocked on a futex (we just don't know which one).
  //
  // Failure Test #1:
  // It is illegal to specify either nullptr or a misaligned futex for the
  // wake futex.
  //
  res = zx_futex_requeue(nullptr, 1u, 0, &requeue_futex, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  const zx_futex_t* misaligned_wake_futex =
      reinterpret_cast<const zx_futex_t*>(reinterpret_cast<uintptr_t>(&wake_futex) + 1);
  res = zx_futex_requeue(misaligned_wake_futex, 1u, 0, &requeue_futex, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #2:
  // It is illegal to specify either nullptr or a misaligned futex for the
  // requeue futex.
  //
  res = zx_futex_requeue(&wake_futex, 1u, 0, nullptr, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  const zx_futex_t* misaligned_requeue_futex =
      reinterpret_cast<const zx_futex_t*>(reinterpret_cast<uintptr_t>(&requeue_futex) + 1);
  res = zx_futex_requeue(&wake_futex, 1u, 0, misaligned_requeue_futex, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #3:
  // It is illegal to use the same futex for both wake and requeue.
  //
  res = zx_futex_requeue(&wake_futex, 1u, 0, &wake_futex, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #4:
  // It is illegal to use an invalid handle value as the new requeue owner
  // which is not ZX_HANDLE_INVALID
  //
  res = zx_futex_requeue(&wake_futex, 1u, 0, &requeue_futex, 1, ZX_HANDLE_BAD_BUT_NOT_INVALID);
  ASSERT_EQ(res, ZX_ERR_BAD_HANDLE);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #5:
  // It is illegal to use an valid handle value which is not a thread.
  //
  res = zx_futex_requeue(&wake_futex, 1u, 0, &requeue_futex, 1, not_a_thread.get());
  ASSERT_EQ(res, ZX_ERR_WRONG_TYPE);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #6:
  // It is illegal to use an valid thread handle handle from another process.
  //
  res = zx_futex_requeue(&wake_futex, 1u, 0, &requeue_futex, 1, external.thread().get());
  ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // We don't need our external process anymore.
  external.Stop();

  // Failure Test #7:
  // It is illegal to a thread currently in waiting in either the wait queue
  // or the requeue queue.  We don't really know which thread is which at this
  // point in time, but trying them all should cover both cases.
  //
  for (const auto& waiter : WAITERS) {
    if (waiter.woken) {
      continue;
    }
    res = zx_futex_requeue(&wake_futex, 1u, 0, &requeue_futex, 1, waiter.thread.handle().get());
    ASSERT_EQ(res, ZX_ERR_INVALID_ARGS);
    ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));
  }

  // Failure Test #8:
  // Nothing should change if we fail to validate the wake futex state.
  //
  res = zx_futex_requeue(&wake_futex, 1u, 1, &requeue_futex, 1, ZX_HANDLE_INVALID);
  ASSERT_EQ(res, ZX_ERR_BAD_STATE);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Failure Test #9:
  // If we pass a bad/invalid handle as the new requeue owner, _and_ we pass a
  // value which does not equal the current futex state by the time we make it
  // into the futex context lock in the kernel, then the operation should fail
  // and error code we get back should be BAD_STATE, not BAD_HANDLE.  The
  // state needs to be validated _before_ we concern ourselves with the
  // validating the potential new owner.
  res = zx_futex_requeue(&wake_futex, 1u, 1, &requeue_futex, 1, ZX_HANDLE_BAD_BUT_NOT_INVALID);
  ASSERT_EQ(res, ZX_ERR_BAD_STATE);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, test_thread_koid));

  // Small helper lambdas we will use during the success tests to count the
  // number of threads which wake up in response to our actions.
  auto CountJustWoken = [&WAITERS]() -> uint32_t {
    uint32_t just_woken = 0;
    for (auto& waiter : WAITERS) {
      if (!waiter.woken && (waiter.thread.state() == Thread::State::WAITING_TO_STOP)) {
        ++just_woken;
        waiter.woken = true;
      }
    }
    return just_woken;
  };

  auto WaitForJustWoken = [&CountJustWoken](uint32_t expected) -> uint32_t {
    uint32_t just_woken = CountJustWoken();

    WaitFor(kLongTimeout, [&]() -> bool {
      just_woken += CountJustWoken();
      return just_woken >= expected;
    });

    // Wait just a bit longer to see if anyone else wakes up who shouldn't.
    //
    // Note: See TODO above about possibly eliminating the need to perform this
    // arbitrary wait.
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
    just_woken += CountJustWoken();
    return just_woken;
  };

  // Time for success tests.
  //
  // During setup, we woke exactly one thread from the wake futex, and
  // requeued exactly one thread from the wake to the requeue futex.  So we
  // should have 1 thread ready to stop, 1 thread blocked on the requeue
  // futex, and the rest of the threads blocked on the wake futex.
  //
  // Verify that exactly one thread was waiting in the requeue futex by waking
  // everyone on the requeue_futex and waiting a little bit to see who becomes
  // blocked on the exit event.
  res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);

  uint32_t just_woken;
  just_woken = WaitForJustWoken(1u);
  ASSERT_EQ(just_woken, 1u);
  ASSERT_NO_FATAL_FAILURES(VerifyState(woken_thread_koid, ZX_KOID_INVALID));

  // Now requeue exactly two threads, setting the owner to the thread that we
  // originally woke up in the process.
  res =
      zx_futex_requeue(&wake_futex, 0u, 0, &requeue_futex, 2, woken_waiter->thread.handle().get());
  ASSERT_OK(res);
  ASSERT_NO_FATAL_FAILURES(VerifyState(ZX_KOID_INVALID, woken_thread_koid));

  res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);

  just_woken = WaitForJustWoken(2u);
  ASSERT_EQ(2u, just_woken);
  ASSERT_NO_FATAL_FAILURES(VerifyState(ZX_KOID_INVALID, ZX_KOID_INVALID));

  // Finally, requeue the rest of the threads, setting ownership of the
  // requeue futex back to ourselves in the process.
  res = zx_futex_requeue(&wake_futex, 0u, 0, &requeue_futex, std::numeric_limits<uint32_t>::max(),
                         test_thread_handle);
  ASSERT_NO_FATAL_FAILURES(VerifyState(ZX_KOID_INVALID, test_thread_koid));

  // Verify that all threads were requeued by waking up everyone on the
  // requeue futex, and stopping threads.
  res = zx_futex_wake(&requeue_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);
  for (auto& waiter : WAITERS) {
    ASSERT_OK(waiter.thread.Stop());
    waiter.woken = true;
    ASSERT_OK(waiter.res.load());
  }

  // Success!
  cleanup.cancel();
}

TEST(FutexOwnershipTestCase, OwnerExit) {
  fbl::futex_t the_futex(0);
  Thread the_owner;
  Thread the_waiter;
  std::atomic<zx_status_t> waiter_res;
  zx_status_t res;

  // If things go wrong, and we bail out early, do out best to shut down all
  // of the threads.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
    the_owner.Stop();
    the_waiter.Stop();
  });

  // Start the "owner" thread.  Have it do nothing at all.  It will end up
  // blocking on an internal signal, waiting for us to tell it to stop.
  ASSERT_NO_FATAL_FAILURES(the_owner.Start("OwnerExitTest owner", []() -> int { return 0; }));

  // Start the "waiter" thread.  Have it wait on the futex, and declare the
  // owner thread to be the owner of the_futex.
  waiter_res.store(ZX_ERR_INTERNAL);
  ASSERT_NO_FATAL_FAILURES(the_waiter.Start(
      "OwnerExitTest waiter",
      [&waiter_res, &the_futex, test_thread_handle = the_owner.handle().get()]() -> int {
        waiter_res.store(zx_futex_wait(&the_futex, 0, test_thread_handle, ZX_TIME_INFINITE));
        return 0;
      }));

  // Wait until our waiter has become blocked by the futex.
  ASSERT_TRUE(WaitFor(kLongTimeout, [&the_waiter, &res]() -> bool {
    // If we fail to fetch thread state, stop waiting.
    uint32_t state;
    res = the_waiter.GetRunState(&state);
    if (res != ZX_OK) {
      return true;
    }

    // We are done if the thread has reached the BLOCKED_FUTEX state
    return (state == ZX_THREAD_STATE_BLOCKED_FUTEX);
  }));
  ASSERT_OK(res);

  // Verify that our futex is owned by our owner thread.
  zx_koid_t koid = ~ZX_KOID_INVALID;
  res = zx_futex_get_owner(&the_futex, &koid);
  ASSERT_OK(res);
  ASSERT_EQ(koid, the_owner.koid());

  // OK, now let the owner thread exit and wait for ownership of the futex to become
  // automatically released.
  //
  // Note: We cannot actually synchronize with this operation with a
  // simple thrd_join for a number of reasons.
  //
  // 1) A successful join on a thread in the zircon C runtime only
  //    establishes that the thread has entered into the kernel for the
  //    last time, never to return again.  The thread _will_ achieve
  //    eventually death at some point in the future, but there is no
  //    guarantee that it has done so yet.
  //
  // 2) Final ownership of the OwnedWaitQueue used by the futex is
  //    released when the kernel portion of the thread achieves kernel
  //    thread state of THREAD_DEATH.  This is a different state from the
  //    observable user-mode thread state, which becomes
  //    ZX_THREAD_STATE_DEAD at the very last instant before the thread
  //    enters the thread lock and transitions the kernel state to
  //    THREAD_DEATH (releasing ownership in the process).
  //
  // 3) The only real way to synchronize with achieving kernel
  //    THREAD_DEATH is during destruction of the kernel ThreadDispatcher
  //    object.  Unfortunately, simply closing the very last user-mode
  //    handle to the thread is no guarantee of this either as the kernel
  //    also holds references the ThreadDispatcher is certain situations.
  //
  // So, the only real choice here is to just wait.  We know that since we
  // have signalled the thread to exit, and we have successfully joined
  // the thread, that it is only a matter of time before it actually
  // exits.  If something goes wrong here, either our local (absurdly large)
  // timeout will fire, or the test framework watchdog will fire.
  ASSERT_OK(the_owner.Stop());

  res = ZX_ERR_INTERNAL;
  ASSERT_TRUE(WaitFor(kLongTimeout, [&the_futex, &res]() -> bool {
    zx_koid_t koid = ~ZX_KOID_INVALID;
    res = zx_futex_get_owner(&the_futex, &koid);

    // If we fail to fetch the ownership info, stop waiting.
    if (res != ZX_OK) {
      return true;
    }

    // We are done if the futex owner is now INVALID.
    return (koid == ZX_KOID_INVALID);
  }));
  ASSERT_OK(res);

  // Release our waiter thread and shut down.
  res = zx_futex_wake(&the_futex, std::numeric_limits<uint32_t>::max());
  ASSERT_OK(res);
  ASSERT_OK(the_waiter.Stop());
  ASSERT_OK(waiter_res.load());

  cleanup.cancel();
}

TEST(FutexOwnershipTestCase, OwnerStarted) {
  // It is illegal to assign ownership to a thread which exists, but has not
  // been started yet.  Attempts to do this using either requeue, or wait
  // operation should result in an INVALID_ARGS status code.
  zx_futex_t futex1{0};
  zx_futex_t futex2{0};

  // Create a thread, but don't start it.  Note that we have to go directly to
  // the zircon syscalls here; creating a thread but not starting it is not
  // allowed by the C11 thrd APIs.
  zx::thread not_started;
  char name[] = "not started thread";
  ASSERT_OK(zx::thread::create(*zx::process::self(), name, sizeof(name) - 1, 0, &not_started));
  ASSERT_TRUE(not_started.is_valid());

  // Attempt to wait on one of our futexes with a short timeout, declaring the
  // not_started thread to be the owner.  This should fail with ZX_ERR_INVALID_ARGS.
  zx_status_t res = zx_futex_wait(&futex1, 0, not_started.get(), zx_deadline_after(ZX_MSEC(1)));
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, res);

  // Try again, but this time use requeue instead of wait in our attempt to assign ownership.
  res = zx_futex_requeue(&futex1, 0, 0, &futex2, 1, not_started.get());
  EXPECT_STATUS(ZX_ERR_INVALID_ARGS, res);
}

TEST(FutexOwnershipTestCase, DeadThreadsCantOwnFutexes) {
  // As the test name implies, dead threads cannot own futexes.  If someone
  // attempts to assign futex ownership to a thread which has exited, the result
  // should be that the thread is owned by no one.  Testing this involves a
  // number of ingredients.  We need:
  //
  // 1) Two futexes.  Testing ownership assignment via requeue requires 2.
  // 2) A thread to wait in the owned futex for the duration of the test.  Futex state
  //    cannot exist without at least one waiter.
  // 3) A thread which has run to completion, but whose final handle has not
  //    been closed.  This thread will serve as the dead thread that we attempt
  //    to assign ownership to.
  // 4) A living thread which can serve as the owner which gets erase during our
  //    attempt to assign ownership to a dead thread.
  fbl::futex_t futex1(0);
  fbl::futex_t futex2(0);
  Thread the_waiter;
  Thread live_owner;
  zx::thread dead_owner;
  zx_status_t res;

  // Success or fail, make sure we clean everything up in the proper order at
  // the end of the test.
  auto cleanup = fbl::MakeAutoCall([&]() {
    zx_futex_wake(&futex1, std::numeric_limits<uint32_t>::max());
    zx_futex_wake(&futex2, std::numeric_limits<uint32_t>::max());
    the_waiter.Stop();
    live_owner.Stop();
  });

  // Start the waiter and park it in futex1.
  ASSERT_NO_FATAL_FAILURES(the_waiter.Start("DeadThread waiter", [&futex1]() -> int {
    return zx_futex_wait(&futex1, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
  }));

  // Wait until our thread becomes blocked in futex1.  It is important to
  // confirm that this has happened before allowing the rest of the test to
  // proceed.  Futexes with no waiters have no state and can have no owners.  We
  // need to be certain that futex1 has at least one waiter in order for the
  // rest of the tests to function properly.
  ASSERT_TRUE(WaitFor(kLongTimeout, [&the_waiter, &res]() -> bool {
    uint32_t run_state;
    res = the_waiter.GetRunState(&run_state);
    // stop waiting if there was an error, or we have achieved the blocked state.
    return (res != ZX_OK) || (run_state == ZX_THREAD_STATE_BLOCKED_FUTEX);
  }));
  ASSERT_OK(res);

  // Create a thread, duplicate it's handle, and then stop the thread.  This
  // will serve as our "dead" owner.
  {
    Thread tmp;
    ASSERT_NO_FATAL_FAILURES(tmp.Start("DeadThread dead owner", []() -> int { return 0; }));
    res = tmp.handle().duplicate(ZX_RIGHT_SAME_RIGHTS, &dead_owner);
    ASSERT_OK(tmp.Stop());
    ASSERT_OK(res);
  }

  // Wait until we are certain that our thread has achieved the DEAD state from the kernel's
  // user-mode thread perspective.
  ASSERT_TRUE(WaitFor(kLongTimeout, [&dead_owner, &res]() -> bool {
    zx_info_thread_t info;
    res = dead_owner.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
    // stop waiting if there was an error, or we have achieved the dead state.
    return (res != ZX_OK) || (info.state == ZX_THREAD_STATE_DEAD);
  }));
  ASSERT_OK(res);

  // Start the live owner, but do not stop it.  Note that even though our thread
  // body does nothing, our utility thread helper class does not actually allow
  // the thread to exit until it has been explicitly Stopped.
  ASSERT_NO_FATAL_FAILURES(live_owner.Start("DeadThread live owner", []() -> int { return 0; }));

  // OK, at this point in time, futex1 should be owned by no one.  Verify this.
  zx_koid_t koid;
  koid = the_waiter.koid();
  ASSERT_OK(zx_futex_get_owner(&futex1, &koid));
  ASSERT_EQ(ZX_KOID_INVALID, koid);

  // Now assign ownership to live_owner using a requeue operation which is
  // actually neither going to wake or requeue any threads.
  ASSERT_OK(zx_futex_requeue(&futex2, 0, 0, &futex1, 1, live_owner.handle().get()));
  koid = the_waiter.koid();
  ASSERT_OK(zx_futex_get_owner(&futex1, &koid));
  ASSERT_EQ(live_owner.koid(), koid);

  // Attempt to assign ownership to the dead thread via a wait operation.  The
  // operation should not fail because the thread is dead, instead it should
  // simply time out.  That said, the futex should have no owner after the
  // assignment attempt.
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            zx_futex_wait(&futex1, 0, dead_owner.get(), zx_deadline_after(ZX_MSEC(1))));
  koid = the_waiter.koid();
  ASSERT_OK(zx_futex_get_owner(&futex1, &koid));
  ASSERT_EQ(ZX_KOID_INVALID, koid);

  // Switch ownership back to the living thread.
  ASSERT_OK(zx_futex_requeue(&futex2, 0, 0, &futex1, 1, live_owner.handle().get()));
  koid = the_waiter.koid();
  ASSERT_OK(zx_futex_get_owner(&futex1, &koid));
  ASSERT_EQ(live_owner.koid(), koid);

  // Attempt to assign ownership to the dead thread via a requeue operation.
  // This should succeed, but no one should own the futex at the end of the
  // operation.
  ASSERT_OK(zx_futex_requeue(&futex2, 0, 0, &futex1, 1, dead_owner.get()));
  koid = the_waiter.koid();
  ASSERT_OK(zx_futex_get_owner(&futex1, &koid));
  ASSERT_EQ(ZX_KOID_INVALID, koid);

  // Success, let our cleanup lambda to the cleanup work for us.
}

int main(int argc, char** argv) {
  ExternalThread::SetProgramName(argv[0]);

  if ((argc == 2) && !strcmp(argv[1], ExternalThread::helper_flag())) {
    return ExternalThread::DoHelperThread();
  }

  if ((argc >= 2) && !strcmp(argv[1], BadHandleFlagTest())) {
    return BadHandleTestMain(argc, argv);
  }

  return RUN_ALL_TESTS(argc, argv);
}
