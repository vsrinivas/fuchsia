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
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <pow2.h>
#include <rand.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>

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
    printf("sleeper %p\n", get_current_thread());
    thread_sleep_relative(ZX_MSEC(rand() % 500));
  }
  return 0;
}

static int sleep_test(void) {
  int i;
  for (i = 0; i < 16; i++)
    thread_detach_and_resume(thread_create("sleeper", &sleep_thread, NULL, DEFAULT_PRIORITY));
  return 0;
}

static int mutex_thread(void* arg) {
  int i;
  const int iterations = 1000000;
  int count = 0;

  static volatile uintptr_t shared = 0;

  auto m = reinterpret_cast<Mutex*>(arg);

  printf("mutex tester thread %p starting up, will go for %d iterations\n", get_current_thread(),
         iterations);

  for (i = 0; i < iterations; i++) {
    m->Acquire();

    if (shared != 0)
      panic("someone else has messed with the shared data\n");

    shared = (intptr_t)get_current_thread();
    if ((rand() % 5) == 0)
      thread_yield();

    if (++count % 10000 == 0)
      printf("%p: count %d\n", get_current_thread(), count);
    shared = 0;

    m->Release();
    if ((rand() % 5) == 0)
      thread_yield();
  }

  printf("mutex tester %p done\n", get_current_thread());

  return 0;
}

static int mutex_test(void) {
  static Mutex imutex;
  printf("preinitialized mutex:\n");
  hexdump(&imutex, sizeof(imutex));

  Mutex m;

  thread_t* threads[5];

  for (uint i = 0; i < fbl::count_of(threads); i++) {
    threads[i] =
        thread_create("mutex tester", &mutex_thread, &m, get_current_thread()->base_priority);
    thread_resume(threads[i]);
  }

  for (uint i = 0; i < fbl::count_of(threads); i++) {
    thread_join(threads[i], NULL, ZX_TIME_INFINITE);
  }

  thread_sleep_relative(ZX_MSEC(100));

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
      event_t test_blocker = EVENT_INITIAL_VALUE(test_blocker, false, 0);
      Mutex test_mutex[inherit_test_mutex_count];
    } args;

    // worker thread to stress the priority inheritance mechanism
    auto inherit_worker = [](void* arg) TA_NO_THREAD_SAFETY_ANALYSIS -> int {
      struct args* args = static_cast<struct args*>(arg);

      for (int count = 0; count < 100000; count++) {
        uint r = rand_range(1, inherit_test_mutex_count);

        // pick a random priority
        thread_set_priority(get_current_thread(),
                            rand_range(DEFAULT_PRIORITY - 4, DEFAULT_PRIORITY + 4));

        // grab a random number of mutexes
        for (uint j = 0; j < r; j++) {
          args->test_mutex[j].Acquire();
        }

        if (count % 1000 == 0)
          printf("%p: count %d\n", get_current_thread(), count);

        // wait on a event for a period of time, to try to have other grabber threads
        // need to tweak our priority in either one of the mutexes we hold or the
        // blocking event
        event_wait_deadline(&args->test_blocker, current_time() + ZX_USEC(rand() % 10u), true);

        // release in reverse order
        for (int j = r - 1; j >= 0; j--) {
          args->test_mutex[j].Release();
        }
      }

      return 0;
    };

    // create a stack of mutexes and a few threads
    thread_t* test_thread[inherit_test_thread_count];
    for (auto& t : test_thread) {
      t = thread_create("mutex tester", inherit_worker, &args, get_current_thread()->base_priority);
      thread_resume(t);
    }

    for (auto& t : test_thread) {
      thread_join(t, NULL, ZX_TIME_INFINITE);
    }
  }

  thread_sleep_relative(ZX_MSEC(100));

  printf("done with mutex inheirit test\n");

  return 0;
}

static event_t e;

static int event_signaler(void* arg) {
  printf("event signaler pausing\n");
  thread_sleep_relative(ZX_SEC(1));

  //  for (;;) {
  printf("signaling event\n");
  event_signal(&e, true);
  printf("done signaling event\n");
  thread_yield();
  //  }

  return 0;
}

