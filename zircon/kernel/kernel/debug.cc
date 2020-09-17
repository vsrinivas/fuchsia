// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @defgroup debug  Debug
 * @{
 */

/**
 * @file
 * @brief  Debug console functions.
 */

#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <lib/console.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <vm/vm.h>

static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("thread", "manipulate kernel threads", &cmd_thread, CMD_AVAIL_ALWAYS)
STATIC_COMMAND("threadstats", "thread level statistics", &cmd_threadstats)
STATIC_COMMAND("threadload", "toggle thread load display", &cmd_threadload)
STATIC_COMMAND("threadq", "toggle thread queue display", &cmd_threadq)
STATIC_COMMAND_END(kernel)

static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
  usage:
    printf("%s bt <thread pointer or id>\n", argv[0].str);
    printf("%s dump <thread pointer or id>\n", argv[0].str);
    printf("%s list\n", argv[0].str);
    printf("%s list_full\n", argv[0].str);
    return -1;
  }

  if (!strcmp(argv[1].str, "bt")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    Thread* t = NULL;
    if (is_kernel_address(argv[2].u)) {
      t = (Thread*)argv[2].u;
    } else {
      t = thread_id_to_thread_slow(argv[2].u);
    }
    if (t) {
      t->PrintBacktrace();
    }
  } else if (!strcmp(argv[1].str, "dump")) {
    if (argc < 3) {
      goto notenoughargs;
    }

    Thread* t = NULL;
    if (is_kernel_address(argv[2].u)) {
      t = (Thread*)argv[2].u;
      dump_thread(t, true);
    } else {
      if (flags & CMD_FLAG_PANIC) {
        dump_thread_user_tid_during_panic(argv[2].u, true);
      } else {
        dump_thread_user_tid(argv[2].u, true);
      }
    }
  } else if (!strcmp(argv[1].str, "list")) {
    printf("thread list:\n");
    if (flags & CMD_FLAG_PANIC) {
      dump_all_threads_during_panic(false);
    } else {
      dump_all_threads(false);
    }
  } else if (!strcmp(argv[1].str, "list_full")) {
    printf("thread list:\n");
    if (flags & CMD_FLAG_PANIC) {
      dump_all_threads_during_panic(true);
    } else {
      dump_all_threads(true);
    }
  } else {
    printf("invalid args\n");
    goto usage;
  }

  // reschedule to let debuglog potentially run
  if (!(flags & CMD_FLAG_PANIC)) {
    Thread::Current::Reschedule();
  }

  return 0;
}

static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags) {
  for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
    if (!mp_is_cpu_active(i)) {
      continue;
    }
    const auto& percpu = percpu::Get(i);

    printf("thread stats (cpu %u):\n", i);
    printf("\ttotal idle time: %" PRIi64 "\n", percpu.stats.idle_time);
    printf("\ttotal busy time: %" PRIi64 "\n",
           zx_time_sub_duration(current_time(), percpu.stats.idle_time));
    printf("\treschedules: %lu\n", percpu.stats.reschedules);
    printf("\treschedule_ipis: %lu\n", percpu.stats.reschedule_ipis);
    printf("\tcontext_switches: %lu\n", percpu.stats.context_switches);
    printf("\tpreempts: %lu\n", percpu.stats.preempts);
    printf("\tyields: %lu\n", percpu.stats.yields);
    printf("\ttimer interrupts: %lu\n", percpu.stats.timer_ints);
    printf("\ttimers: %lu\n", percpu.stats.timers);
  }

  return 0;
}

namespace {

class RecurringCallback {
 public:
  typedef void (*CallbackFunc)();

  RecurringCallback(CallbackFunc callback) : func_(callback) {}

  void Toggle();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(RecurringCallback);

  static void CallbackWrapper(Timer* t, zx_time_t now, void* arg);

