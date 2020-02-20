// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_BRWLOCK_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_BRWLOCK_H_

#include <assert.h>
#include <debug.h>
#include <endian.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdint.h>

#include <fbl/canary.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/wait.h>
#include <ktl/atomic.h>

namespace internal {

enum class BrwLockEnablePi : bool {
  No = false,
  Yes = true,
};

template <BrwLockEnablePi PI>
struct BrwLockWaitQueueType;

template <>
struct BrwLockWaitQueueType<BrwLockEnablePi::Yes> {
  using Type = OwnedWaitQueue;
};

template <>
struct BrwLockWaitQueueType<BrwLockEnablePi::No> {
  using Type = WaitQueue;
};

template <BrwLockEnablePi PI>
struct BrwLockState;

template <>
struct alignas(16) BrwLockState<BrwLockEnablePi::Yes> {
  BrwLockState(uint64_t state) : state_(state), writer_(nullptr) {}
  ktl::atomic<uint64_t> state_;
  ktl::atomic<Thread*> writer_;
};

static_assert(sizeof(BrwLockState<BrwLockEnablePi::Yes>) == 16,
              "PI BrwLockState expected to be exactly 16 bytes");
static_assert(BYTE_ORDER == LITTLE_ENDIAN, "PI BrwLockState assumptions little endian ordering");

template <>
struct BrwLockState<BrwLockEnablePi::No> {
  BrwLockState(uint64_t state) : state_(state) {}
  ktl::atomic<uint64_t> state_;
};

static_assert(sizeof(BrwLockState<BrwLockEnablePi::No>) == 8,
              "Non PI BrwLockState expected to be exactly 8 bytes");

// Blocking (i.e. non spinning) reader-writer lock. Readers and writers are
// ordered by priority (i.e. their wait_queue release order) and otherwise
// readers and writers are treated equally and will fall back to FIFO ordering
// at some priority.
// The lock optionally respects priority inheritance. Not supporting PI is more
// efficient as the current active writer does not have to be tracked. Enabling PI
// creates an additional restriction that readers must not take any additional
// locks or otherwise block whilst holding the read lock.
template <BrwLockEnablePi PI>
class TA_CAP("mutex") BrwLock {
 public:
  BrwLock() {}
  ~BrwLock();

  void ReadAcquire() TA_ACQ_SHARED(this) TA_NO_THREAD_SAFETY_ANALYSIS {
    DEBUG_ASSERT(!arch_blocking_disallowed());
    canary_.Assert();
    if constexpr (PI == BrwLockEnablePi::Yes) {
      // As readers are not recorded and do not receive boosting from blocking
      // writers they must not block or otherwise cease to run, otherwise
      // our PI will be violated.
      CurrentThread::PreemptDisable();
    }
    // Attempt the optimistic grab
    uint64_t prev = state_.state_.fetch_add(kBrwLockReader, ktl::memory_order_acquire);
    // See if there are only readers
    if (unlikely((prev & kBrwLockReaderMask) != prev)) {
      ContendedReadAcquire();
    }
  }

  void WriteAcquire() TA_ACQ(this) {
    DEBUG_ASSERT(!arch_blocking_disallowed());
    canary_.Assert();
    // When acquiring the write lock we require there be no-one else using
    // the lock.
    CommonWriteAcquire(kBrwLockUnlocked, [this] { ContendedWriteAcquire(); });
  }

  void WriteRelease() TA_REL(this) TA_NO_THREAD_SAFETY_ANALYSIS;

  void ReadRelease() TA_REL_SHARED(this) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    uint64_t prev = state_.state_.fetch_sub(kBrwLockReader, ktl::memory_order_release);
    if (unlikely((prev & kBrwLockReaderMask) == 1 && (prev & kBrwLockWaiterMask) != 0)) {
      // there are no readers but still some waiters, becomes our job to wake them up
      ReleaseWakeup();
    }
    if constexpr (PI == BrwLockEnablePi::Yes) {
      CurrentThread::PreemptReenable();
    }
  }

  void ReadUpgrade() TA_REL_SHARED(this) TA_ACQ(this) TA_NO_THREAD_SAFETY_ANALYSIS {
    canary_.Assert();
    DEBUG_ASSERT(!arch_blocking_disallowed());
    // To upgrade we require that we as a current reader be the only current
    // user of the lock.
    CommonWriteAcquire(kBrwLockReader, [this] { ContendedReadUpgrade(); });
  }

  // suppress default constructors
  DISALLOW_COPY_ASSIGN_AND_MOVE(BrwLock);

  // Tag structs needed for linking BrwLock acquisition options to the different
  // policy structures. See LOCK_DEP_POLICY_OPTION usage below.
  struct Reader {};
  struct Writer {};

  struct ReaderPolicy {
    struct State {};
    // This will be seen by Guard to know to generate shared acquisitions for thread analysis.
    struct Shared {};

    static bool Acquire(BrwLock* lock, State* state) TA_ACQ_SHARED(lock) {
      lock->ReadAcquire();
      return true;
    }
    static void Release(BrwLock* lock, State* state) TA_REL_SHARED(lock) { lock->ReadRelease(); }
  };

  struct WriterPolicy {
    struct State {};

    static bool Acquire(BrwLock* lock, State* state) TA_ACQ(lock) {
      lock->WriteAcquire();
      return true;
    }
    static void Release(BrwLock* lock, State* state) TA_REL(lock) { lock->WriteRelease(); }
  };

