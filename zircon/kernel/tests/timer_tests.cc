// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/unittest/unittest.h>
#include <malloc.h>
#include <platform.h>
#include <pow2.h>
#include <rand.h>
#include <stdio.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>

#include "tests.h"

static void timer_diag_cb(timer_t* timer, zx_time_t now, void* arg) {
  event_t* event = (event_t*)arg;
  event_signal(event, true);
}

static int timer_do_one_thread(void* arg) {
  event_t event;
  timer_t timer;

  event_init(&event, false, 0);
  timer_init(&timer);

  const Deadline deadline = Deadline::no_slack(current_time() + ZX_MSEC(10));
  timer_set(&timer, deadline, timer_diag_cb, &event);
  event_wait(&event);

  printf("got timer on cpu %u\n", arch_curr_cpu_num());

  event_destroy(&event);

  return 0;
}

static void timer_diag_all_cpus(void) {
  thread_t* timer_threads[SMP_MAX_CPUS];
  uint max = arch_max_num_cpus();

  uint i;
  for (i = 0; i < max; i++) {
    char name[16];
    snprintf(name, sizeof(name), "timer %u\n", i);

    timer_threads[i] =
        thread_create_etc(NULL, name, timer_do_one_thread, NULL, DEFAULT_PRIORITY, NULL);
    DEBUG_ASSERT_MSG(timer_threads[i] != NULL, "failed to create thread for cpu %u\n", i);
    thread_set_cpu_affinity(timer_threads[i], cpu_num_to_mask(i));
    thread_resume(timer_threads[i]);
  }
  for (i = 0; i < max; i++) {
    zx_status_t status = thread_join(timer_threads[i], NULL, ZX_TIME_INFINITE);
    DEBUG_ASSERT_MSG(status == ZX_OK, "failed to join thread for cpu %u: %d\n", i, status);
  }
}

static void timer_diag_cb2(timer_t* timer, zx_time_t now, void* arg) {
  auto timer_count = static_cast<ktl::atomic<size_t>*>(arg);
  timer_count->fetch_add(1);
  thread_preempt_set_pending();
}

static void timer_diag_coalescing(TimerSlack slack, const zx_time_t* deadline,
                                  const zx_duration_t* expected_adj, size_t count) {
  printf("testing coalsecing mode %u\n", slack.mode());

  ktl::atomic<size_t> timer_count(0);

  timer_t* timer = (timer_t*)malloc(sizeof(timer_t) * count);

  printf("       orig         new       adjustment\n");
  for (size_t ix = 0; ix != count; ++ix) {
    timer_init(&timer[ix]);
    const Deadline dl(deadline[ix], slack);
    timer_set(&timer[ix], dl, timer_diag_cb2, &timer_count);
    printf("[%zu] %" PRIi64 "  -> %" PRIi64 ", %" PRIi64 "\n", ix, dl.when(),
           timer[ix].scheduled_time, timer[ix].slack);

    if (timer[ix].slack != expected_adj[ix]) {
      printf("\n!! unexpected adjustment! expected %" PRIi64 "\n", expected_adj[ix]);
    }
  }

  // Wait for the timers to fire.
  while (timer_count.load() != count) {
    thread_sleep(current_time() + ZX_MSEC(5));
  }

  free(timer);
}

static void timer_diag_coalescing_center(void) {
  zx_time_t when = current_time() + ZX_MSEC(1);
  zx_duration_t off = ZX_USEC(10);
  TimerSlack slack = {2u * off, TIMER_SLACK_CENTER};

  const zx_time_t deadline[] = {
      when + (6u * off),  // non-coalesced, adjustment = 0
      when,               // non-coalesced, adjustment = 0
      when - off,         // coalesced with [1], adjustment = 10u
      when - (3u * off),  // non-coalesced, adjustment = 0
      when + off,         // coalesced with [1], adjustment = -10u
      when + (3u * off),  // non-coalesced, adjustment = 0
      when + (5u * off),  // coalesced with [0], adjustment = 10u
      when - (3u * off),  // non-coalesced, same as [3], adjustment = 0
  };

  const zx_duration_t expected_adj[fbl::count_of(deadline)] = {
      0, 0, ZX_USEC(10), 0, -ZX_USEC(10), 0, ZX_USEC(10), 0};

  timer_diag_coalescing(slack, deadline, expected_adj, fbl::count_of(deadline));
}

