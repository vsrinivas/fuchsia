// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <lib/sync/condition.h>
#include <lib/sync/mutex.h>
#include <pthread.h>
#include <vector>
#include <zircon/assert.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include "event.h"
#include "thread.h"
#include "tracer.h"
#include "utils.h"

constexpr uint32_t THREAD_COUNT = 5;
using ThreadCollection = std::array<std::unique_ptr<Thread>, THREAD_COUNT>;
enum class PrioInherit : bool { No = false, Yes };

///////////////////////////////////////////////////////
//
// libsync Synchronization primitives
//
///////////////////////////////////////////////////////
class TA_CAP("mutex") LibSyncMutex {
 public:
  static const char* Name() { return "sync_mutex_t"; }

  LibSyncMutex() = default;
  ~LibSyncMutex() = default;
  void Acquire() TA_ACQ() { sync_mutex_lock(&mutex_); }
  void Release() TA_REL() { sync_mutex_unlock(&mutex_); }

 private:
  sync_mutex_t mutex_;
};

class TA_CAP("mutex") LibSyncCondVar {
 public:
  static const char* Name() { return "sync_condition_t"; }

  LibSyncCondVar() = default;
  ~LibSyncCondVar() = default;
  void AcquireLock() TA_ACQ() { sync_mutex_lock(&mutex_); }
  void ReleaseLock() TA_REL() { sync_mutex_unlock(&mutex_); }

  void Broadcast() { sync_condition_broadcast(&condition_); }
  void Signal() { sync_condition_signal(&condition_); }
  void Wait() { sync_condition_wait(&condition_, &mutex_); }

 private:
  sync_condition_t condition_;
  sync_mutex_t mutex_;
};

///////////////////////////////////////////////////////
//
// pthread Synchronization primitives
//
///////////////////////////////////////////////////////
template <PrioInherit EnablePi>
class TA_CAP("mutex") PThreadMutex {
 public:
  static const char* Name() {
    if constexpr (EnablePi == PrioInherit::Yes) {
      return "pthread_mutex_t with PI";
    } else {
      return "pthread_mutex_t without PI";
    }
  }

  PThreadMutex() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    if constexpr (EnablePi == PrioInherit::Yes) {
      pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    }

    pthread_mutex_init(&mutex_, &attr);
    pthread_mutexattr_destroy(&attr);
  }

  ~PThreadMutex() { pthread_mutex_destroy(&mutex_); }
  void Acquire() TA_ACQ() { pthread_mutex_lock(&mutex_); }
  void Release() TA_REL() { pthread_mutex_unlock(&mutex_); }

 private:
  pthread_mutex_t mutex_;
};

template <PrioInherit EnablePi>
class TA_CAP("mutex") PThreadCondVar {
 public:
  static const char* Name() {
    if constexpr (EnablePi == PrioInherit::Yes) {
      return "pthread_cond_t with PI";
    } else {
      return "pthread_cond_t without PI";
    }
  }

  PThreadCondVar() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    if constexpr (EnablePi == PrioInherit::Yes) {
      pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    }

    pthread_cond_init(&condition_, nullptr);
    pthread_mutex_init(&mutex_, &attr);
    pthread_mutexattr_destroy(&attr);
  }

  ~PThreadCondVar() {
    pthread_cond_destroy(&condition_);
    pthread_mutex_destroy(&mutex_);
  }

  void AcquireLock() TA_ACQ() { pthread_mutex_lock(&mutex_); }
  void ReleaseLock() TA_REL() { pthread_mutex_unlock(&mutex_); }

  void Broadcast() { pthread_cond_broadcast(&condition_); }
  void Signal() { pthread_cond_signal(&condition_); }
  void Wait() { pthread_cond_wait(&condition_, &mutex_); }

 private:
  pthread_cond_t condition_;
  pthread_mutex_t mutex_;
};

///////////////////////////////////////////////////////
//
// C11 Synchronization primitives
//
///////////////////////////////////////////////////////
class TA_CAP("mutex") MtxTMutex {
 public:
  static const char* Name() { return "mtx_t"; }

  MtxTMutex() { mtx_init(&mutex_, mtx_plain); }
  ~MtxTMutex() { mtx_destroy(&mutex_); }
  void Acquire() TA_ACQ() { mtx_lock(&mutex_); }
  void Release() TA_REL() { mtx_unlock(&mutex_); }

 private:
  mtx_t mutex_;
};

class TA_CAP("mutex") CndTCondVar {
 public:
  static const char* Name() { return "cnd_t"; }

