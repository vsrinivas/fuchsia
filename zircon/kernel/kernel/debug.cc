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
#include <inttypes.h>
#include <lib/console.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <ffl/string.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <vm/vm.h>

static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_zmips(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("thread", "manipulate kernel threads", &cmd_thread, CMD_AVAIL_ALWAYS)
STATIC_COMMAND("threadstats", "thread level statistics", &cmd_threadstats)
STATIC_COMMAND("threadload", "toggle thread load display", &cmd_threadload)
STATIC_COMMAND("threadq", "toggle thread queue display", &cmd_threadq)
STATIC_COMMAND("zmips", "compute zmips of a cpu", &cmd_zmips)
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
      Backtrace bt;
      t->GetBacktrace(bt);
      bt.Print();
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
        dump_thread_tid_during_panic(argv[2].u, true);
      } else {
        dump_thread_tid(argv[2].u, true);
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

RecurringCallback g_threadload_callback([]() {
  static struct cpu_stats old_stats[SMP_MAX_CPUS];
  static zx_duration_t last_idle_time[SMP_MAX_CPUS];

  printf(
      "cpu    load"
      " sched (cs ylds pmpts irq_pmpts)"
      "  sysc"
      " ints (hw  tmr tmr_cb)"
      " ipi (rs  gen)\n");
  for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
    Guard<MonitoredSpinLock, NoIrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};

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

RecurringCallback g_threadq_callback([]() {
  printf("----------------------------------------------------\n");
  for (cpu_num_t i = 0; i < percpu::processor_count(); i++) {
    if (mp_is_cpu_active(i)) {
      printf("thread queue cpu %2u:\n", i);
      percpu::Get(i).scheduler.Dump();
    }
  }
  printf("\n");
});

}  // anonymous namespace
static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags) {
  g_threadload_callback.Toggle();
  return 0;
}

static int cmd_threadq(int argc, const cmd_args* argv, uint32_t flags) {
  g_threadq_callback.Toggle();
  return 0;
}

static int cmd_zmips(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("Not enough arguments.\n");
  usage:
    printf("%s <cpu number>\n", argv[0].str);
    return -1;
  }

  cpu_num_t cpu_num = static_cast<cpu_num_t>(argv[1].u);
  if (cpu_num >= percpu::processor_count()) {
    printf("CPU number must be in the range [%zu, %zu].\n", size_t{0},
           percpu::processor_count() - 1);
    goto usage;
  }

  const auto calibrate = [](void* arg) -> int {
    const cpu_num_t cpu_num = *static_cast<cpu_num_t*>(arg);

    // Busy loop for the given number of iterations.
    const auto delay = [](uint64_t loops) {
      while (loops != 0) {
        __asm__ volatile("" : "+g"(loops)::);
        --loops;
      }
    };

    using U30 = ffl::Fixed<uint64_t, 30>;
    U30 zmips_min = U30::Max();
    U30 zmips_max = U30::Min();

    const int max_samples = 10;
    const uint64_t max_loops = uint64_t{1} << 48;
    const zx_duration_t target_duration_ns = ZX_SEC(1) / 20;

    for (int i = 0; i < max_samples; i++) {
      // Quickly find the number of loops it takes for the delay loop to run for at least the target
      // duration by stepping in power of two increments, avoiding excessively large values.
      for (uint64_t loops = 1; loops < max_loops; loops *= 2) {
        // Disable interrupts to limit the noise of the measurement. The target duration is selected
        // to provide suitable precision without disabling interrupts for too long to risk tripping
        // software/hardware watchdogs.
        InterruptDisableGuard interrupt_disable;
        const zx_time_t start_ns = current_time();
        delay(loops);
        const zx_time_t stop_ns = current_time();
        interrupt_disable.Reenable();

        const zx_duration_t duration_ns = zx_time_sub_time(stop_ns, start_ns);
        if (duration_ns >= target_duration_ns) {
          printf("Calibrating CPU %u: %" PRIu64 " loops per %" PRId64 " ns\n", cpu_num, loops,
                 duration_ns);

          const U30 zmips = U30{loops} / duration_ns * 1000;
          zmips_min = ktl::min(zmips_min, zmips);
          zmips_max = ktl::max(zmips_max, zmips);
          break;
        }
      }
    }

    printf("Calibrated CPU %u: %s-%s ZMIPS\n", cpu_num,
           Format(zmips_min, ffl::String::Mode::Dec, 2).c_str(),
           Format(zmips_max, ffl::String::Mode::Dec, 2).c_str());

    return 0;
  };

  Thread* thread = Thread::Create("calibrate_zmips", +calibrate, &cpu_num, DEFAULT_PRIORITY);
  if (thread == nullptr) {
    printf("Failed to create calibration thread!\n");
    return -1;
  }

  thread->SetCpuAffinity(cpu_num_to_mask(cpu_num));
  thread->Resume();

  int retcode;
  thread->Join(&retcode, ZX_TIME_INFINITE);

  return retcode;
}