static void timer_diag_coalescing_late(void) {
  zx_time_t when = current_time() + ZX_MSEC(1);
  zx_duration_t off = ZX_USEC(10);
  TimerSlack slack = {3u * off, TIMER_SLACK_LATE};

  const zx_time_t deadline[] = {
      when + off,         // non-coalesced, adjustment = 0
      when + (2u * off),  // non-coalesced, adjustment = 0
      when - off,         // coalesced with [0], adjustment = 20u
      when - (3u * off),  // non-coalesced, adjustment = 0
      when + (3u * off),  // non-coalesced, adjustment = 0
      when + (2u * off),  // non-coalesced, same as [1]
      when - (4u * off),  // coalesced with [3], adjustment = 10u
  };

  const zx_duration_t expected_adj[fbl::count_of(deadline)] = {0, 0, ZX_USEC(20), 0,
                                                               0, 0, ZX_USEC(10)};

  timer_diag_coalescing(slack, deadline, expected_adj, fbl::count_of(deadline));
}

static void timer_diag_coalescing_early(void) {
  zx_time_t when = current_time() + ZX_MSEC(1);
  zx_duration_t off = ZX_USEC(10);
  TimerSlack slack = {3u * off, TIMER_SLACK_EARLY};

  const zx_time_t deadline[] = {
      when,               // non-coalesced, adjustment = 0
      when + (2u * off),  // coalesced with [0], adjustment = -20u
      when - off,         // non-coalesced, adjustment = 0
      when - (3u * off),  // non-coalesced, adjustment = 0
      when + (4u * off),  // non-coalesced, adjustment = 0
      when + (5u * off),  // coalesced with [4], adjustment = -10u
      when - (2u * off),  // coalesced with [3], adjustment = -10u
  };

  const zx_duration_t expected_adj[fbl::count_of(deadline)] = {0, -ZX_USEC(20), 0,           0,
                                                               0, -ZX_USEC(10), -ZX_USEC(10)};

  timer_diag_coalescing(slack, deadline, expected_adj, fbl::count_of(deadline));
}

static void timer_far_deadline(void) {
  event_t event;
  timer_t timer;

  event_init(&event, false, 0);
  timer_init(&timer);

  const Deadline deadline = Deadline::no_slack(ZX_TIME_INFINITE - 5);
  timer_set(&timer, deadline, timer_diag_cb, &event);
  zx_status_t st = event_wait_deadline(&event, current_time() + ZX_MSEC(100), false);
  if (st != ZX_ERR_TIMED_OUT) {
    printf("error: unexpected timer fired!\n");
  } else {
    timer_cancel(&timer);
  }

  event_destroy(&event);
}

// Print timer diagnostics for manual review.
int timer_diag(int, const cmd_args*, uint32_t) {
  timer_diag_coalescing_center();
  timer_diag_coalescing_late();
  timer_diag_coalescing_early();
  timer_diag_all_cpus();
  timer_far_deadline();
  return 0;
}

struct timer_stress_args {
  volatile int timer_stress_done;
  volatile uint64_t num_set;
  volatile uint64_t num_fired;
};

static void timer_stress_cb(struct timer* t, zx_time_t now, void* void_arg) {
  timer_stress_args* args = reinterpret_cast<timer_stress_args*>(void_arg);
  atomic_add_u64(&args->num_fired, 1);
}

// Returns a random duration between 0 and max (inclusive).
static zx_duration_t rand_duration(zx_duration_t max) {
  return (zx_duration_mul_int64(max, rand())) / RAND_MAX;
}

static int timer_stress_worker(void* void_arg) {
  timer_stress_args* args = reinterpret_cast<timer_stress_args*>(void_arg);
  while (!atomic_load(&args->timer_stress_done)) {
    timer_t t = TIMER_INITIAL_VALUE(t);
    zx_duration_t timer_duration = rand_duration(ZX_MSEC(5));

    // Set a timer, then switch to a different CPU to ensure we race with it.

    arch_disable_ints();
    uint timer_cpu = arch_curr_cpu_num();
    const Deadline deadline = Deadline::no_slack(current_time() + timer_duration);
    timer_set(&t, deadline, timer_stress_cb, void_arg);
    thread_set_cpu_affinity(get_current_thread(), ~cpu_num_to_mask(timer_cpu));
    DEBUG_ASSERT(arch_curr_cpu_num() != timer_cpu);
    arch_enable_ints();

    // We're now running on something other than timer_cpu.

    atomic_add_u64(&args->num_set, 1);

    // Sleep for the timer duration so that this thread's timer_cancel races with the timer
    // callback. We want to race to ensure there are no synchronization or memory visibility
    // issues.
    thread_sleep_relative(timer_duration);
    timer_cancel(&t);
  }
  return 0;
}

