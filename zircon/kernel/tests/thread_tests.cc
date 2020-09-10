// Copyright 2016, 2018 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <pretty/hexdump.h>

#include "tests.h"

// NOTE: The tests in this file are meant for interactive use only. Use a minimal
// build and in the console type "k thread_tests".

static uint rand_range(uint low, uint high) {
  uint r = rand();
  uint result = ((r ^ (r >> 16)) % (high - low + 1u)) + low;

  return result;
}

static int sleep_thread(void* arg) {
  for (;;) {
    printf("sleeper %p\n", Thread::Current::Get());
    Thread::Current::SleepRelative(ZX_MSEC(rand() % 500));
  }
  return 0;
}

static int mutex_thread(void* arg) {
  int i;
  const int iterations = 1000000;
  int count = 0;

  static volatile uintptr_t shared = 0;

  auto m = reinterpret_cast<Mutex*>(arg);

  printf("mutex tester thread %p starting up, will go for %d iterations\n", Thread::Current::Get(),
         iterations);

  for (i = 0; i < iterations; i++) {
    m->Acquire();

    if (shared != 0)
      panic("someone else has messed with the shared data\n");

    shared = (intptr_t)Thread::Current::Get();
    if ((rand() % 5) == 0)
      Thread::Current::Yield();

    if (++count % 10000 == 0)
      printf("%p: count %d\n", Thread::Current::Get(), count);
    shared = 0;

    m->Release();
    if ((rand() % 5) == 0)
      Thread::Current::Yield();
  }

  printf("mutex tester %p done\n", Thread::Current::Get());

  return 0;
}

static int mutex_test() {
  static Mutex imutex;
  printf("preinitialized mutex:\n");
  hexdump(&imutex, sizeof(imutex));

  Mutex m;

  Thread* threads[5];

  for (auto& thread : threads) {
    thread = Thread::Create("mutex tester", &mutex_thread, &m,
                            Thread::Current::Get()->scheduler_state().base_priority());
    thread->Resume();
  }

  for (auto& thread : threads) {
    thread->Join(NULL, ZX_TIME_INFINITE);
  }

  Thread::Current::SleepRelative(ZX_MSEC(100));

  printf("done with mutex tests\n");

  return 0;
}

static int mutex_inherit_test() {
  printf("running mutex inheritance test\n");

  constexpr uint inherit_test_mutex_count = 4;
  constexpr uint inherit_test_thread_count = 5;

  {  // Explicit scope to control when the destruction of |args| happens
    // working variables to pass the working thread
    struct args {
      Event test_blocker;
      Mutex test_mutex[inherit_test_mutex_count];
    } args;

    // worker thread to stress the priority inheritance mechanism
    auto inherit_worker = [](void* arg) TA_NO_THREAD_SAFETY_ANALYSIS -> int {
      struct args* args = static_cast<struct args*>(arg);

      for (int count = 0; count < 100000; count++) {
        uint r = rand_range(1, inherit_test_mutex_count);

        // pick a random priority
        Thread::Current::Get()->SetPriority(rand_range(DEFAULT_PRIORITY - 4, DEFAULT_PRIORITY + 4));

        // grab a random number of mutexes
        for (uint j = 0; j < r; j++) {
          args->test_mutex[j].Acquire();
        }

        if (count % 1000 == 0)
          printf("%p: count %d\n", Thread::Current::Get(), count);

        // wait on a event for a period of time, to try to have other grabber threads
        // need to tweak our priority in either one of the mutexes we hold or the
        // blocking event
        args->test_blocker.WaitDeadline(current_time() + ZX_USEC(rand() % 10u), Interruptible::Yes);

        // release in reverse order
        for (int j = r - 1; j >= 0; j--) {
          args->test_mutex[j].Release();
        }
      }

      return 0;
    };

    // create a stack of mutexes and a few threads
    Thread* test_thread[inherit_test_thread_count];
    for (auto& t : test_thread) {
      t = Thread::Create("mutex tester", inherit_worker, &args,
                         Thread::Current::Get()->scheduler_state().base_priority());
      t->Resume();
    }

    for (auto& t : test_thread) {
      t->Join(NULL, ZX_TIME_INFINITE);
    }
  }

  Thread::Current::SleepRelative(ZX_MSEC(100));

  printf("done with mutex inheirit test\n");

  return 0;
}

