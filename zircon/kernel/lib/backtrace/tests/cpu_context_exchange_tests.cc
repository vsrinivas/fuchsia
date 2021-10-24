// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/backtrace/cpu_context_exchange.h>
#include <lib/fit/defer.h>
#include <lib/unittest/unittest.h>

#include <arch/interrupt.h>
#include <kernel/mp.h>

#if defined(__x86_64__)
#include <lib/backtrace/global_cpu_context_exchange.h>
#endif

namespace {

void NoOpNotify(cpu_num_t target) {}

// See that when there's no active request, |HandleRequest| returns.
bool NoActiveRequestTest() {
  BEGIN_TEST;

  CpuContextExchange exchange(&NoOpNotify);

  // No one waiting?  No-op.
  iframe_t frame{};
  {
    InterruptDisableGuard irqd;
    exchange.HandleRequest(0, frame);
  }

  END_TEST;
}

bool TimeoutTest() {
  BEGIN_TEST;

  CpuContextExchange exchange(&NoOpNotify);
  CpuContext context;

  // See that we timeout at or after the specified timeout duration.
  const zx_duration_t timeout = ZX_USEC(200);
  zx_duration_t delta;
  zx_status_t status;
  {
    InterruptDisableGuard irqd;
    zx_time_t before = current_time();
    status = exchange.RequestContext(0, timeout, context);
    delta = current_time() - before;
  }
  EXPECT_EQ(ZX_ERR_TIMED_OUT, status);
  EXPECT_GE(delta, timeout);

  // At this point the exchange is "stuck" waiting on a reply from CPU-0.  See
  // that a subsequent request fails with ZX_ERR_TIMED_OUT.
  {
    InterruptDisableGuard irqd;
    status = exchange.RequestContext(0, timeout, context);
  }
  EXPECT_EQ(ZX_ERR_TIMED_OUT, status);

  END_TEST;
}

// Have one CPU request the context of all others.
bool OneToManyTest() {
  BEGIN_TEST;

  struct Shared {
    CpuContextExchange<decltype(&NoOpNotify)> exchange;
    ktl::atomic<bool> ready{false};
    ktl::atomic<size_t> num_running{0};
  } shared{CpuContextExchange(&NoOpNotify), false, 0};

  // Each responder will get a pointer to their own struct.
  struct Responder {
    Shared* shared{nullptr};
    cpu_num_t cpu{INVALID_CPU};
    Thread* thread{nullptr};

    ~Responder() {
      if (thread != nullptr) {
        thread->Join(nullptr, ZX_TIME_INFINITE);
        thread = nullptr;
      }
    }
  } responders[SMP_MAX_CPUS];
  size_t num_responders = 0;

  cpu_mask_t mask = mp_get_active_mask();
  const cpu_num_t requester = remove_cpu_from_mask(mask);
  if (requester == INVALID_CPU || mask == 0) {
    printf("not enough active cpus; skipping test\n");
    END_TEST;
  }

  // This thread will be the requester.
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(requester));

  thread_start_routine responder_fn = [](void* arg) -> int {
    Responder* r = reinterpret_cast<Responder*>(arg);
    iframe_t frame{};

    // Wait to be signaled.
    r->shared->num_running.fetch_add(1);
    while (!r->shared->ready.load()) {
      arch::Yield();
    }

    // Keep going until told to stop.
    while (r->shared->ready.load()) {
      {
        InterruptDisableGuard irqd;
        r->shared->exchange.HandleRequest(0, frame);
      }
      arch::Yield();
    }
    return 0;
  };

  // One thread for each responder.
  cpu_num_t cpu;
  while ((cpu = remove_cpu_from_mask(mask)) != INVALID_CPU) {
    Responder& responder = responders[num_responders];
    responder.shared = &shared;
    responder.cpu = cpu;
    responder.thread =
        Thread::Create("cpu context exchange", responder_fn, &responder, DEFAULT_PRIORITY);
    ASSERT_NONNULL(responder.thread);

    responder.thread->SetCpuAffinity(cpu_num_to_mask(responder.cpu));
    responder.thread->Resume();
    ++num_responders;
  }

  // Wait for them to start running.
  while (shared.num_running.load() < num_responders) {
    arch::Yield();
  }

  // Go!
  shared.ready.store(true);

  CpuContext context;
  for (size_t x = 0; x < num_responders; ++x) {
    zx_status_t status;
    {
      InterruptDisableGuard irqd;
      status = shared.exchange.RequestContext(responders[x].cpu, ZX_TIME_INFINITE, context);
    }
    EXPECT_EQ(ZX_OK, status);
  }

  shared.ready.store(false);

  END_TEST;
}

// TODO(maniscalco): Add a ManyToOne test.

bool NmiInterruptsTimerTest() {
  BEGIN_TEST;

#if defined(__x86_64__)
  struct Args {
    ktl::atomic<bool> timer_fired{false};
    zx_status_t status{ZX_ERR_INTERNAL};
  } args;

  Timer t;
  Timer::Callback timer_cb = [](Timer*, zx_time_t, void* _args) {
    auto* args = reinterpret_cast<Args*>(_args);
    CpuContext context;
    args->status =
        g_cpu_context_exchange.RequestContext(arch_curr_cpu_num(), ZX_TIME_INFINITE, context);
    args->timer_fired.store(true);
  };
  t.Set(Deadline::no_slack(ZX_TIME_INFINITE_PAST), timer_cb, &args);

  // Wait for the timer to fire.
  while (!args.timer_fired.load()) {
    arch::Yield();
  }
  ASSERT_FALSE(t.Cancel());

  // See that the timer successfully interrupted itself.
  ASSERT_EQ(ZX_OK, args.status);
#else
  printf("test is x86-only; skipping test");
#endif

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(cpu_context_exchange_tests)
UNITTEST("NoActiveRequest", NoActiveRequestTest)
UNITTEST("Timeout", TimeoutTest)
UNITTEST("OneToMany", OneToManyTest)
UNITTEST("NmiInterruptsTimer", NmiInterruptsTimerTest)
UNITTEST_END_TESTCASE(cpu_context_exchange_tests, "cpu_context_exchange",
                      "cpu context exchange tests")
