// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_MUTEX_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_MUTEX_H_

#include <assert.h>
#include <debug.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/time.h>

#include <fbl/canary.h>
#include <fbl/macros.h>
#include <kernel/lockdep.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>

// Kernel mutex support.
//
class TA_CAP("mutex") Mutex {
 public:
  constexpr Mutex() = default;
  ~Mutex();

  // No moving or copying allowed.
  DISALLOW_COPY_ASSIGN_AND_MOVE(Mutex);

  // The maximum duration to spin before falling back to blocking.
  // TODO(fxbug.dev/34646): Decide how to make this configurable per device/platform
  // and describe how to optimize this value.
  static constexpr zx_duration_t SPIN_MAX_DURATION = ZX_USEC(150);

  // Acquire the mutex.
  void Acquire(zx_duration_t spin_max_duration = SPIN_MAX_DURATION) TA_ACQ() TA_EXCL(thread_lock);

  // Release the mutex. Must be held by the current thread.
  void Release() TA_REL() TA_EXCL(thread_lock);

  // Special version of Release which operates with the thread lock held
  void ReleaseThreadLocked(bool allow_reschedule) TA_REL() TA_REQ(thread_lock);

  // does the current thread hold the mutex?
  bool IsHeld() const { return (holder() == Thread::Current::Get()); }

  // Panic unless the given lock is held.
  //
  // Can be used when thread safety analysis can't prove you are holding
  // a lock. The asserts may be optimized away in release builds.
  void AssertHeld() const TA_ASSERT() { DEBUG_ASSERT(IsHeld()); }

 private:
  enum class ThreadLockState : bool { NotHeld = false, Held = true };

  // Templated implementation for |Release| and |ReleaseThreadLocked|.
  template <ThreadLockState TLS>
  void ReleaseInternal(bool allow_reschedule) TA_REL() __TA_NO_THREAD_SAFETY_ANALYSIS;

  // Acquire a lock held by another thread.
  //
  // This is a slowpath taken by |Acquire| if the mutex is found to already be held
  // by another thread.
  //
  // This function is deliberately moved out of line from |Acquire| to keep the stack
  // set up, tear down in the |Acquire| fastpath small.
  void AcquireContendedMutex(zx_duration_t spin_max_duration, Thread* current_thread) TA_ACQ()
      TA_EXCL(thread_lock);

  // Release a lock contended by another thread.
  //
  // This is a slowpath taken by |Release| (via |ReleaseInternal|) when releasing a
  // lock that is being waited for by another thread.
  //
  // This function is deliberately moved out of line from |Release| to keep the stack
  // set up, tear down in the |Release| fastpath small.
  template <Mutex::ThreadLockState TLS>
  void ReleaseContendedMutex(bool allow_rescheduled,
                             uintptr_t old_mutex_state) __TA_NO_THREAD_SAFETY_ANALYSIS;

  static constexpr uint32_t MAGIC = 0x6D757478;  // 'mutx'
  static constexpr uintptr_t STATE_FREE = 0u;
  static constexpr uintptr_t STATE_FLAG_CONTESTED = 1u;

  // Accessors to extract the holder pointer from the val member
  uintptr_t val() const { return val_.load(ktl::memory_order_relaxed); }

  static Thread* holder_from_val(uintptr_t value) {
    return reinterpret_cast<Thread*>(value & ~STATE_FLAG_CONTESTED);
  }

  Thread* holder() const { return holder_from_val(val()); }

  fbl::Canary<MAGIC> magic_;
  ktl::atomic<uintptr_t> val_{STATE_FREE};
  OwnedWaitQueue wait_;
};

// Lock policy for kernel mutexes
//
struct MutexPolicy {
  struct State {
    const zx_duration_t spin_max_duration{Mutex::SPIN_MAX_DURATION};
  };

  // Basic acquire and release operations.
  template <typename LockType>
  static bool Acquire(LockType* lock, State* state) TA_ACQ(lock) TA_EXCL(thread_lock) {
    lock->Acquire(state->spin_max_duration);
    return true;
  }
  template <typename LockType>
  static void Release(LockType* lock, State*) TA_REL(lock) TA_EXCL(thread_lock) {
    lock->Release();
  }

  // Runtime lock assertions.
  template <typename LockType>
  static void AssertHeld(const LockType& lock) TA_ASSERT(lock) {
    lock.AssertHeld();
  }

  // A enum tag that can be passed to Guard<Mutex>::Release(...) to
  // select the special-case release method below.
  enum SelectThreadLockHeld { ThreadLockHeld };

  // Specifies whether the special-case release method below should
  // reschedule.
  enum RescheduleOption : bool {
    NoReschedule = false,
    Reschedule = true,
  };

  // Releases the lock using the special mutex release operation. This
  // is selected by calling:
  //
  //  Guard<TrivialMutex|Mutex|Mutex>::Release(ThreadLockHeld [, Reschedule | NoReschedule])
  //
  template <typename LockType>
  static void Release(LockType* lock, State*, SelectThreadLockHeld,
                      RescheduleOption reschedule = Reschedule) TA_REL(lock) TA_REQ(thread_lock) {
    lock->ReleaseThreadLocked(reschedule);
  }
};

// Configure the lockdep::Guard for kernel mutexes to use MutexPolicy.
LOCK_DEP_POLICY(Mutex, MutexPolicy);

// Declares a Mutex member of the struct or class |containing_type|.
//
// Example usage:
//
// struct MyType {
//     DECLARE_MUTEX(MyType [, LockFlags]) lock;
// };
//
#define DECLARE_MUTEX(containing_type, ...) \
  LOCK_DEP_INSTRUMENT(containing_type, ::Mutex, ##__VA_ARGS__)

// Declares a |lock_type| member of the struct or class |containing_type|.
//
// Example usage:
//
// struct MyType {
//     DECLARE_LOCK(MyType, LockType [, LockFlags]) lock;
// };
//
#define DECLARE_LOCK(containing_type, lock_type, ...) \
  LOCK_DEP_INSTRUMENT(containing_type, lock_type, ##__VA_ARGS__)

// By default, singleton mutexes in the kernel use Mutex in order to avoid
// a useless global dtor
//
// Example usage:
//
//  DECLARE_SINGLETON_MUTEX(MyGlobalLock [, LockFlags]);
//
#define DECLARE_SINGLETON_MUTEX(name, ...) LOCK_DEP_SINGLETON_LOCK(name, ::Mutex, ##__VA_ARGS__)

// Declares a singleton |lock_type| with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_LOCK(MyGlobalLock, LockType, [, LockFlags]);
//
#define DECLARE_SINGLETON_LOCK(name, lock_type, ...) \
  LOCK_DEP_SINGLETON_LOCK(name, lock_type, ##__VA_ARGS__)

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_MUTEX_H_