static int event_signaler(void* arg) {
  Event* event = static_cast<Event*>(arg);

  printf("event signaler pausing\n");
  Thread::Current::SleepRelative(ZX_SEC(1));

  //  for (;;) {
  printf("signaling event\n");
  event->Signal();
  printf("done signaling event\n");
  Thread::Current::Yield();
  //  }

  return 0;
}

struct WaiterArgs {
  Event* event;
  size_t count;
};

static int event_waiter(void* arg) {
  // Copy our arguments here so we can mutate the count.
  WaiterArgs args = *static_cast<WaiterArgs*>(arg);

  while (args.count > 0) {
    printf("thread %p: waiting on event...\n", Thread::Current::Get());
    zx_status_t status = args.event->WaitDeadline(ZX_TIME_INFINITE, Interruptible::Yes);
    if (status == ZX_ERR_INTERNAL_INTR_KILLED) {
      printf("thread %p: killed\n", Thread::Current::Get());
      return -1;
    } else if (status != ZX_OK) {
      printf("thread %p: event_wait() returned error %d\n", Thread::Current::Get(), status);
      return -1;
    }
    printf("thread %p: done waiting on event\n", Thread::Current::Get());
    Thread::Current::Yield();
    args.count--;
  }

  return 0;
}

static void event_test() {
  Thread* threads[5];

  printf("event tests starting\n");

  {
    /* make sure signaling the event wakes up all the threads and stays signaled */
    printf(
        "creating event, waiting on it with 4 threads, signaling it and making sure all threads "
        "fall "
        "through twice\n");
    Event event;
    WaiterArgs args{&event, 2};
    threads[0] = Thread::Create("event signaler", &event_signaler, &event, DEFAULT_PRIORITY);
    threads[1] = Thread::Create("event waiter 0", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[2] = Thread::Create("event waiter 1", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[3] = Thread::Create("event waiter 2", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[4] = Thread::Create("event waiter 3", &event_waiter, &args, DEFAULT_PRIORITY);

    for (auto& thread : threads)
      thread->Resume();

    for (auto& thread : threads)
      thread->Join(NULL, ZX_TIME_INFINITE);

    Thread::Current::SleepRelative(ZX_SEC(2));
    printf("destroying event by going out of scope\n");
  }

  {
    AutounsignalEvent event;
    WaiterArgs args{&event, 99};
    /* make sure signaling the event wakes up precisely one thread */
    printf(
        "creating event, waiting on it with 4 threads, signaling it and making sure only one "
        "thread "
        "wakes up\n");
    threads[0] = Thread::Create("event signaler", &event_signaler, &event, DEFAULT_PRIORITY);
    threads[1] = Thread::Create("event waiter 0", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[2] = Thread::Create("event waiter 1", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[3] = Thread::Create("event waiter 2", &event_waiter, &args, DEFAULT_PRIORITY);
    threads[4] = Thread::Create("event waiter 3", &event_waiter, &args, DEFAULT_PRIORITY);

    for (auto& thread : threads)
      thread->Resume();

    Thread::Current::SleepRelative(ZX_SEC(2));

    for (auto& thread : threads) {
      thread->Kill();
      thread->Join(NULL, ZX_TIME_INFINITE);
    }
  }

  printf("event tests done\n");
}

static Event context_switch_event;
static Event context_switch_done_event;

static int context_switch_tester(void* arg) {
  int i;
  uint64_t total_count = 0;
  const int iter = 100000;
  uintptr_t thread_count = (uintptr_t)arg;

  context_switch_event.Wait();

  uint64_t count = arch::Cycles();
  for (i = 0; i < iter; i++) {
    Thread::Current::Yield();
  }
  total_count += arch::Cycles() - count;
  Thread::Current::SleepRelative(ZX_SEC(1));
  printf("took %" PRIu64 " cycles to yield %d times, %" PRIu64 " per yield, %" PRIu64
         " per yield per thread\n",
         total_count, iter, total_count / iter, total_count / iter / thread_count);

  context_switch_done_event.Signal();

  return 0;
}

static void context_switch_test() {
  Thread::Create("context switch idle", &context_switch_tester, (void*)1, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Current::SleepRelative(ZX_MSEC(100));
  context_switch_event.Signal();
  context_switch_done_event.Wait();
  Thread::Current::SleepRelative(ZX_MSEC(100));

  context_switch_event.Unsignal();
  context_switch_done_event.Unsignal();
  Thread::Create("context switch 2a", &context_switch_tester, (void*)2, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Create("context switch 2b", &context_switch_tester, (void*)2, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Current::SleepRelative(ZX_MSEC(100));
  context_switch_event.Signal();
  context_switch_done_event.Wait();
  Thread::Current::SleepRelative(ZX_MSEC(100));

  context_switch_event.Unsignal();
  context_switch_done_event.Unsignal();
  Thread::Create("context switch 4a", &context_switch_tester, (void*)4, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Create("context switch 4b", &context_switch_tester, (void*)4, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Create("context switch 4c", &context_switch_tester, (void*)4, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Create("context switch 4d", &context_switch_tester, (void*)4, DEFAULT_PRIORITY)
      ->DetachAndResume();
  Thread::Current::SleepRelative(ZX_MSEC(100));
  context_switch_event.Signal();
  context_switch_done_event.Wait();
  Thread::Current::SleepRelative(ZX_MSEC(100));
}

static ktl::atomic<int> atomic_var;
static ktl::atomic<int> atomic_count;

static int atomic_tester(void* arg) {
  int add = (int)(uintptr_t)arg;
  int i;

  const int iter = 10000000;

  TRACEF("add %d, %d iterations\n", add, iter);

  for (i = 0; i < iter; i++) {
    atomic_var.fetch_add(add);
  }

  int old = atomic_count.fetch_sub(1);
  TRACEF("exiting, old count %d\n", old);

  return 0;
}

static void atomic_test(void) {
  atomic_var = 0;
  atomic_count = 8;

  printf("testing atomic routines\n");

  Thread* threads[8];
  threads[0] = Thread::Create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[1] = Thread::Create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[2] = Thread::Create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[3] = Thread::Create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[4] = Thread::Create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[5] = Thread::Create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[6] = Thread::Create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[7] = Thread::Create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);

  /* start all the threads */
  for (auto& thread : threads)
    thread->Resume();

  /* wait for them to all stop */
  for (auto& thread : threads) {
    thread->Join(NULL, ZX_TIME_INFINITE);
  }

  printf("atomic count == %d (should be zero)\n", atomic_var.load());
}

static ktl::atomic<int> preempt_count;

static int preempt_tester(void* arg) {
  spin(1000000);

  printf("exiting ts %" PRIi64 " ns\n", current_time());

  preempt_count.fetch_sub(1);

  return 0;
}

static void preempt_test() {
  /* create 5 threads, let them run. If the system is properly timer preempting,
   * the threads should interleave each other at a fine enough granularity so
   * that they complete at roughly the same time. */
  printf("testing preemption\n");

  preempt_count = 5;

  for (int i = 0; i < preempt_count; i++)
    Thread::Create("preempt tester", &preempt_tester, NULL, LOW_PRIORITY)->DetachAndResume();

  while (preempt_count > 0) {
    Thread::Current::SleepRelative(ZX_SEC(1));
  }

  printf("done with preempt test, above time stamps should be very close\n");
}

static int join_tester(void* arg) {
  int val = (int)(uintptr_t)arg;

  printf("\t\tjoin tester starting\n");
  Thread::Current::SleepRelative(ZX_MSEC(500));
  printf("\t\tjoin tester exiting with result %d\n", val);

  return val;
}

static int join_tester_server(void* arg) {
  int ret;
  zx_status_t err;
  Thread* t;

  printf("\ttesting thread_join/thread_detach\n");

  printf("\tcreating and waiting on thread to exit with thread_join\n");
  t = Thread::Create("join tester", &join_tester, (void*)1, DEFAULT_PRIORITY);
  t->Resume();
  ret = 99;
  t->canary().Assert();
  err = t->Join(&ret, ZX_TIME_INFINITE);
  printf("\tthread_join returns err %d, retval %d\n", err, ret);

  printf("\tcreating and waiting on thread to exit with thread_join, after thread has exited\n");
  t = Thread::Create("join tester", &join_tester, (void*)2, DEFAULT_PRIORITY);
  t->Resume();
  Thread::Current::SleepRelative(ZX_SEC(1));  // wait until thread is already dead
  ret = 99;
  t->canary().Assert();
  err = t->Join(&ret, ZX_TIME_INFINITE);
  printf("\tthread_join returns err %d, retval %d\n", err, ret);

  printf("\tcreating a thread, detaching it, let it exit on its own\n");
  t = Thread::Create("join tester", &join_tester, (void*)3, DEFAULT_PRIORITY);
  t->Detach();
  t->Resume();
  Thread::Current::SleepRelative(ZX_SEC(1));  // wait until the thread should be dead

  printf("\tcreating a thread, detaching it after it should be dead\n");
  t = Thread::Create("join tester", &join_tester, (void*)4, DEFAULT_PRIORITY);
  t->Resume();
  Thread::Current::SleepRelative(ZX_SEC(1));  // wait until thread is already dead
  t->canary().Assert();
  t->Detach();

  printf("\texiting join tester server\n");

  return 55;
}

static void join_test() {
  int ret;
  zx_status_t err;
  Thread* t;

  printf("testing thread_join/thread_detach\n");

  printf("creating thread join server thread\n");
  t = Thread::Create("join tester server", &join_tester_server, (void*)1, DEFAULT_PRIORITY);
  t->Resume();
  ret = 99;
  err = t->Join(&ret, ZX_TIME_INFINITE);
  printf("thread_join returns err %d, retval %d (should be 0 and 55)\n", err, ret);
}

struct lock_pair_t {
  SpinLock first;
  SpinLock second;
};

// Acquires lock on "second" and holds it until it sees that "first" is released.
static int hold_and_release(void* arg) {
  lock_pair_t* pair = reinterpret_cast<lock_pair_t*>(arg);
  ASSERT(pair != nullptr);
  interrupt_saved_state_t state;
  pair->second.AcquireIrqSave(state);
  while (pair->first.HolderCpu() != UINT_MAX) {
    arch::Yield();
  }
  pair->second.ReleaseIrqRestore(state);
  return 0;
}

static void spinlock_test() {
  interrupt_saved_state_t state;
  SpinLock lock;

  // Verify basic functionality (single core).
  printf("testing spinlock:\n");
  ASSERT(!lock.IsHeld());
  ASSERT(!arch_ints_disabled());
  lock.AcquireIrqSave(state);
  ASSERT(arch_ints_disabled());
  ASSERT(lock.IsHeld());
  ASSERT(lock.HolderCpu() == arch_curr_cpu_num());
  lock.ReleaseIrqRestore(state);
  ASSERT(!lock.IsHeld());
  ASSERT(!arch_ints_disabled());

  // Verify slightly more advanced functionality that requires multiple cores.
  cpu_mask_t active = mp_get_active_mask();
  if (!active || ispow2(active)) {
    printf("skipping rest of spinlock_test, not enough active cpus\n");
    return;
  }

  // Hold the first lock, then create a thread and wait for it to acquire the lock.
  lock_pair_t pair;
  pair.first.AcquireIrqSave(state);
  Thread* holder_thread =
      Thread::Create("hold_and_release", &hold_and_release, &pair, DEFAULT_PRIORITY);
  ASSERT(holder_thread != nullptr);
  // Right now we have suspended IRQs and so we will not be moved off this cpu. To prevent any
  // poor decisions by the scheduler that could cause deadlock we set the affinity of the
  // holder_thread to not include our cpu.
  holder_thread->SetCpuAffinity(active ^ cpu_num_to_mask(arch_curr_cpu_num()));
  holder_thread->Resume();
  while (pair.second.HolderCpu() == UINT_MAX) {
    arch::Yield();
  }

  // See that from our perspective "second" is not held.
  ASSERT(!pair.second.IsHeld());
  pair.first.ReleaseIrqRestore(state);
  holder_thread->Join(NULL, ZX_TIME_INFINITE);

  printf("seems to work\n");
}

static int sleeper_kill_thread(void* arg) {
  Thread::Current::SleepRelative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = Thread::Current::SleepInterruptible(t + ZX_SEC(5));
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("thread_sleep_interruptible returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static int waiter_kill_thread_infinite_wait(void* arg) {
  Event* e = (Event*)arg;

  Thread::Current::SleepRelative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = e->WaitDeadline(ZX_TIME_INFINITE, Interruptible::Yes);
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("event_wait_deadline returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static int waiter_kill_thread(void* arg) {
  Event* e = (Event*)arg;

  Thread::Current::SleepRelative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = e->WaitDeadline(t + ZX_SEC(5), Interruptible::Yes);
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("event_wait_deadline with deadline returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static void kill_tests() {
  Thread* t;

  printf("starting sleeper thread, then killing it while it sleeps.\n");
  t = Thread::Create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  t->Resume();
  Thread::Current::SleepRelative(ZX_MSEC(200));
  t->Kill();
  t->Join(NULL, ZX_TIME_INFINITE);

  printf("starting sleeper thread, then killing it before it wakes up.\n");
  t = Thread::Create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  t->Resume();
  t->Kill();
  t->Join(NULL, ZX_TIME_INFINITE);

  printf("starting sleeper thread, then killing it before it is unsuspended.\n");
  t = Thread::Create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  t->Kill();  // kill it before it is resumed
  t->Resume();
  t->Join(NULL, ZX_TIME_INFINITE);

  {
    printf("starting waiter thread that waits forever, then killing it while it blocks.\n");
    Event e;
    t = Thread::Create("waiter", waiter_kill_thread_infinite_wait, &e, LOW_PRIORITY);
    t->Resume();
    Thread::Current::SleepRelative(ZX_MSEC(200));
    t->Kill();
    t->Join(NULL, ZX_TIME_INFINITE);
  }

  {
    printf("starting waiter thread that waits forever, then killing it before it wakes up.\n");
    Event e;
    t = Thread::Create("waiter", waiter_kill_thread_infinite_wait, &e, LOW_PRIORITY);
    t->Resume();
    t->Kill();
    t->Join(NULL, ZX_TIME_INFINITE);
  }

  {
    printf("starting waiter thread that waits some time, then killing it while it blocks.\n");
    Event e;
    t = Thread::Create("waiter", waiter_kill_thread, &e, LOW_PRIORITY);
    t->Resume();
    Thread::Current::SleepRelative(ZX_MSEC(200));
    t->Kill();
    t->Join(NULL, ZX_TIME_INFINITE);
  }

  {
    printf("starting waiter thread that waits some time, then killing it before it wakes up.\n");
    Event e;
    t = Thread::Create("waiter", waiter_kill_thread, &e, LOW_PRIORITY);
    t->Resume();
    t->Kill();
    t->Join(NULL, ZX_TIME_INFINITE);
  }
}

struct affinity_test_state {
  Thread* threads[16] = {};
  volatile bool shutdown = false;
};

template <typename T>
static void spin_while(zx_time_t t, T func) {
  zx_time_t start = current_time();

  while ((current_time() - start) < t) {
    func();
  }
}

static cpu_mask_t random_mask(cpu_mask_t active) {
  cpu_mask_t r;
  DEBUG_ASSERT(active != 0);
  // Assuming rand is properly random this should converge in 2 iterations on average.
  do {
    r = rand() % active;
  } while (r == 0);
  return r;
}

static int affinity_test_thread(void* arg) {
  Thread* t = Thread::Current::Get();
  affinity_test_state* state = static_cast<affinity_test_state*>(arg);
  cpu_mask_t active = mp_get_active_mask();

  printf("top of affinity tester %p\n", t);

  while (!state->shutdown) {
    int which = rand() % static_cast<int>(ktl::size(state->threads));
    switch (rand() % 5) {
      case 0:  // set affinity
        // printf("%p set aff %p\n", t, state->threads[which]);
        state->threads[which]->SetCpuAffinity((cpu_mask_t)random_mask(active));
        break;
      case 1:  // sleep for a bit
        // printf("%p sleep\n", t);
        Thread::Current::SleepRelative(ZX_USEC(rand() % 100));
        break;
      case 2:  // spin for a bit
        // printf("%p spin\n", t);
        spin((uint32_t)rand() % 100);
        // printf("%p spin done\n", t);
        break;
      case 3:  // yield
        // printf("%p yield\n", t);
        spin_while(ZX_USEC((uint32_t)rand() % 100), Thread::Current::Yield);
        // printf("%p yield done\n", t);
        break;
      case 4:  // reschedule
        // printf("%p reschedule\n", t);
        spin_while(ZX_USEC((uint32_t)rand() % 100), Thread::Current::Reschedule);
        // printf("%p reschedule done\n", t);
        break;
    }
  }

  printf("affinity tester %p exiting\n", t);

  return 0;
}

// start a bunch of threads that randomly set the affinity of the other threads
// to random masks while doing various work.
// a successful pass is one where it completes the run without tripping over any asserts
// in the scheduler code.
__NO_INLINE static void affinity_test() {
  printf("starting thread affinity test\n");

  cpu_mask_t active = mp_get_active_mask();
  if (!active || ispow2(active)) {
    printf("aborting test, not enough active cpus\n");
    return;
  }

  affinity_test_state state;

  for (auto& t : state.threads) {
    t = Thread::Create("affinity_tester", &affinity_test_thread, &state, LOW_PRIORITY);
  }

  for (auto& t : state.threads) {
    t->Resume();
  }

  static const int duration = 30;
  printf("running tests for %i seconds\n", duration);
  for (int i = 0; i < duration; i++) {
    Thread::Current::SleepRelative(ZX_SEC(1));
    printf("%d sec elapsed\n", i + 1);
  }
  state.shutdown = true;
  Thread::Current::SleepRelative(ZX_SEC(1));

  for (auto& t : state.threads) {
    printf("joining thread %p\n", t);
    t->Join(nullptr, ZX_TIME_INFINITE);
  }

  printf("done with affinity test\n");
}

static int prio_test_thread(void* arg) {
  Thread* volatile t = Thread::Current::Get();
  ASSERT(t->scheduler_state().base_priority() == LOW_PRIORITY);

  auto ev = (Event*)arg;
  ev->SignalNoResched();

  // Busy loop until our priority changes.
  int count = 0;
  for (;;) {
    if (t->scheduler_state().base_priority() == DEFAULT_PRIORITY) {
      break;
    }
    ++count;
  }

  ev->SignalNoResched();

  // And then when it changes again.
  for (;;) {
    if (t->scheduler_state().base_priority() == HIGH_PRIORITY) {
      break;
    }
    ++count;
  }

  return count;
}

__NO_INLINE static void priority_test() {
  printf("starting priority tests\n");

  Thread* t = Thread::Current::Get();
  int base_priority = t->scheduler_state().base_priority();

  if (base_priority != DEFAULT_PRIORITY) {
    printf("unexpected initial state, aborting test\n");
    return;
  }

  t->SetPriority(DEFAULT_PRIORITY + 2);
  Thread::Current::SleepRelative(ZX_MSEC(1));
  ASSERT(t->scheduler_state().base_priority() == DEFAULT_PRIORITY + 2);

  t->SetPriority(DEFAULT_PRIORITY - 2);
  Thread::Current::SleepRelative(ZX_MSEC(1));
  ASSERT(t->scheduler_state().base_priority() == DEFAULT_PRIORITY - 2);

  cpu_mask_t active = mp_get_active_mask();
  if (!active || ispow2(active)) {
    printf("skipping rest, not enough active cpus\n");
    return;
  }

  AutounsignalEvent ev;

  Thread* nt = Thread::Create("prio-test", prio_test_thread, &ev, LOW_PRIORITY);

  cpu_num_t curr = arch_curr_cpu_num();
  cpu_num_t other;
  if (mp_is_cpu_online(curr + 1)) {
    other = curr + 1;
  } else if (mp_is_cpu_online(curr - 1)) {
    other = curr - 1;
  } else {
    ASSERT(false);
  }

  nt->SetCpuAffinity(cpu_num_to_mask(other));
  nt->Resume();

  zx_status_t status = ev.WaitDeadline(ZX_TIME_INFINITE, Interruptible::Yes);
  ASSERT(status == ZX_OK);
  nt->SetPriority(DEFAULT_PRIORITY);

  status = ev.WaitDeadline(ZX_TIME_INFINITE, Interruptible::Yes);
  ASSERT(status == ZX_OK);
  nt->SetPriority(HIGH_PRIORITY);

  int count = 0;
  nt->Join(&count, ZX_TIME_INFINITE);
  printf("%d loops\n", count);

  printf("done with priority tests\n");
}

int thread_tests(int, const cmd_args*, uint32_t) {
  kill_tests();

  mutex_test();
  event_test();
  mutex_inherit_test();

  spinlock_test();
  atomic_test();

  Thread::Current::SleepRelative(ZX_MSEC(200));
  context_switch_test();

  preempt_test();

  join_test();

  affinity_test();

  priority_test();

  return 0;
}

static int spinner_thread(void* arg) {
  for (;;)
    ;

  return 0;
}

int spinner(int argc, const cmd_args* argv, uint32_t) {
  if (argc < 2) {
    printf("not enough args\n");
    printf("usage: %s <priority>\n", argv[0].str);
    return -1;
  }

  Thread* t = Thread::Create("spinner", spinner_thread, NULL, (int)argv[1].u);
  if (!t)
    return ZX_ERR_NO_MEMORY;

  t->DetachAndResume();

  return 0;
}