  DECLARE_SPINLOCK(SpinLock) lock_;
  Timer timer_;
  bool started_ = false;
  CallbackFunc func_ = nullptr;
};

static constexpr TimerSlack kSlack{ZX_MSEC(10), TIMER_SLACK_CENTER};

void RecurringCallback::CallbackWrapper(Timer* t, zx_time_t now, void* arg) {
  auto cb = static_cast<RecurringCallback*>(arg);
  cb->func_();

  {
    Guard<SpinLock, IrqSave> guard{&cb->lock_};

    if (cb->started_) {
      const Deadline deadline(zx_time_add_duration(now, ZX_SEC(1)), kSlack);
      t->Set(deadline, CallbackWrapper, arg);
    }
  }

  // reschedule to give the debuglog a chance to run
  Thread::Current::preemption_state().PreemptSetPending();
}

void RecurringCallback::Toggle() {
  Guard<SpinLock, IrqSave> guard{&lock_};

  if (!started_) {
    const Deadline deadline = Deadline::after(ZX_SEC(1), kSlack);
    // start the timer
    timer_.Set(deadline, CallbackWrapper, static_cast<void*>(this));
    started_ = true;
  } else {
    timer_.Cancel();
    started_ = false;
  }
}

}  // anonymous namespace

static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags) {
  static RecurringCallback cb([]() {
    static struct cpu_stats old_stats[SMP_MAX_CPUS];
    static zx_duration_t last_idle_time[SMP_MAX_CPUS];

    printf(
        "cpu    load"
        " sched (cs ylds pmpts irq_pmpts)"
        "  sysc"
        " ints (hw  tmr tmr_cb)"
        " ipi (rs  gen)\n");
    for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
      Guard<SpinLock, NoIrqSave> thread_lock_guard{ThreadLock::Get()};

      // dont display time for inactive cpus
      if (!mp_is_cpu_active(i)) {
        continue;
      }
      const auto& percpu = percpu::Get(i);

      zx_duration_t idle_time = percpu.stats.idle_time;

      // if the cpu is currently idle, add the time since it went idle up until now to the idle
      // counter
      bool is_idle = !!mp_is_cpu_idle(i);
      if (is_idle) {
        zx_duration_t recent_idle_time = zx_time_sub_time(
            current_time(), percpu.idle_thread.scheduler_state().last_started_running());
        idle_time = zx_duration_add_duration(idle_time, recent_idle_time);
      }

      zx_duration_t delta_time = zx_duration_sub_duration(idle_time, last_idle_time[i]);
      zx_duration_t busy_time;
      if (ZX_SEC(1) > delta_time) {
        busy_time = zx_duration_sub_duration(ZX_SEC(1), delta_time);
      } else {
        busy_time = 0;
      }
      zx_duration_t busypercent = zx_duration_mul_int64(busy_time, 10000) / ZX_SEC(1);

      printf(
          "%3u"
          " %3u.%02u%%"
          " %9lu %4lu %5lu %9lu"
          " %5lu"
          " %8lu %4lu %6lu"
          " %8lu %4lu"
          "\n",
          i, static_cast<uint>(busypercent / 100), static_cast<uint>(busypercent % 100),
          percpu.stats.context_switches - old_stats[i].context_switches,
          percpu.stats.yields - old_stats[i].yields, percpu.stats.preempts - old_stats[i].preempts,
          percpu.stats.irq_preempts - old_stats[i].irq_preempts,
          percpu.stats.syscalls - old_stats[i].syscalls,
          percpu.stats.interrupts - old_stats[i].interrupts,
          percpu.stats.timer_ints - old_stats[i].timer_ints,
          percpu.stats.timers - old_stats[i].timers,
          percpu.stats.reschedule_ipis - old_stats[i].reschedule_ipis,
          percpu.stats.generic_ipis - old_stats[i].generic_ipis);

      old_stats[i] = percpu.stats;
      last_idle_time[i] = idle_time;
    }
  });

  cb.Toggle();

  return 0;
}

static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags) {
  static RecurringCallback callback([]() {
    printf("----------------------------------------------------\n");
    for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
      Guard<SpinLock, NoIrqSave> thread_lock_guard{ThreadLock::Get()};

      if (!mp_is_cpu_active(i)) {
        continue;
      }

      printf("thread queue cpu %2u:\n", i);
      percpu::Get(i).scheduler.Dump();
    }
    printf("\n");
  });

  callback.Toggle();

  return 0;
}
