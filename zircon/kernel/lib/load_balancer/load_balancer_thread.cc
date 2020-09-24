// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/console.h>
#include <lib/counters.h>
#include <lib/load_balancer.h>
#include <lib/system-topology.h>
#include <trace.h>

#include <ktl/atomic.h>
#include <lk/init.h>

#define LOCAL_TRACE 0

namespace {

#if !DISABLE_PERIODIC_LOAD_BALANCER

// This value is a tradeoff between how long the cycle takes to run and how fresh the
// resulting data is. At the time of implementation the cycle takes 1.5us to run, so we
// want the period to be sufficiently high that it is predominantly sleeping. However
// this generates information that guides thread placement and the more recent that
// information is the more efficient our thread placement will be.
constexpr zx_duration_t kPeriod = ZX_MSEC(20);

ktl::atomic<bool> print_state = false;

int load_balancer_thread(void*) {
  LTRACEF("Load Balancer Thread running.\n");

  load_balancer::LoadBalancer<> balancer;
  while (true) {
    const zx_time_t start = current_time();

    balancer.Cycle();
    if (print_state) {
      balancer.PrintState();
      print_state = false;
    }

    const zx_duration_t cycle_duration = zx_time_sub_time(current_time(), start);
    // In practice the cycle_duration is fairly small but we compensate for it to
    // keep to our period.
    Thread::Current::SleepRelative(zx_time_sub_duration(kPeriod, cycle_duration));
  }
  // This thread should never exit.
  ASSERT(false);
  return 0;
}

void load_balancer_init(uint) {
  Thread::Create("load-balancer-thread", load_balancer_thread, nullptr, DEFAULT_PRIORITY)
      ->DetachAndResume();

  LTRACEF("Load Balancer Thread detached.\n");
}

// We want to run before the system goes fully threaded to set the initial
// values for early core load shedding. If we don't threads won't move cores and
// we will lose out potential parrallelism in early boot.
LK_INIT_HOOK(load_balancer_init, &load_balancer_init, LK_INIT_LEVEL_TOPOLOGY)

static int cmd_lb(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
    printf("usage:\n");
    printf("%s state - print state to console\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }

  if (strcmp(argv[1].str, "state") == 0) {
    print_state.store(true);
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("lb",
               "Thread Load balancer commands, responsible for balacing processing "
               "load across processors.",
               &cmd_lb)
STATIC_COMMAND_END(lb)

#endif
}  // namespace
