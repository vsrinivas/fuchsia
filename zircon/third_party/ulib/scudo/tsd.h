//===-- tsd.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_H_
#define SCUDO_TSD_H_

#include "atomic_helpers.h"
#include "common.h"
#include "linux.h"
#include "mutex.h"

#include <limits.h> // for PTHREAD_DESTRUCTOR_ITERATIONS
#include <pthread.h>

namespace scudo {

template <class Allocator> struct ALIGNED(SCUDO_CACHE_LINE_SIZE) TSD {
  typename Allocator::CacheT Cache;
  typename Allocator::QuarantineCacheT QuarantineCache;
  u8 DestructorIterations;

  void initLinkerInitialized(Allocator *Instance) {
    Instance->initCache(&Cache);
    DestructorIterations = PTHREAD_DESTRUCTOR_ITERATIONS;
  }
  void init(Allocator *Instance) {
    memset(this, 0, sizeof(*this));
    initLinkerInitialized(Instance);
  }

  void commitBack(Allocator *Instance) { Instance->commitBack(this); }

  INLINE bool tryLock() {
    if (Mutex.tryLock()) {
      atomic_store_relaxed(&Precedence, 0);
      return true;
    }
    if (atomic_load_relaxed(&Precedence) == 0)
      atomic_store_relaxed(
          &Precedence,
          static_cast<uptr>(getMonotonicTime() >> FIRST_32_SECOND_64(16, 0)));
    return false;
  }
  INLINE void lock() {
    atomic_store_relaxed(&Precedence, 0);
    Mutex.lock();
  }
  INLINE void unlock() { Mutex.unlock(); }
  INLINE uptr getPrecedence() { return atomic_load_relaxed(&Precedence); }

private:
  StaticSpinMutex Mutex;
  atomic_uptr Precedence;
};

// Exclusive TSD Registry
// TODO(kostyak): split?

enum ThreadState : u8 {
  ThreadNotInitialized = 0,
  ThreadInitialized,
  ThreadTornDown,
};

template <class Allocator> void teardownThread(void *Ptr);

template <class Allocator> struct TSDRegistryExT {
  ALWAYS_INLINE void initThreadMaybe(Allocator *Instance, bool MinimalInit) {
    if (LIKELY(State != ThreadNotInitialized))
      return;
    initThread(Instance, MinimalInit);
  }
  TSD<Allocator> *getTSDAndLock(bool *UnlockRequired) {
    if (UNLIKELY(State != ThreadInitialized)) {
      FallbackTSD->lock();
      *UnlockRequired = true;
      return FallbackTSD;
    }
    *UnlockRequired = false;
    return &ThreadTSD;
  }

private:
  NOINLINE void initOnce(Allocator *Instance) {
    BlockingMutexLock L(&Mutex);
    if (OnceDone)
      return;
    CHECK_EQ(pthread_key_create(&PThreadKey, teardownThread<Allocator>), 0);
    Instance->initLinkerInitialized();
    FallbackTSD = reinterpret_cast<TSD<Allocator> *>(
        map(nullptr, sizeof(TSD<Allocator>), "scudo:tsd"));
    FallbackTSD->initLinkerInitialized(Instance);
    OnceDone = true;
  }
  NOINLINE void initThread(Allocator *Instance, bool MinimalInit) {
    if (UNLIKELY(!OnceDone))
      initOnce(Instance);
    if (UNLIKELY(MinimalInit))
      return;
    CHECK_EQ(
        pthread_setspecific(PThreadKey, reinterpret_cast<void *>(Instance)), 0);
    ThreadTSD.initLinkerInitialized(Instance);
    State = ThreadInitialized;
  }

  BlockingMutex Mutex;
  bool OnceDone;
  pthread_key_t PThreadKey;
  TSD<Allocator> *FallbackTSD;
  static THREADLOCAL ThreadState State;
  static THREADLOCAL TSD<Allocator> ThreadTSD;

  friend void teardownThread<Allocator>(void *Ptr);
};

template <class Allocator>
THREADLOCAL TSD<Allocator> TSDRegistryExT<Allocator>::ThreadTSD;
template <class Allocator>
THREADLOCAL ThreadState TSDRegistryExT<Allocator>::State;

template <class Allocator> void teardownThread(void *Ptr) {
  typedef TSDRegistryExT<Allocator> TSDRegistryT;
  Allocator *Instance = reinterpret_cast<Allocator *>(Ptr);
  // The glibc POSIX thread-local-storage deallocation routine calls user
  // provided destructors in a loop of PTHREAD_DESTRUCTOR_ITERATIONS.
  // We want to be called last since other destructors might call free and the
  // like, so we wait until PTHREAD_DESTRUCTOR_ITERATIONS before draining the
  // quarantine and swallowing the cache.
  const uptr N = TSDRegistryT::ThreadTSD.DestructorIterations;
  if (N > 1) {
    TSDRegistryT::ThreadTSD.DestructorIterations = N - 1;
    // If pthread_setspecific fails, we will go ahead with the teardown.
    if (LIKELY(pthread_setspecific(Instance->getTSDRegistry()->PThreadKey,
                                   reinterpret_cast<void *>(Instance)) == 0))
      return;
  }
  TSDRegistryT::ThreadTSD.commitBack(Instance);
  TSDRegistryT::State = ThreadTornDown;
}

// Shared TSD Registry
// TODO(kostyak): split?

template <class Allocator, u32 MaxTSDCount> struct TSDRegistrySharedT {
  ALWAYS_INLINE void initThreadMaybe(Allocator *Instance,
                                     UNUSED bool MinimalInit) {
    if (LIKELY(getCurrentTSD()))
      return;
    initThread(Instance);
  }

  ALWAYS_INLINE TSD<Allocator> *getTSDAndLock(bool *UnlockRequired) {
    TSD<Allocator> *TSD = getCurrentTSD();
    DCHECK(TSD && "No TSD associated with the current thread!");
    *UnlockRequired = true;
    // Try to lock the currently associated context.
    if (TSD->tryLock())
      return TSD;
    // If it failed, go the slow path.
    return getTSDAndLockSlow(TSD);
  }

private:
  NOINLINE void initOnce(Allocator *Instance) {
    BlockingMutexLock L(&Mutex);
    if (OnceDone)
      return;
    CHECK_EQ(pthread_key_create(&PThreadKey, nullptr), 0); // For non-TLS
    Instance->initLinkerInitialized();
    NumberOfTSDs = Min(Max(1U, getNumberOfCPUs()), MaxTSDCount);
    TSDs = reinterpret_cast<TSD<Allocator> *>(
        map(nullptr, sizeof(TSD<Allocator>) * NumberOfTSDs, "scudo:tsd"));
    for (u32 I = 0; I < NumberOfTSDs; I++) {
      TSDs[I].initLinkerInitialized(Instance);
      u32 A = I + 1;
      u32 B = NumberOfTSDs;
      while (B != 0) {
        const u32 T = A;
        A = B;
        B = T % B;
      }
      if (A == 1)
        CoPrimes[NumberOfCoPrimes++] = I + 1;
    }
    OnceDone = true;
  }

  ALWAYS_INLINE void setCurrentTSD(TSD<Allocator> *CurrentTSD) {
#if SCUDO_ANDROID
    *getAndroidTlsPtr() = reinterpret_cast<uptr>(CurrentTSD);
#elif SCUDO_LINUX
    ThreadTSD = CurrentTSD;
#else
    CHECK_EQ(
        pthread_setspecific(PThreadKey, reinterpret_cast<void *>(CurrentTSD)),
        0);
#endif
  }

  ALWAYS_INLINE TSD<Allocator> *getCurrentTSD() {
#if SCUDO_ANDROID
    return reinterpret_cast<TSD<Allocator> *>(*getAndroidTlsPtr());
#elif SCUDO_LINUX
    return ThreadTSD;
#else
    return reinterpret_cast<TSD<Allocator> *>(pthread_getspecific(PThreadKey));
#endif
  }

  NOINLINE void initThread(Allocator *Instance) {
    if (UNLIKELY(!OnceDone))
      initOnce(Instance);
    // Initial context assignment is done in a plain round-robin fashion.
    const u32 Index = atomic_fetch_add(&CurrentIndex, 1, memory_order_relaxed);
    setCurrentTSD(&TSDs[Index % NumberOfTSDs]);
  }

  NOINLINE TSD<Allocator> *getTSDAndLockSlow(TSD<Allocator> *CurrentTSD) {
    if (NumberOfTSDs > 1U) {
      // Use the Precedence of the current TSD as our random seed. Since we are
      // in the slow path, it means that tryLock failed, and as a result it's
      // very likely that said Precedence is non-zero.
      u32 RandState = static_cast<u32>(CurrentTSD->getPrecedence());
      const u32 R = getRandomU32(&RandState);
      const u32 Inc = CoPrimes[R % NumberOfCoPrimes];
      u32 Index = R % NumberOfTSDs;
      uptr LowestPrecedence = UINTPTR_MAX;
      TSD<Allocator> *CandidateTSD = nullptr;
      // Go randomly through at most 4 contexts and find a candidate.
      for (u32 I = 0; I < Min(4U, NumberOfTSDs); I++) {
        if (TSDs[Index].tryLock()) {
          setCurrentTSD(&TSDs[Index]);
          return &TSDs[Index];
        }
        const uptr Precedence = TSDs[Index].getPrecedence();
        // A 0 precedence here means another thread just locked this TSD.
        if (UNLIKELY(Precedence == 0))
          continue;
        if (Precedence < LowestPrecedence) {
          CandidateTSD = &TSDs[Index];
          LowestPrecedence = Precedence;
        }
        Index += Inc;
        if (Index >= NumberOfTSDs)
          Index -= NumberOfTSDs;
      }
      if (CandidateTSD) {
        CandidateTSD->lock();
        setCurrentTSD(CandidateTSD);
        return CandidateTSD;
      }
    }
    // Last resort, stick with the current one.
    CurrentTSD->lock();
    return CurrentTSD;
  }

  BlockingMutex Mutex;
  bool OnceDone;
  pthread_key_t PThreadKey;
  atomic_u32 CurrentIndex;
  TSD<Allocator> *TSDs;
  u32 NumberOfTSDs;
  u32 CoPrimes[MaxTSDCount];
  u32 NumberOfCoPrimes;
#if SCUDO_LINUX && !SCUDO_ANDROID
  static THREADLOCAL TSD<Allocator> *ThreadTSD;
#endif
};

#if SCUDO_LINUX && !SCUDO_ANDROID
template <class Allocator, u32 MaxTSDCount>
THREADLOCAL TSD<Allocator>
    *TSDRegistrySharedT<Allocator, MaxTSDCount>::ThreadTSD;
#endif

} // namespace scudo

#endif // SCUDO_TSD_H_
