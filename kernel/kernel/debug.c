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
#include <stdio.h>
#include <string.h>
#include <kernel/thread.h>
#include <kernel/timer.h>
#include <kernel/mp.h>
#include <err.h>
#include <platform.h>

#if WITH_LIB_CONSOLE
#include <lib/console.h>

static int cmd_threads(int argc, const cmd_args *argv);
static int cmd_threadstats(int argc, const cmd_args *argv);
static int cmd_threadload(int argc, const cmd_args *argv);
static int cmd_kill(int argc, const cmd_args *argv);

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 1
STATIC_COMMAND_MASKED("threads", "list kernel threads", &cmd_threads, CMD_AVAIL_ALWAYS)
#endif
#if THREAD_STATS
STATIC_COMMAND("threadstats", "thread level statistics", &cmd_threadstats)
STATIC_COMMAND("threadload", "toggle thread load display", &cmd_threadload)
#endif
STATIC_COMMAND("kill", "kill a thread", &cmd_kill)
STATIC_COMMAND_END(kernel);

#if LK_DEBUGLEVEL > 1
static int cmd_threads(int argc, const cmd_args *argv)
{
    printf("thread list:\n");
    dump_all_threads();

    return 0;
}
#endif

#if THREAD_STATS
static int cmd_threadstats(int argc, const cmd_args *argv)
{
    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        if (!mp_is_cpu_active(i))
            continue;

        printf("thread stats (cpu %u):\n", i);
        printf("\ttotal idle time: %" PRIu64 "\n", thread_stats[i].idle_time);
        printf("\ttotal busy time: %" PRIu64 "\n",
               current_time_hires() - thread_stats[i].idle_time);
        printf("\treschedules: %lu\n", thread_stats[i].reschedules);
#if WITH_SMP
        printf("\treschedule_ipis: %lu\n", thread_stats[i].reschedule_ipis);
#endif
        printf("\tcontext_switches: %lu\n", thread_stats[i].context_switches);
        printf("\tpreempts: %lu\n", thread_stats[i].preempts);
        printf("\tyields: %lu\n", thread_stats[i].yields);
        printf("\tinterrupts: %lu\n", thread_stats[i].interrupts);
        printf("\ttimer interrupts: %lu\n", thread_stats[i].timer_ints);
        printf("\ttimers: %lu\n", thread_stats[i].timers);
    }

    return 0;
}

static enum handler_return threadload(struct timer *t, lk_time_t now, void *arg)
{
    static struct thread_stats old_stats[SMP_MAX_CPUS];
    static lk_bigtime_t last_idle_time[SMP_MAX_CPUS];

    for (uint i = 0; i < SMP_MAX_CPUS; i++) {
        /* dont display time for inactiv cpus */
        if (!mp_is_cpu_active(i))
            continue;

        lk_bigtime_t idle_time = thread_stats[i].idle_time;

        /* if the cpu is currently idle, add the time since it went idle up until now to the idle counter */
        bool is_idle = !!mp_is_cpu_idle(i);
        if (is_idle) {
            idle_time += current_time_hires() - thread_stats[i].last_idle_timestamp;
        }

        lk_bigtime_t delta_time = idle_time - last_idle_time[i];
        lk_bigtime_t busy_time = 1000000000ULL - (delta_time > 1000000000ULL ? 1000000000ULL : delta_time);
        uint busypercent = (busy_time * 10000) / (1000000000);

        printf("cpu %u LOAD: "
               "%u.%02u%%, "
               "cs %lu, "
               "ylds %lu, "
               "pmpts %lu, "
               "irq_pmpts %lu, "
#if WITH_SMP
               "rs_ipis %lu, "
#endif
               "ints %lu, "
               "tmr ints %lu, "
               "tmrs %lu\n",
               i,
               busypercent / 100, busypercent % 100,
               thread_stats[i].context_switches - old_stats[i].context_switches,
               thread_stats[i].yields - old_stats[i].yields,
               thread_stats[i].preempts - old_stats[i].preempts,
               thread_stats[i].irq_preempts - old_stats[i].irq_preempts,
#if WITH_SMP
               thread_stats[i].reschedule_ipis - old_stats[i].reschedule_ipis,
#endif
               thread_stats[i].interrupts - old_stats[i].interrupts,
               thread_stats[i].timer_ints - old_stats[i].timer_ints,
               thread_stats[i].timers - old_stats[i].timers);

        old_stats[i] = thread_stats[i];
        last_idle_time[i] = idle_time;
    }

    return INT_NO_RESCHEDULE;
}

static int cmd_threadload(int argc, const cmd_args *argv)
{
    static bool showthreadload = false;
    static timer_t tltimer;

    if (showthreadload == false) {
        // start the display
        timer_initialize(&tltimer);
        timer_set_periodic(&tltimer, 1000, &threadload, NULL);
        showthreadload = true;
    } else {
        timer_cancel(&tltimer);
        showthreadload = false;
    }

    return 0;
}

#endif // THREAD_STATS

static int cmd_kill(int argc, const cmd_args *argv)
{
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