  CndTCondVar() {
    cnd_init(&condition_);
    mtx_init(&mutex_, mtx_plain);
  }

  ~CndTCondVar() {
    cnd_destroy(&condition_);
    mtx_destroy(&mutex_);
  }

  void AcquireLock() TA_ACQ() { mtx_lock(&mutex_); }
  void ReleaseLock() TA_REL() { mtx_unlock(&mutex_); }

  void Broadcast() { cnd_broadcast(&condition_); }
  void Signal() { cnd_signal(&condition_); }
  void Wait() { cnd_wait(&condition_, &mutex_); }

 private:
  cnd_t condition_;
  mtx_t mutex_;
};

///////////////////////////////////////////////////////
//
// libfbl Synchronization primitives
//
///////////////////////////////////////////////////////
class TA_CAP("mutex") FblMutex {
 public:
  static const char* Name() { return "fbl::Mutex"; }

  FblMutex() = default;
  ~FblMutex() = default;
  void Acquire() TA_ACQ() { mutex_.Acquire(); }
  void Release() TA_REL() { mutex_.Release(); }

 private:
  fbl::Mutex mutex_;
};

template <typename MutexType>
zx_status_t ExerciseMutexChain(ThreadCollection* _threads) {
  ZX_DEBUG_ASSERT(_threads != nullptr);
  ZX_DEBUG_ASSERT(_threads->size() >= 2);
  auto& threads = *_threads;

  zx_status_t res = ZX_ERR_INTERNAL;
  ;
  struct chain_node_t {
    Event exit_evt;
    Event ready_evt;
    MutexType hold_mutex;
    MutexType* blocking_mutex = nullptr;
  };

  auto nodes = std::unique_ptr<chain_node_t[]>(new chain_node_t[threads.size()]);

  Tracer::Trace(TRACE_SCOPE_PROCESS, "Setting up mutex chain; type = \"%s\"", MutexType::Name());
  for (uint32_t i = 0; i < threads.size(); ++i) {
    auto& node = nodes[i];
    auto& thread = *(threads[i]);

    if (i > 0) {
      node.blocking_mutex = &nodes[i - 1].hold_mutex;
    }

    res = thread.Start([&node]() {
      node.hold_mutex.Acquire();
      node.ready_evt.Signal();

      if (node.blocking_mutex != nullptr) {
        node.blocking_mutex->Acquire();
        node.exit_evt.Wait();
        node.blocking_mutex->Release();
      } else {
        node.exit_evt.Wait();
      }

      node.hold_mutex.Release();
    });

    if (res != ZX_OK) {
      fprintf(stderr, "Failed to start \"%s\" (res = %d)\n", thread.name(), res);
      return res;
    }

    res = node.ready_evt.Wait(zx::msec(500));
    if (res != ZX_OK) {
      fprintf(stderr, "Time out waiting for \"%s\" to become ready (res = %d)\n", thread.name(),
              res);
      return res;
    }
  }

  for (uint32_t i = 0; i < threads.size(); ++i) {
    nodes[i].exit_evt.Signal();
    threads[i]->WaitForReset();
  }

  return ZX_OK;
}

template <typename MutexType>
zx_status_t ExerciseMutexMultiWait(ThreadCollection* _threads) {
  ZX_DEBUG_ASSERT(_threads != nullptr);
  ZX_DEBUG_ASSERT(_threads->size() >= 2);
  auto& threads = *_threads;

  zx_status_t res = ZX_ERR_INTERNAL;
  ;
  MutexType the_mutex;
  Event exit_evt;
  Event ready_evt;

  Tracer::Trace(TRACE_SCOPE_PROCESS, "Setting up multi-wait; type = \"%s\"", MutexType::Name());
  for (uint32_t i = 0; i < threads.size(); ++i) {
    auto& thread = *(threads[i]);
    if (i == 0) {
      res = thread.Start([&the_mutex, &exit_evt, &ready_evt]() {
        the_mutex.Acquire();
        ready_evt.Signal();
        exit_evt.Wait();
        the_mutex.Release();
      });

      res = ready_evt.Wait(zx::msec(500));
      if (res != ZX_OK) {
        fprintf(stderr, "Time out waiting for \"%s\" to become ready (res = %d)\n", thread.name(),
                res);
        return res;
      }
    } else {
      res = thread.Start([&the_mutex, &exit_evt]() {
        the_mutex.Acquire();
        exit_evt.Wait();
        the_mutex.Release();
      });
    }

    if (res != ZX_OK) {
      fprintf(stderr, "Failed to start \"%s\" (res = %d)\n", thread.name(), res);
      return res;
    }
  }

  exit_evt.Signal();
  for (auto& thread : threads) {
    thread->WaitForReset();
  }

  return ZX_OK;
}