static int event_waiter(void* arg) {
  uintptr_t count = (uintptr_t)arg;

  while (count > 0) {
    printf("thread %p: waiting on event...\n", get_current_thread());
    zx_status_t status = event_wait_deadline(&e, ZX_TIME_INFINITE, true);
    if (status == ZX_ERR_INTERNAL_INTR_KILLED) {
      printf("thread %p: killed\n", get_current_thread());
      return -1;
    } else if (status != ZX_OK) {
      printf("thread %p: event_wait() returned error %d\n", get_current_thread(), status);
      return -1;
    }
    printf("thread %p: done waiting on event\n", get_current_thread());
    thread_yield();
    count--;
  }

  return 0;
}

static void event_test(void) {
  thread_t* threads[5];

  static event_t ievent = EVENT_INITIAL_VALUE(ievent, true, 0x1234);
  printf("preinitialized event:\n");
  hexdump(&ievent, sizeof(ievent));

  printf("event tests starting\n");

  /* make sure signaling the event wakes up all the threads and stays signaled */
  printf(
      "creating event, waiting on it with 4 threads, signaling it and making sure all threads fall "
      "through twice\n");
  event_init(&e, false, 0);
  threads[0] = thread_create("event signaler", &event_signaler, NULL, DEFAULT_PRIORITY);
  threads[1] = thread_create("event waiter 0", &event_waiter, (void*)2, DEFAULT_PRIORITY);
  threads[2] = thread_create("event waiter 1", &event_waiter, (void*)2, DEFAULT_PRIORITY);
  threads[3] = thread_create("event waiter 2", &event_waiter, (void*)2, DEFAULT_PRIORITY);
  threads[4] = thread_create("event waiter 3", &event_waiter, (void*)2, DEFAULT_PRIORITY);

  for (uint i = 0; i < fbl::count_of(threads); i++)
    thread_resume(threads[i]);

  for (uint i = 0; i < fbl::count_of(threads); i++)
    thread_join(threads[i], NULL, ZX_TIME_INFINITE);

  thread_sleep_relative(ZX_SEC(2));
  printf("destroying event\n");
  event_destroy(&e);

  /* make sure signaling the event wakes up precisely one thread */
  printf(
      "creating event, waiting on it with 4 threads, signaling it and making sure only one thread "
      "wakes up\n");
  event_init(&e, false, EVENT_FLAG_AUTOUNSIGNAL);
  threads[0] = thread_create("event signaler", &event_signaler, NULL, DEFAULT_PRIORITY);
  threads[1] = thread_create("event waiter 0", &event_waiter, (void*)99, DEFAULT_PRIORITY);
  threads[2] = thread_create("event waiter 1", &event_waiter, (void*)99, DEFAULT_PRIORITY);
  threads[3] = thread_create("event waiter 2", &event_waiter, (void*)99, DEFAULT_PRIORITY);
  threads[4] = thread_create("event waiter 3", &event_waiter, (void*)99, DEFAULT_PRIORITY);

  for (uint i = 0; i < fbl::count_of(threads); i++)
    thread_resume(threads[i]);

  thread_sleep_relative(ZX_SEC(2));

  for (uint i = 0; i < fbl::count_of(threads); i++) {
    thread_kill(threads[i]);
    thread_join(threads[i], NULL, ZX_TIME_INFINITE);
  }

  event_destroy(&e);

  printf("event tests done\n");
}

static int quantum_tester(void* arg) {
  for (;;) {
    printf("%p: in this thread. rq %" PRIi64 "\n", get_current_thread(),
           get_current_thread()->remaining_time_slice);
  }
  return 0;
}