static unsigned get_num_cpus_online() {
  unsigned count = 0;
  cpu_mask_t online = mp_get_online_mask();
  while (online) {
    online >>= 1;
    ++count;
  }
  return count;
}

// timer_stress is a simple stress test intended to flush out bugs in kernel timers.
int timer_stress(int argc, const cmd_args* argv, uint32_t) {
  if (argc < 2) {
    printf("not enough args\n");
    printf("usage: %s <num seconds>\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  // We need 2 or more CPUs for this test.
  if (get_num_cpus_online() < 2) {
    printf("not enough online cpus\n");
    return ZX_ERR_INTERNAL;
  }

  timer_stress_args args{};

  thread_t* threads[256];
  for (auto& thread : threads) {
    thread = thread_create("timer-stress-worker", &timer_stress_worker, &args, DEFAULT_PRIORITY);
  }

  printf("running for %zu seconds\n", argv[1].u);
  for (const auto& thread : threads) {
    thread_resume(thread);
  }

  thread_sleep_relative(ZX_SEC(argv[1].u));
  atomic_store(&args.timer_stress_done, 1);

  for (const auto& thread : threads) {
    thread_join(thread, nullptr, ZX_TIME_INFINITE);
  }

  printf("timer stress done; timer set %zu, timer fired %zu\n", args.num_set, args.num_fired);
  return 0;
}

struct timer_args {
  volatile int result;
  volatile int timer_fired;
  volatile int remaining;
  volatile int wait;
  spin_lock_t* lock;
};

static void timer_cb(struct timer*, zx_time_t now, void* void_arg) {
  timer_args* arg = reinterpret_cast<timer_args*>(void_arg);
  atomic_store(&arg->timer_fired, 1);
}

// Set a timer and cancel it before the deadline has elapsed.
static bool cancel_before_deadline() {
  BEGIN_TEST;
  timer_args arg{};
  timer_t t = TIMER_INITIAL_VALUE(t);
  const Deadline deadline = Deadline::no_slack(current_time() + ZX_HOUR(5));
  timer_set(&t, deadline, timer_cb, &arg);
  ASSERT_TRUE(timer_cancel(&t));
  ASSERT_FALSE(atomic_load(&arg.timer_fired));
  END_TEST;
}

// Set a timer and cancel it after it has fired.
static bool cancel_after_fired() {
  BEGIN_TEST;
  timer_args arg{};
  timer_t t = TIMER_INITIAL_VALUE(t);
  const Deadline deadline = Deadline::no_slack(current_time());
  timer_set(&t, deadline, timer_cb, &arg);
  while (!atomic_load(&arg.timer_fired)) {
  }
  ASSERT_FALSE(timer_cancel(&t));
  END_TEST;
}

static void timer_cancel_cb(struct timer* t, zx_time_t now, void* void_arg) {
  timer_args* arg = reinterpret_cast<timer_args*>(void_arg);
  atomic_store(&arg->result, timer_cancel(t));
  atomic_store(&arg->timer_fired, 1);
}

// Set a timer and cancel it from its own callback.
static bool cancel_from_callback() {
  BEGIN_TEST;
  timer_args arg{};
  arg.result = 1;
  timer_t t = TIMER_INITIAL_VALUE(t);
  const Deadline deadline = Deadline::no_slack(current_time());
  timer_set(&t, deadline, timer_cancel_cb, &arg);
  while (!atomic_load(&arg.timer_fired)) {
  }
  ASSERT_FALSE(arg.result);
  ASSERT_FALSE(timer_cancel(&t));
  END_TEST;
}

static void timer_set_cb(struct timer* t, zx_time_t now, void* void_arg) {
  timer_args* arg = reinterpret_cast<timer_args*>(void_arg);
  if (atomic_add(&arg->remaining, -1) >= 1) {
    const Deadline deadline = Deadline::no_slack(current_time() + ZX_USEC(10));
    timer_set(t, deadline, timer_set_cb, void_arg);
  }
}

// Set a timer that re-sets itself from its own callback.
static bool set_from_callback() {
  BEGIN_TEST;
  timer_args arg{};
  arg.remaining = 5;
  timer_t t = TIMER_INITIAL_VALUE(t);
  const Deadline deadline = Deadline::no_slack(current_time());
  timer_set(&t, deadline, timer_set_cb, &arg);
  while (atomic_load(&arg.remaining) > 0) {
  }

  // We cannot assert the return value below because we don't know if the last timer has fired.
  timer_cancel(&t);

  END_TEST;
}

static void timer_trylock_cb(struct timer* t, zx_time_t now, void* void_arg) {
  timer_args* arg = reinterpret_cast<timer_args*>(void_arg);
  atomic_store(&arg->timer_fired, 1);
  while (atomic_load(&arg->wait)) {
  }

  int result = timer_trylock_or_cancel(t, arg->lock);
  if (!result) {
    spin_unlock(arg->lock);
  }

  atomic_store(&arg->result, result);
}

// See that timer_trylock_or_cancel spins until the timer is canceled.
static bool trylock_or_cancel_canceled() {
  BEGIN_TEST;

  // We need 2 or more CPUs for this test.
  if (get_num_cpus_online() < 2) {
    printf("skipping test trylock_or_cancel_canceled, not enough online cpus\n");
    return true;
  }

  timer_args arg{};
  timer_t t = TIMER_INITIAL_VALUE(t);

  SpinLock lock;
  arg.lock = lock.GetInternal();
  arg.wait = 1;

  arch_disable_ints();

  uint timer_cpu = arch_curr_cpu_num();
  const Deadline deadline = Deadline::no_slack(current_time() + ZX_USEC(100));
  timer_set(&t, deadline, timer_trylock_cb, &arg);

  // The timer is set to run on timer_cpu, switch to a different CPU, acquire the spinlock then
  // signal the callback to proceed.
  thread_set_cpu_affinity(get_current_thread(), ~cpu_num_to_mask(timer_cpu));
  DEBUG_ASSERT(arch_curr_cpu_num() != timer_cpu);

  arch_enable_ints();

  {
    AutoSpinLock guard(&lock);

    while (!atomic_load(&arg.timer_fired)) {
    }

    // Callback should now be running. Tell it to stop waiting and start trylocking.
    atomic_store(&arg.wait, 0);

    // See that timer_cancel returns false indicating that the timer ran.
    ASSERT_FALSE(timer_cancel(&t));
  }

  // See that the timer failed to acquire the lock.
  ASSERT_TRUE(arg.result);
  END_TEST;
}

// See that timer_trylock_or_cancel acquires the lock when the holder releases it.
static bool trylock_or_cancel_get_lock() {
  BEGIN_TEST;

  // We need 2 or more CPUs for this test.
  if (get_num_cpus_online() < 2) {
    printf("skipping test trylock_or_cancel_get_lock, not enough online cpus\n");
    return true;
  }

  timer_args arg{};
  timer_t t = TIMER_INITIAL_VALUE(t);

  SpinLock lock;
  arg.lock = lock.GetInternal();
  arg.wait = 1;

  arch_disable_ints();

  uint timer_cpu = arch_curr_cpu_num();
  const Deadline deadline = Deadline::no_slack(current_time() + ZX_USEC(100));
  timer_set(&t, deadline, timer_trylock_cb, &arg);

  // The timer is set to run on timer_cpu, switch to a different CPU, acquire the spinlock then
  // signal the callback to proceed.
  thread_set_cpu_affinity(get_current_thread(), ~cpu_num_to_mask(timer_cpu));
  DEBUG_ASSERT(arch_curr_cpu_num() != timer_cpu);

  arch_enable_ints();

  {
    AutoSpinLock guard(&lock);

    while (!atomic_load(&arg.timer_fired)) {
    }

    // Callback should now be running. Tell it to stop waiting and start trylocking.
    atomic_store(&arg.wait, 0);
  }

  // See that timer_cancel returns false indicating that the timer ran.
  ASSERT_FALSE(timer_cancel(&t));

  // Note, we cannot assert the value of arg.result. We have both released the lock and canceled
  // the timer, but we don't know which of these events the timer observed first.

  END_TEST;
}

UNITTEST_START_TESTCASE(timer_tests)
UNITTEST("cancel_before_deadline", cancel_before_deadline)
UNITTEST("cancel_after_fired", cancel_after_fired)
UNITTEST("cancel_from_callback", cancel_from_callback)
UNITTEST("set_from_callback", set_from_callback)
UNITTEST("trylock_or_cancel_canceled", trylock_or_cancel_canceled)
UNITTEST("trylock_or_cancel_get_lock", trylock_or_cancel_get_lock)
UNITTEST_END_TESTCASE(timer_tests, "timer", "timer tests")