template <typename CondVarType>
zx_status_t ExerciseCondvarBroadcast(ThreadCollection* _threads) {
  ZX_DEBUG_ASSERT(_threads != nullptr);
  ZX_DEBUG_ASSERT(_threads->size() >= 2);
  auto& threads = *_threads;

  struct {
    CondVarType the_condvar;
    uint32_t exit_threshold TA_GUARDED(the_condvar);
  } ctx;

  ctx.the_condvar.AcquireLock();
  ctx.exit_threshold = 1000;
  ctx.the_condvar.ReleaseLock();

  Tracer::Trace(TRACE_SCOPE_PROCESS, "Setting up condvar broadcast; type = \"%s\"",
                CondVarType::Name());

  for (uint32_t i = 0; i < threads.size(); ++i) {
    auto& thread = *(threads[i]);
    uint32_t next_prio = i ? threads[i - 1]->prio() : 0;
    zx_status_t res;

    res = thread.Start([&ctx, &thread, next_prio]() {
      ctx.the_condvar.AcquireLock();
      while (thread.prio() < ctx.exit_threshold) {
        ctx.the_condvar.Wait();
        // Linger in the lock for a bit to encourage contention
        zx::nanosleep(zx::deadline_after(zx::usec(250)));
      }
      ctx.exit_threshold = next_prio;
      ctx.the_condvar.Broadcast();
      ctx.the_condvar.ReleaseLock();
    });

    if (res != ZX_OK) {
      fprintf(stderr, "Failed to start \"%s\" (res = %d)\n", thread.name(), res);
      return res;
    }
  }

  // Now that all of the threads are set up and waiting, set the exit
  // threshold and signal the condvar.
  ctx.the_condvar.AcquireLock();
  ctx.exit_threshold = threads[threads.size() - 1]->prio();
  ctx.the_condvar.Broadcast();
  ctx.the_condvar.ReleaseLock();

  for (auto& thread : threads) {
    thread->WaitForReset();
  }

  return ZX_OK;
}

int main(int argc, char** argv) {
  zx_status_t res;
  ThreadCollection threads;

  // Create the thread objects for the threads we will use during testing.  We
  // don't actually want to create new threads for each pass of the testing as
  // that makes the traces difficult to read.  Having one set we use over and
  // over should be sufficient.
  constexpr uint32_t BASE_PRIO = 3;
  constexpr uint32_t PRIO_SPACING = 2;

  for (uint32_t i = 0; i < threads.size(); ++i) {
    threads[i] = std::make_unique<Thread>(BASE_PRIO + (i * PRIO_SPACING));
  }

  Tracer the_tracer;
  res = the_tracer.Start();
  if (res != ZX_OK) {
    return -1;
  }

  res = Thread::ConnectSchedulerService();
  if (res != ZX_OK) {
    fprintf(stderr, "Failed to start connect to scheduler service (res = %d)\n", res);
    return -1;
  }

  using TrialFn = zx_status_t (*)(ThreadCollection*);
  constexpr TrialFn TRIALS[] = {
      ExerciseMutexChain<LibSyncMutex>,
      ExerciseMutexMultiWait<LibSyncMutex>,
      ExerciseMutexChain<PThreadMutex<PrioInherit::No>>,
      ExerciseMutexMultiWait<PThreadMutex<PrioInherit::No>>,
      ExerciseMutexChain<PThreadMutex<PrioInherit::Yes>>,
      ExerciseMutexMultiWait<PThreadMutex<PrioInherit::Yes>>,
      ExerciseMutexChain<MtxTMutex>,
      ExerciseMutexMultiWait<MtxTMutex>,
      ExerciseMutexChain<FblMutex>,
      ExerciseMutexMultiWait<FblMutex>,
      ExerciseCondvarBroadcast<LibSyncCondVar>,
      ExerciseCondvarBroadcast<PThreadCondVar<PrioInherit::No>>,
      ExerciseCondvarBroadcast<PThreadCondVar<PrioInherit::Yes>>,
      ExerciseCondvarBroadcast<CndTCondVar>,
  };

  for (auto& DoTrial : TRIALS) {
    if (DoTrial(&threads) != ZX_OK) {
      return -1;
    }
  }

  Tracer::Trace(TRACE_SCOPE_PROCESS, "Finished!");
  return 0;
}