static void quantum_test(void) {
  thread_detach_and_resume(
      thread_create("quantum tester 0", &quantum_tester, NULL, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("quantum tester 1", &quantum_tester, NULL, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("quantum tester 2", &quantum_tester, NULL, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("quantum tester 3", &quantum_tester, NULL, DEFAULT_PRIORITY));
}

static event_t context_switch_event;
static event_t context_switch_done_event;

static int context_switch_tester(void* arg) {
  int i;
  uint64_t total_count = 0;
  const int iter = 100000;
  uintptr_t thread_count = (uintptr_t)arg;

  event_wait(&context_switch_event);

  uint64_t count = arch_cycle_count();
  for (i = 0; i < iter; i++) {
    thread_yield();
  }
  total_count += arch_cycle_count() - count;
  thread_sleep_relative(ZX_SEC(1));
  printf("took %" PRIu64 " cycles to yield %d times, %" PRIu64 " per yield, %" PRIu64
         " per yield per thread\n",
         total_count, iter, total_count / iter, total_count / iter / thread_count);

  event_signal(&context_switch_done_event, true);

  return 0;
}

static void context_switch_test(void) {
  event_init(&context_switch_event, false, 0);
  event_init(&context_switch_done_event, false, 0);

  thread_detach_and_resume(
      thread_create("context switch idle", &context_switch_tester, (void*)1, DEFAULT_PRIORITY));
  thread_sleep_relative(ZX_MSEC(100));
  event_signal(&context_switch_event, true);
  event_wait(&context_switch_done_event);
  thread_sleep_relative(ZX_MSEC(100));

  event_unsignal(&context_switch_event);
  event_unsignal(&context_switch_done_event);
  thread_detach_and_resume(
      thread_create("context switch 2a", &context_switch_tester, (void*)2, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("context switch 2b", &context_switch_tester, (void*)2, DEFAULT_PRIORITY));
  thread_sleep_relative(ZX_MSEC(100));
  event_signal(&context_switch_event, true);
  event_wait(&context_switch_done_event);
  thread_sleep_relative(ZX_MSEC(100));

  event_unsignal(&context_switch_event);
  event_unsignal(&context_switch_done_event);
  thread_detach_and_resume(
      thread_create("context switch 4a", &context_switch_tester, (void*)4, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("context switch 4b", &context_switch_tester, (void*)4, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("context switch 4c", &context_switch_tester, (void*)4, DEFAULT_PRIORITY));
  thread_detach_and_resume(
      thread_create("context switch 4d", &context_switch_tester, (void*)4, DEFAULT_PRIORITY));
  thread_sleep_relative(ZX_MSEC(100));
  event_signal(&context_switch_event, true);
  event_wait(&context_switch_done_event);
  thread_sleep_relative(ZX_MSEC(100));
}

static volatile int atomic;
static volatile int atomic_count;

static int atomic_tester(void* arg) {
  int add = (int)(uintptr_t)arg;
  int i;

  const int iter = 10000000;

  TRACEF("add %d, %d iterations\n", add, iter);

  for (i = 0; i < iter; i++) {
    atomic_add(&atomic, add);
  }

  int old = atomic_add(&atomic_count, -1);
  TRACEF("exiting, old count %d\n", old);

  return 0;
}

static void atomic_test(void) {
  atomic = 0;
  atomic_count = 8;

  printf("testing atomic routines\n");

  thread_t* threads[8];
  threads[0] = thread_create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[1] = thread_create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[2] = thread_create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[3] = thread_create("atomic tester 1", &atomic_tester, (void*)1, LOW_PRIORITY);
  threads[4] = thread_create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[5] = thread_create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[6] = thread_create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);
  threads[7] = thread_create("atomic tester 2", &atomic_tester, (void*)-1, LOW_PRIORITY);

  /* start all the threads */
  for (uint i = 0; i < fbl::count_of(threads); i++)
    thread_resume(threads[i]);

  /* wait for them to all stop */
  for (uint i = 0; i < fbl::count_of(threads); i++) {
    thread_join(threads[i], NULL, ZX_TIME_INFINITE);
  }

  printf("atomic count == %d (should be zero)\n", atomic);
}

static volatile int preempt_count;

static int preempt_tester(void* arg) {
  spin(1000000);

  printf("exiting ts %" PRIi64 " ns\n", current_time());

  atomic_add(&preempt_count, -1);

  return 0;
}

static void preempt_test(void) {
  /* create 5 threads, let them run. If the system is properly timer preempting,
   * the threads should interleave each other at a fine enough granularity so
   * that they complete at roughly the same time. */
  printf("testing preemption\n");

  preempt_count = 5;

  for (int i = 0; i < preempt_count; i++)
    thread_detach_and_resume(thread_create("preempt tester", &preempt_tester, NULL, LOW_PRIORITY));

  while (preempt_count > 0) {
    thread_sleep_relative(ZX_SEC(1));
  }

  printf("done with preempt test, above time stamps should be very close\n");

  /* do the same as above, but mark the threads as real time, which should
   * effectively disable timer based preemption for them. They should
   * complete in order, about a second apart. */
  printf("testing real time preemption\n");

  const int num_threads = 5;
  preempt_count = num_threads;

  for (int i = 0; i < num_threads; i++) {
    thread_t* t = thread_create("preempt tester", &preempt_tester, NULL, LOW_PRIORITY);
    thread_set_real_time(t);
    thread_set_cpu_affinity(t, cpu_num_to_mask(0));
    thread_detach_and_resume(t);
  }

  while (preempt_count > 0) {
    thread_sleep_relative(ZX_SEC(1));
  }

  printf("done with real-time preempt test, above time stamps should be 1 second apart\n");
}

static int join_tester(void* arg) {
  int val = (int)(uintptr_t)arg;

  printf("\t\tjoin tester starting\n");
  thread_sleep_relative(ZX_MSEC(500));
  printf("\t\tjoin tester exiting with result %d\n", val);

  return val;
}

static int join_tester_server(void* arg) {
  int ret;
  zx_status_t err;
  thread_t* t;

  printf("\ttesting thread_join/thread_detach\n");

  printf("\tcreating and waiting on thread to exit with thread_join\n");
  t = thread_create("join tester", &join_tester, (void*)1, DEFAULT_PRIORITY);
  thread_resume(t);
  ret = 99;
  printf("\tthread magic is 0x%x (should be 0x%x)\n", (unsigned)t->magic, (unsigned)THREAD_MAGIC);
  err = thread_join(t, &ret, ZX_TIME_INFINITE);
  printf("\tthread_join returns err %d, retval %d\n", err, ret);
  printf("\tthread magic is 0x%x (should be 0)\n", (unsigned)t->magic);

  printf("\tcreating and waiting on thread to exit with thread_join, after thread has exited\n");
  t = thread_create("join tester", &join_tester, (void*)2, DEFAULT_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_SEC(1));  // wait until thread is already dead
  ret = 99;
  printf("\tthread magic is 0x%x (should be 0x%x)\n", (unsigned)t->magic, (unsigned)THREAD_MAGIC);
  err = thread_join(t, &ret, ZX_TIME_INFINITE);
  printf("\tthread_join returns err %d, retval %d\n", err, ret);
  printf("\tthread magic is 0x%x (should be 0)\n", (unsigned)t->magic);

  printf("\tcreating a thread, detaching it, let it exit on its own\n");
  t = thread_create("join tester", &join_tester, (void*)3, DEFAULT_PRIORITY);
  thread_detach(t);
  thread_resume(t);
  thread_sleep_relative(ZX_SEC(1));  // wait until the thread should be dead
  printf("\tthread magic is 0x%x (should be 0)\n", (unsigned)t->magic);

  printf("\tcreating a thread, detaching it after it should be dead\n");
  t = thread_create("join tester", &join_tester, (void*)4, DEFAULT_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_SEC(1));  // wait until thread is already dead
  printf("\tthread magic is 0x%x (should be 0x%x)\n", (unsigned)t->magic, (unsigned)THREAD_MAGIC);
  thread_detach(t);
  printf("\tthread magic is 0x%x\n", (unsigned)t->magic);

  printf("\texiting join tester server\n");

  return 55;
}

static void join_test(void) {
  int ret;
  zx_status_t err;
  thread_t* t;

  printf("testing thread_join/thread_detach\n");

  printf("creating thread join server thread\n");
  t = thread_create("join tester server", &join_tester_server, (void*)1, DEFAULT_PRIORITY);
  thread_resume(t);
  ret = 99;
  err = thread_join(t, &ret, ZX_TIME_INFINITE);
  printf("thread_join returns err %d, retval %d (should be 0 and 55)\n", err, ret);
}

struct lock_pair_t {
  spin_lock_t first = SPIN_LOCK_INITIAL_VALUE;
  spin_lock_t second = SPIN_LOCK_INITIAL_VALUE;
};

// Acquires lock on "second" and holds it until it sees that "first" is released.
static int hold_and_release(void* arg) {
  lock_pair_t* pair = reinterpret_cast<lock_pair_t*>(arg);
  ASSERT(pair != nullptr);
  spin_lock_saved_state_t state;
  spin_lock_irqsave(&pair->second, state);
  while (spin_lock_holder_cpu(&pair->first) != UINT_MAX) {
    arch_spinloop_pause();
  }
  spin_unlock_irqrestore(&pair->second, state);
  return 0;
}

static void spinlock_test(void) {
  spin_lock_saved_state_t state;
  spin_lock_t lock;

  spin_lock_init(&lock);

  // Verify basic functionality (single core).
  printf("testing spinlock:\n");
  ASSERT(!spin_lock_held(&lock));
  ASSERT(!arch_ints_disabled());
  spin_lock_irqsave(&lock, state);
  ASSERT(arch_ints_disabled());
  ASSERT(spin_lock_held(&lock));
  ASSERT(spin_lock_holder_cpu(&lock) == arch_curr_cpu_num());
  spin_unlock_irqrestore(&lock, state);
  ASSERT(!spin_lock_held(&lock));
  ASSERT(!arch_ints_disabled());

  // Verify slightly more advanced functionality that requires multiple cores.
  cpu_mask_t active = mp_get_active_mask();
  if (!active || ispow2(active)) {
    printf("skipping rest of spinlock_test, not enough active cpus\n");
    return;
  }

  // Hold the first lock, then create a thread and wait for it to acquire the lock.
  lock_pair_t pair;
  spin_lock_irqsave(&pair.first, state);
  thread_t* holder_thread =
      thread_create("hold_and_release", &hold_and_release, &pair, DEFAULT_PRIORITY);
  ASSERT(holder_thread != nullptr);
  // Right now we have suspended IRQs and so we will not be moved off this cpu. To prevent any
  // poor decisions by the scheduler that could cause deadlock we set the affinity of the
  // holder_thread to not include our cpu.
  thread_set_cpu_affinity(holder_thread, active ^ cpu_num_to_mask(arch_curr_cpu_num()));
  thread_resume(holder_thread);
  while (spin_lock_holder_cpu(&pair.second) == UINT_MAX) {
    arch_spinloop_pause();
  }

  // See that from our perspective "second" is not held.
  ASSERT(!spin_lock_held(&pair.second));
  spin_unlock_irqrestore(&pair.first, state);
  thread_join(holder_thread, NULL, ZX_TIME_INFINITE);

  printf("seems to work\n");
}

static int sleeper_kill_thread(void* arg) {
  thread_sleep_relative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = thread_sleep_interruptable(t + ZX_SEC(5));
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("thread_sleep_interruptable returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static int waiter_kill_thread_infinite_wait(void* arg) {
  event_t* e = (event_t*)arg;

  thread_sleep_relative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = event_wait_deadline(e, ZX_TIME_INFINITE, true);
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("event_wait_deadline returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static int waiter_kill_thread(void* arg) {
  event_t* e = (event_t*)arg;

  thread_sleep_relative(ZX_MSEC(100));

  zx_time_t t = current_time();
  zx_status_t err = event_wait_deadline(e, t + ZX_SEC(5), true);
  zx_duration_t duration = (current_time() - t) / ZX_MSEC(1);
  TRACEF("event_wait_deadline with deadline returns %d after %" PRIi64 " msecs\n", err, duration);

  return 0;
}

static void kill_tests(void) {
  thread_t* t;

  printf("starting sleeper thread, then killing it while it sleeps.\n");
  t = thread_create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_MSEC(200));
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);

  printf("starting sleeper thread, then killing it before it wakes up.\n");
  t = thread_create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  thread_resume(t);
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);

  printf("starting sleeper thread, then killing it before it is unsuspended.\n");
  t = thread_create("sleeper", sleeper_kill_thread, 0, LOW_PRIORITY);
  thread_kill(t);  // kill it before it is resumed
  thread_resume(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);

  event_t e;

  printf("starting waiter thread that waits forever, then killing it while it blocks.\n");
  event_init(&e, false, 0);
  t = thread_create("waiter", waiter_kill_thread_infinite_wait, &e, LOW_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_MSEC(200));
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);
  event_destroy(&e);

  printf("starting waiter thread that waits forever, then killing it before it wakes up.\n");
  event_init(&e, false, 0);
  t = thread_create("waiter", waiter_kill_thread_infinite_wait, &e, LOW_PRIORITY);
  thread_resume(t);
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);
  event_destroy(&e);

  printf("starting waiter thread that waits some time, then killing it while it blocks.\n");
  event_init(&e, false, 0);
  t = thread_create("waiter", waiter_kill_thread, &e, LOW_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_MSEC(200));
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);
  event_destroy(&e);

  printf("starting waiter thread that waits some time, then killing it before it wakes up.\n");
  event_init(&e, false, 0);
  t = thread_create("waiter", waiter_kill_thread, &e, LOW_PRIORITY);
  thread_resume(t);
  thread_kill(t);
  thread_join(t, NULL, ZX_TIME_INFINITE);
  event_destroy(&e);
}

struct affinity_test_state {
  thread_t* threads[16] = {};
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
  thread_t* t = get_current_thread();
  affinity_test_state* state = static_cast<affinity_test_state*>(arg);
  cpu_mask_t active = mp_get_active_mask();

  printf("top of affinity tester %p\n", t);

  while (!state->shutdown) {
    int which = rand() % static_cast<int>(fbl::count_of(state->threads));
    switch (rand() % 5) {
      case 0:  // set affinity
        // printf("%p set aff %p\n", t, state->threads[which]);
        thread_set_cpu_affinity(state->threads[which], (cpu_mask_t)random_mask(active));
        break;
      case 1:  // sleep for a bit
        // printf("%p sleep\n", t);
        thread_sleep_relative(ZX_USEC(rand() % 100));
        break;
      case 2:  // spin for a bit
        // printf("%p spin\n", t);
        spin((uint32_t)rand() % 100);
        // printf("%p spin done\n", t);
        break;
      case 3:  // yield
        // printf("%p yield\n", t);
        spin_while(ZX_USEC((uint32_t)rand() % 100), thread_yield);
        // printf("%p yield done\n", t);
        break;
      case 4:  // reschedule
        // printf("%p reschedule\n", t);
        spin_while(ZX_USEC((uint32_t)rand() % 100), thread_reschedule);
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
    t = thread_create("affinity_tester", &affinity_test_thread, &state, LOW_PRIORITY);
  }

  for (auto& t : state.threads) {
    thread_resume(t);
  }

  static const int duration = 30;
  printf("running tests for %i seconds\n", duration);
  for (int i = 0; i < duration; i++) {
    thread_sleep_relative(ZX_SEC(1));
    printf("%d sec elapsed\n", i + 1);
  }
  state.shutdown = true;
  thread_sleep_relative(ZX_SEC(1));

  for (auto& t : state.threads) {
    printf("joining thread %p\n", t);
    thread_join(t, nullptr, ZX_TIME_INFINITE);
  }

  printf("done with affinity test\n");
}

#define TLS_TEST_TAGV ((void*)0x666)

static void tls_test_callback(void* tls) {
  ASSERT(tls == TLS_TEST_TAGV);
  atomic_add(&atomic_count, 1);
}

static int tls_test_thread(void* arg) {
  tls_set(0u, TLS_TEST_TAGV);
  tls_set_callback(0u, &tls_test_callback);
  tls_set(1u, TLS_TEST_TAGV);
  tls_set_callback(1u, &tls_test_callback);
  return 0;
}

static void tls_tests() {
  printf("starting tls tests\n");
  atomic_count = 0;

  thread_t* t = thread_create("tls-test", tls_test_thread, 0, LOW_PRIORITY);
  thread_resume(t);
  thread_sleep_relative(ZX_MSEC(200));
  thread_join(t, nullptr, ZX_TIME_INFINITE);

  ASSERT(atomic_count == 2);
  atomic_count = 0;

  printf("done with tls tests\n");
}

static int prio_test_thread(void* arg) {
  thread_t* t = get_current_thread();
  ASSERT(t->base_priority == LOW_PRIORITY);

  auto ev = (event_t*)arg;
  event_signal(ev, false);

  // Busy loop until our priority changes.
  volatile int* v_pri = &t->base_priority;
  int count = 0;
  for (;;) {
    if (*v_pri == DEFAULT_PRIORITY) {
      break;
    }
    ++count;
  }

  event_signal(ev, false);

  // And then when it changes again.
  for (;;) {
    if (*v_pri == HIGH_PRIORITY) {
      break;
    }
    ++count;
  }

  return count;
}

__NO_INLINE static void priority_test() {
  printf("starting priority tests\n");

  thread_t* t = get_current_thread();
  int base_priority = t->base_priority;

  if (base_priority != DEFAULT_PRIORITY) {
    printf("unexpected initial state, aborting test\n");
    return;
  }

  thread_set_priority(t, DEFAULT_PRIORITY + 2);
  thread_sleep_relative(ZX_MSEC(1));
  ASSERT(t->base_priority == DEFAULT_PRIORITY + 2);

  thread_set_priority(t, DEFAULT_PRIORITY - 2);
  thread_sleep_relative(ZX_MSEC(1));
  ASSERT(t->base_priority == DEFAULT_PRIORITY - 2);

  cpu_mask_t active = mp_get_active_mask();
  if (!active || ispow2(active)) {
    printf("skipping rest, not enough active cpus\n");
    return;
  }

  event_t ev;
  event_init(&ev, false, EVENT_FLAG_AUTOUNSIGNAL);

  thread_t* nt = thread_create("prio-test", prio_test_thread, &ev, LOW_PRIORITY);

  cpu_num_t curr = arch_curr_cpu_num();
  cpu_num_t other;
  if (mp_is_cpu_online(curr + 1)) {
    other = curr + 1;
  } else if (mp_is_cpu_online(curr - 1)) {
    other = curr - 1;
  } else {
    ASSERT(false);
  }

  thread_set_cpu_affinity(nt, cpu_num_to_mask(other));
  thread_resume(nt);

  zx_status_t status = event_wait_deadline(&ev, ZX_TIME_INFINITE, true);
  ASSERT(status == ZX_OK);
  thread_set_priority(nt, DEFAULT_PRIORITY);

  status = event_wait_deadline(&ev, ZX_TIME_INFINITE, true);
  ASSERT(status == ZX_OK);
  thread_set_priority(nt, HIGH_PRIORITY);

  int count = 0;
  thread_join(nt, &count, ZX_TIME_INFINITE);
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

  thread_sleep_relative(ZX_MSEC(200));
  context_switch_test();

  preempt_test();

  join_test();

  affinity_test();

  tls_tests();

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
    printf("usage: %s <priority> <rt>\n", argv[0].str);
    return -1;
  }

  thread_t* t = thread_create("spinner", spinner_thread, NULL, (int)argv[1].u);
  if (!t)
    return ZX_ERR_NO_MEMORY;

  if (argc >= 3 && !strcmp(argv[2].str, "rt")) {
    thread_set_real_time(t);
  }
  thread_detach_and_resume(t);

  return 0;
}
