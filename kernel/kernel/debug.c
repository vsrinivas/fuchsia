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
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>

#if WITH_LIB_CONSOLE
#include <lib/console.h>

static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags);
static int cmd_kill(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 1
STATIC_COMMAND_MASKED("thread", "list kernel threads with options", &cmd_thread, CMD_AVAIL_ALWAYS)
#endif
STATIC_COMMAND("threadstats", "thread level statistics", &cmd_threadstats)
STATIC_COMMAND("threadload", "toggle thread load display", &cmd_threadload)
STATIC_COMMAND("kill", "kill a thread", &cmd_kill)
STATIC_COMMAND_END(kernel);

#if LK_DEBUGLEVEL > 1
static int cmd_thread(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
        printf("not enough arguments\n");
    usage:
        printf("%s list\n", argv[0].str);
        printf("%s list_full\n", argv[0].str);
        return -1;
    }

    if (!strcmp(argv[1].str, "list")) {
        printf("thread list:\n");
        dump_all_threads(false);

        /* reschedule to let debuglog potentially run */
        if (!(flags & CMD_FLAG_PANIC))
            thread_reschedule();
    } else if (!strcmp(argv[1].str, "list_full")) {
        printf("thread list:\n");
        dump_all_threads(true);

        /* reschedule to let debuglog potentially run */
        if (!(flags & CMD_FLAG_PANIC))
            thread_reschedule();
    } else {
        printf("invalid args\n");
        goto usage;
    }

    return 0;
}
#endif

static int cmd_threadstats(int argc, const cmd_args* argv, uint32_t flags) {
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        if (!mp_is_cpu_active(i))
            continue;

        printf("thread stats (cpu %u):\n", i);
        printf("\ttotal idle time: %" PRIu64 "\n", percpu[i].stats.idle_time);
        printf("\ttotal busy time: %" PRIu64 "\n",
               current_time() - percpu[i].stats.idle_time);
        printf("\treschedules: %lu\n", percpu[i].stats.reschedules);
        printf("\treschedule_ipis: %lu\n", percpu[i].stats.reschedule_ipis);
        printf("\tcontext_switches: %lu\n", percpu[i].stats.context_switches);
        printf("\tpreempts: %lu\n", percpu[i].stats.preempts);
        printf("\tyields: %lu\n", percpu[i].stats.yields);
        printf("\tinterrupts: %lu\n", percpu[i].stats.interrupts);
        printf("\ttimer interrupts: %lu\n", percpu[i].stats.timer_ints);
        printf("\ttimers: %lu\n", percpu[i].stats.timers);
    }

    return 0;
}

static enum handler_return threadload(struct timer* t, lk_time_t now, void* arg) {
    static struct cpu_stats old_stats[SMP_MAX_CPUS];
    static lk_time_t last_idle_time[SMP_MAX_CPUS];

    printf("cpu    load"
           " sched (cs ylds pmpts irq_pmpts)"
           " excep"
           "    pf"
           "  sysc"
           " ints (hw  tmr tmr_cb)"
           " ipi (rs  gen)\n");
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        /* dont display time for inactive cpus */
        if (!mp_is_cpu_active(i))
            continue;

        lk_time_t idle_time = percpu[i].stats.idle_time;

        /* if the cpu is currently idle, add the time since it went idle up until now to the idle counter */
        bool is_idle = !!mp_is_cpu_idle(i);
        if (is_idle) {
            idle_time += current_time() - percpu[i].idle_thread.last_started_running;
        }

        lk_time_t delta_time = idle_time - last_idle_time[i];
        lk_time_t busy_time = LK_SEC(1) - (delta_time > LK_SEC(1) ? LK_SEC(1) : delta_time);
        uint busypercent = (busy_time * 10000) / LK_SEC(1);

        printf("%3u"
               " %3u.%02u%%"
               " %9lu %4lu %5lu %9lu"
               " %6lu"
               " %5lu"
               " %5lu"
               " %8lu %4lu %6lu"
               " %8lu %4lu"
               "\n",
               i,
               busypercent / 100, busypercent % 100,
               percpu[i].stats.context_switches - old_stats[i].context_switches,
               percpu[i].stats.yields - old_stats[i].yields,
               percpu[i].stats.preempts - old_stats[i].preempts,
               percpu[i].stats.irq_preempts - old_stats[i].irq_preempts,
               percpu[i].stats.exceptions - old_stats[i].exceptions,
               percpu[i].stats.page_faults - old_stats[i].page_faults,
               percpu[i].stats.syscalls - old_stats[i].syscalls,
               percpu[i].stats.interrupts - old_stats[i].interrupts,
               percpu[i].stats.timer_ints - old_stats[i].timer_ints,
               percpu[i].stats.timers - old_stats[i].timers,
               percpu[i].stats.reschedule_ipis - old_stats[i].reschedule_ipis,
               percpu[i].stats.generic_ipis - old_stats[i].generic_ipis);

        old_stats[i] = percpu[i].stats;
        last_idle_time[i] = idle_time;
    }

    timer_set(t, now + LK_SEC(1), TIMER_SLACK_CENTER, LK_MSEC(10), &threadload, NULL);

    /* reschedule here to allow the debuglog a chance to run */
    return INT_RESCHEDULE;
}

static int cmd_threadload(int argc, const cmd_args* argv, uint32_t flags) {
    static bool showthreadload = false;
    static timer_t tltimer;

    if (showthreadload == false) {
        // start the display
        timer_init(&tltimer);
        timer_set(&tltimer, current_time() + LK_SEC(1),
                  TIMER_SLACK_CENTER, LK_MSEC(10), &threadload, NULL);
        showthreadload = true;
    } else {
        timer_cancel(&tltimer);
        showthreadload = false;
    }

    return 0;
}

static int cmd_kill(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
        printf("not enough arguments\n");
        return -1;
    }

    bool wait = true;
    if (argc >= 3 && !strcmp(argv[2].str, "nowait"))
        wait = false;
    thread_kill(argv[1].p, wait);

    return 0;
}

#endif // WITH_LIB_CONSOLE