 private:
  static constexpr uint64_t kBrwLockUnlocked = 0;
  // We count readers in the low part of the state
  static constexpr uint64_t kBrwLockReader = 1;
  static constexpr uint64_t kBrwLockReaderMask = 0xFFFFFFFF;
  // We count waiters in all but the MSB of the state
  static constexpr uint64_t kBrwLockWaiter = 1ul << 32;
  static constexpr uint64_t kBrwLockWaiterMask = 0x7FFFFFFF00000000;
  // Writer is in the MSB
  static constexpr uint64_t kBrwLockWriter = 1ul << 63;

  void ContendedReadAcquire();
  void ContendedWriteAcquire();
  void ContendedReadUpgrade();
  void ReleaseWakeup();
  void Block(bool write) TA_REQ(thread_lock);
  ResourceOwnership Wake() TA_REQ(thread_lock);

  template <typename F>
  void CommonWriteAcquire(uint64_t expected_state_bits, F contended)
      TA_ACQ(this) TA_NO_THREAD_SAFETY_ANALYSIS {
    Thread* __UNUSED ct = get_current_thread();

    bool success;

    if constexpr (PI == BrwLockEnablePi::Yes) {
      // To prevent a race between setting the kBrwLocKWriter bit and the writer_ we
      // perform a 16byte compare and swap of both values. This ensures that Block
      // can never fail to see a writer_. Other possibilities are
      //   * Disable interrupts: This would be correct, but disabling interrupts
      //     is more expensive than a 16byte CAS
      //   * thread_preempt_disable: Cheaper than disabling interrupts but is
      //     *INCORRECT* as when preemption happens we must take the thread_lock to
      //     proceed, but Block must hold the thread lock until it observes that
      //     writer_ has been set, thus resulting in deadlock.
      static_assert(alignof(BrwLockState<PI>) >= sizeof(unsigned __int128));
      static_assert(sizeof(state_) == sizeof(unsigned __int128));
      static_assert(offsetof(BrwLockState<PI>, state_) == 0);
      static_assert(offsetof(BrwLockState<PI>, writer_) == 8);
      unsigned __int128* raw_state = reinterpret_cast<unsigned __int128*>(&state_);

      unsigned __int128 expected = static_cast<unsigned __int128>(expected_state_bits);
      unsigned __int128 desired = static_cast<unsigned __int128>(kBrwLockWriter) |
                                  static_cast<unsigned __int128>(reinterpret_cast<uintptr_t>(ct))
                                      << 64;

      // TODO(maniscalco): Ideally, we'd use a ktl::atomic/std::atomic here, but that's not easy to
      // do. Once we have std::atomic_ref, raw_state can become a struct and we can stop using the
      // compiler builtin without triggering UB.
      success = __atomic_compare_exchange_n(raw_state, &expected, desired, /*weak=*/false,
                                            /*success_memmodel=*/__ATOMIC_ACQUIRE,
                                            /*failure_memmodel=*/__ATOMIC_RELAXED);

    } else {
      success = state_.state_.compare_exchange_strong(expected_state_bits, kBrwLockWriter,
                                                      ktl::memory_order_acquire,
                                                      ktl::memory_order_relaxed);
    }

    if (unlikely(!success)) {
      contended();
      if constexpr (PI == BrwLockEnablePi::Yes) {
        DEBUG_ASSERT(state_.writer_.load(ktl::memory_order_relaxed) == ct);
      }
    }
  }

  fbl::Canary<fbl::magic("RWLK")> canary_;
  BrwLockState<PI> state_ = kBrwLockUnlocked;
  typename BrwLockWaitQueueType<PI>::Type wait_;
};

// Must declare policy options whilst in the internal namespace for ADL resolution to work.
using BrwLockPi = BrwLock<BrwLockEnablePi::Yes>;

// Configure fbl::Guard<BrwLockPi, BrwLockPi::Writer> write locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLockPi, BrwLockPi::Writer, BrwLockPi::WriterPolicy);
// Configure fbl::Guard<BrwLockPi, BrwLockPi::Reader> read locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLockPi, BrwLockPi::Reader, BrwLockPi::ReaderPolicy);

using BrwLockNoPi = BrwLock<BrwLockEnablePi::No>;

// Configure fbl::Guard<BrwLockNoPi, BrwLockNoPi::Writer> write locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLockNoPi, BrwLockNoPi::Writer, BrwLockNoPi::WriterPolicy);
// Configure fbl::Guard<BrwLockNoPi, BrwLockNoPi::Reader> read locks through the given policy.
LOCK_DEP_POLICY_OPTION(BrwLockNoPi, BrwLockNoPi::Reader, BrwLockNoPi::ReaderPolicy);

}  // namespace internal

using ::internal::BrwLockPi;

#define DECLARE_BRWLOCK_PI(container_type) LOCK_DEP_INSTRUMENT(container_type, BrwLockPi)
#define DECLARE_SINGLETON_BRWLOCK_PI(name, ...) \
  LOCK_DEP_SINGLETON_LOCK(name, BrwLockPi, ##__VA_ARGS__)

using BrwLockNoPi = internal::BrwLockNoPi;

#define DECLARE_BRWLOCK_NO_PI(container_type) LOCK_DEP_INSTRUMENT(container_type, BrwLockNoPi)
#define DECLARE_SINGLETON_BRWLOCK_NO_PI(name, ...) \
  LOCK_DEP_SINGLETON_LOCK(name, BrwLockNoPi, ##__VA_ARGS__)

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_BRWLOCK_H_
