// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/device/sysinfo.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: dynamically compute this based on what it returns
#define MAX_CPUS 32

static bool cpu_stats = true;
static mx_time_t delay = MX_SEC(1);

static mx_status_t cpustats(mx_handle_t root_resource) {
    static mx_time_t last_idle_time[MAX_CPUS];
    static mx_info_cpu_stats_t old_stats[MAX_CPUS];
    mx_info_cpu_stats_t stats[MAX_CPUS];

    // retrieve the system stats
    size_t actual, avail;
    mx_status_t err = mx_object_get_info(root_resource, MX_INFO_CPU_STATS, &stats, sizeof(stats), &actual, &avail);
    if (err != NO_ERROR) {
        fprintf(stderr, "MX_INFO_CPU_STATS returns %d (%s)\n", err, mx_status_get_string(err));
        return err;
    }

    if (actual < avail) {
        fprintf(stderr, "WARNING: actual cpus reported %zu less than available cpus %zu\n",
                actual, avail);
    }

    printf("cpu    load"
           " sched (cs ylds pmpts irq_pmpts)"
           " excep"
           " pagef"
           "  sysc"
           " ints (hw  tmr tmr_cb)"
           " ipi (rs  gen)\n");
    for (size_t i = 0; i < actual; i++) {
        mx_time_t idle_time = stats[i].idle_time;

        mx_time_t delta_time = idle_time - last_idle_time[i];
        mx_time_t busy_time = delay - (delta_time > delay ? delay : delta_time);
        unsigned int busypercent = (busy_time * 10000) / delay;

        printf("%3zu"
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
               stats[i].context_switches - old_stats[i].context_switches,
               stats[i].yields - old_stats[i].yields,
               stats[i].preempts - old_stats[i].preempts,
               stats[i].irq_preempts - old_stats[i].irq_preempts,
               stats[i].exceptions - old_stats[i].exceptions,
               stats[i].page_faults - old_stats[i].page_faults,
               stats[i].syscalls - old_stats[i].syscalls,
               stats[i].ints - old_stats[i].ints,
               stats[i].timer_ints - old_stats[i].timer_ints,
               stats[i].timers - old_stats[i].timers,
               stats[i].reschedule_ipis - old_stats[i].reschedule_ipis,
               stats[i].generic_ipis - old_stats[i].generic_ipis);

        old_stats[i] = stats[i];
        last_idle_time[i] = idle_time;
    }

    return NO_ERROR;
}

static void print_help(FILE* f) {
    fprintf(f, "Usage: kstats [options]\n");
    fprintf(f, "Options:\n");
    fprintf(f, " -c              Print system CPU stats (default)\n");
    fprintf(f, " -d <delay>      Delay in seconds (default 1 second)\n");
    fprintf(f, "\nCPU stats columns:\n");
    fprintf(f, "\tcpu:  cpu #\n");
    fprintf(f, "\tload: percentage load\n");
    fprintf(f, "\tsched (cs ylds pmpts irq_pmpts): scheduler statistics\n");
    fprintf(f, "\t\tcs:        context switches\n");
    fprintf(f, "\t\tylds:      explicit thread yields\n");
    fprintf(f, "\t\tpmpts:     thread preemption events\n");
    fprintf(f, "\t\tirq_pmpts: thread preemption events from interrupt\n");

    fprintf(f, "\texcep: exceptions (undefined instruction, bad memory access, etc)\n");
    fprintf(f, "\tpagef: page faults\n");
    fprintf(f, "\tsysc:  syscalls\n");
    fprintf(f, "\tints (hw  tmr tmr_cb): interrupt statistics\n");
    fprintf(f, "\t\thw:     hardware interrupts\n");
    fprintf(f, "\t\ttmr:    timer interrupts\n");
    fprintf(f, "\t\ttmr_cb: kernel timer events\n");
    fprintf(f, "\tipi (rs  gen): inter-processor-interrupts\n");
    fprintf(f, "\t\trs:     reschedule events\n");
    fprintf(f, "\t\tgen:    generic interprocessor interrupts\n");
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            print_help(stdout);
            return 0;
        }
        if (!strcmp(arg, "-c")) {
            cpu_stats = true;
        } else if (!strcmp(arg, "-d")) {
            delay = 0;
            if (i + 1 < argc) {
                delay = MX_SEC(atoi(argv[i + 1]));
            }
            if (delay == 0) {
                fprintf(stderr, "Bad delay\n");
                print_help(stderr);
                return 1;
            }
            i++;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_help(stderr);
            return 1;
        }
    }

    int fd = open("/dev/misc/sysinfo", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "cannot open sysinfo: %d\n", errno);
        return ERR_NOT_FOUND;
    }

    mx_handle_t root_resource;
    size_t n = ioctl_sysinfo_get_root_resource(fd, &root_resource);
    close(fd);
    if (n != sizeof(root_resource)) {
        fprintf(stderr, "cannot obtain root resource\n");
        return ERR_NOT_FOUND;
    }

    // set stdin to non blocking
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    mx_status_t ret = NO_ERROR;
    for (;;) {
        mx_time_t next_deadline = mx_deadline_after(delay);

        ret = cpustats(root_resource);

        if (ret != NO_ERROR)
            break;

        // TODO: replace once ctrl-c works in the shell
        char c;
        int err;
        while ((err = read(STDIN_FILENO, &c, 1)) > 0) {
            if (c == 0x3)
                return 0;
        }

        mx_nanosleep(next_deadline);
    }

    mx_handle_close(root_resource);

    return ret;
}
