// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <pretty/sizes.h>

#include "resources.h"

// TODO: dynamically compute this based on what it returns
#define MAX_CPUS 32

static zx_status_t gueststats(zx_handle_t root_resource, zx_duration_t delay) {
  static zx_info_guest_stats_t old_stats[MAX_CPUS];
  zx_info_guest_stats_t stats[MAX_CPUS];

  size_t actual, avail;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_GUEST_STATS, &stats, sizeof(stats),
                                       &actual, &avail);
  if (err != ZX_OK) {
    fprintf(stderr, "ZX_INFO_GUEST_STATS returns %d (%s)\n", err, zx_status_get_string(err));
    return err;
  }

  if (actual < avail) {
    fprintf(stderr, "WARNING: actual cpus reported %zu less than available cpus %zu\n", actual,
            avail);
  }

#ifdef __aarch64__
  printf(
      "cpu   "
      " vm_entry"
      " vm_exit"
      " inst_abt"
      " data_abt"
      " wfx_inst"
      " sys_inst"
      " smc_inst"
      " ints\n");

  for (size_t i = 0; i < actual; i++) {
    printf(
        "%3zu"
        " %9lu"
        " %7lu"
        " %8lu"
        " %8lu"
        " %7lu"
        " %7lu"
        " %7lu"
        " %7lu"
        "\n",
        i, stats[i].vm_entries - old_stats[i].vm_entries, stats[i].vm_exits - old_stats[i].vm_exits,
        stats[i].instruction_aborts - old_stats[i].instruction_aborts,
        stats[i].data_aborts - old_stats[i].data_aborts,
        stats[i].wfi_wfe_instructions - old_stats[i].wfi_wfe_instructions,
        stats[i].system_instructions - old_stats[i].system_instructions,
        stats[i].smc_instructions - old_stats[i].smc_instructions,
        stats[i].interrupts - old_stats[i].interrupts);

    old_stats[i] = stats[i];
  }
#elif defined(__riscv)
  printf(
      "cpu   "
      " vm_entry"
      " vm_exit\n");

  for (size_t i = 0; i < actual; i++) {
    printf(
        "%3zu"
        " %7lu"
        " %7lu",
        i, stats[i].vm_entries - old_stats[i].vm_entries,
	stats[i].vm_exits - old_stats[i].vm_exits);

    old_stats[i] = stats[i];
  }
#else
  printf(
      "cpu   "
      " vm_entry"
      " vm_exit"
      " ints"
      " ints_win"
      " ept"
      " ctrl_reg"
      " msr(rd wr)"
      " inst(io hlt cpuid ple vmcall xsetbv)\n");

  for (size_t i = 0; i < actual; i++) {
    printf(
        "%3zu"
        " %7lu"
        " %7lu"
        " %7lu"
        " %5lu"
        " %6lu"
        " %6lu"
        " %8lu %3lu"
        " %6lu %5lu %5lu %4lu %4lu %6lu\n",
        i, stats[i].vm_entries - old_stats[i].vm_entries, stats[i].vm_exits - old_stats[i].vm_exits,
        stats[i].interrupts - old_stats[i].interrupts,
        stats[i].interrupt_windows - old_stats[i].interrupt_windows,
        stats[i].ept_violations - old_stats[i].ept_violations,
        stats[i].control_register_accesses - old_stats[i].control_register_accesses,
        stats[i].wrmsr_instructions - old_stats[i].wrmsr_instructions,
        stats[i].rdmsr_instructions - old_stats[i].rdmsr_instructions,
        stats[i].io_instructions - old_stats[i].io_instructions,
        stats[i].hlt_instructions - old_stats[i].hlt_instructions,
        stats[i].cpuid_instructions - old_stats[i].cpuid_instructions,
        stats[i].pause_instructions - old_stats[i].pause_instructions,
        stats[i].vmcall_instructions - old_stats[i].vmcall_instructions,
        stats[i].xsetbv_instructions - old_stats[i].xsetbv_instructions);

    old_stats[i] = stats[i];
  }
#endif
  return ZX_OK;
}

static zx_status_t cpustats(zx_handle_t root_resource, zx_duration_t delay) {
  static zx_duration_t last_idle_time[MAX_CPUS];
  static zx_info_cpu_stats_t old_stats[MAX_CPUS];
  zx_info_cpu_stats_t stats[MAX_CPUS];

  // retrieve the system stats
  size_t actual, avail;
  zx_status_t err =
      zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats, sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    fprintf(stderr, "ZX_INFO_CPU_STATS returns %d (%s)\n", err, zx_status_get_string(err));
    return err;
  }

  if (actual < avail) {
    fprintf(stderr, "WARNING: actual cpus reported %zu less than available cpus %zu\n", actual,
            avail);
  }

  printf(
      "cpu    load"
      " sched (cs ylds pmpts irq_pmpts)"
      " excep"
      " pagef"
      "  sysc"
      " ints (hw  tmr tmr_cb)"
      " ipi (rs  gen)\n");
  for (size_t i = 0; i < actual; i++) {
    zx_duration_t idle_time = stats[i].idle_time;

    zx_duration_t delta_time = zx_duration_sub_duration(idle_time, last_idle_time[i]);
    zx_duration_t busy_time;
    if (delay > delta_time) {
      busy_time = zx_duration_sub_duration(delay, delta_time);
    } else {
      busy_time = 0;
    }
    unsigned int busypercent = zx_duration_mul_int64(busy_time, 10000) / delay;

    printf(
        "%3zu"
        " %3u.%02u%%"
        " %9lu %4lu %5lu %9lu"
        " %6lu"
        " %5lu"
        " %5lu"
        " %8lu %4lu %6lu"
        " %8lu %4lu"
        "\n",
        i, busypercent / 100, busypercent % 100,
        stats[i].context_switches - old_stats[i].context_switches,
        stats[i].yields - old_stats[i].yields, stats[i].preempts - old_stats[i].preempts,
        stats[i].irq_preempts - old_stats[i].irq_preempts,
        stats[i].exceptions - old_stats[i].exceptions,
        stats[i].page_faults - old_stats[i].page_faults, stats[i].syscalls - old_stats[i].syscalls,
        stats[i].ints - old_stats[i].ints, stats[i].timer_ints - old_stats[i].timer_ints,
        stats[i].timers - old_stats[i].timers,
        stats[i].reschedule_ipis - old_stats[i].reschedule_ipis,
        stats[i].generic_ipis - old_stats[i].generic_ipis);

    old_stats[i] = stats[i];
    last_idle_time[i] = idle_time;
  }

  return ZX_OK;
}

static zx_status_t cpuload(zx_handle_t root_resource, zx_duration_t delay) {
  static zx_duration_t last_idle_time[MAX_CPUS];
  zx_info_cpu_stats_t stats[MAX_CPUS];

  // retrieve the system stats
  size_t actual, avail;
  zx_status_t err =
      zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats, sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    fprintf(stderr, "ZX_INFO_CPU_STATS returns %d (%s)\n", err, zx_status_get_string(err));
    return err;
  }

  if (actual < avail) {
    fprintf(stderr, "WARNING: actual cpus reported %zu less than available cpus %zu\n", actual,
            avail);
  }

  for (size_t i = 0; i < actual; i++) {
    zx_duration_t idle_time = stats[i].idle_time;

    zx_duration_t delta_time = zx_duration_sub_duration(idle_time, last_idle_time[i]);
    zx_duration_t busy_time;
    if (delay > delta_time) {
      busy_time = zx_duration_sub_duration(delay, delta_time);
    } else {
      busy_time = 0;
    }
    const double busypercent = (double)busy_time / delay;

    static const char kBar[] = "||||||||||||||||||||";
    static const int kBarLength = sizeof(kBar);

    static const char* default_color = "\033[0;0m";
    static const char* cpu_num_color = "\033[1;34m";
    const char* color = busypercent < .9 ? "\033[1;34m" : "\033[1;31m";

    printf("%s%2zu%s-[%s%-20.*s%s] ", cpu_num_color, i, default_color, color,
           (int)(busypercent * kBarLength), kBar, default_color);
    if ((i % 4) == 3) {
      printf("\n");
    }

    last_idle_time[i] = idle_time;
  }
  if (actual % 4 != 3) {
    printf("\n");
  }
  return ZX_OK;
}

static zx_status_t memstats(zx_handle_t root_resource) {
  zx_info_kmem_stats_t stats;
  zx_status_t err =
      zx_object_get_info(root_resource, ZX_INFO_KMEM_STATS, &stats, sizeof(stats), NULL, NULL);
  if (err != ZX_OK) {
    fprintf(stderr, "ZX_INFO_KMEM_STATS returns %d (%s)\n", err, zx_status_get_string(err));
    return err;
  }

  const int width = 80 / 8 - 1;
  printf("%*s %*s %*s %*s %*s %*s %*s %*s %*s\n", width, "mem total", width, "free", width, "VMOs",
         width, "kheap", width, "kfree", width, "wired", width, "mmu", width, "ipc", width,
         "other");

  const size_t fields[] = {
      stats.total_bytes,        stats.free_bytes,
      stats.vmo_bytes,          stats.total_heap_bytes - stats.free_heap_bytes,
      stats.free_heap_bytes,    stats.wired_bytes,
      stats.mmu_overhead_bytes, stats.ipc_bytes,
      stats.other_bytes,
  };
  char line[128] = {};
  for (unsigned int i = 0; i < countof(fields); i++) {
    const char unit = 'M';
    char buf[MAX_FORMAT_SIZE_LEN];
    format_size_fixed(buf, sizeof(buf), fields[i], unit);

    char stage[MAX_FORMAT_SIZE_LEN + 8];
    snprintf(stage, sizeof(stage), "%*s ", width, buf);

    strlcat(line, stage, sizeof(line));

    // TODO(dbort): Save some history so we can show deltas over time.
    // Maybe have a few buckets like 1s, 10s, 1m.
  }
  printf("%s\n", line);
  return ZX_OK;
}

static void print_help(FILE* f) {
  fprintf(f, "Usage: kstats [options]\n");
  fprintf(f, "Options:\n");
  fprintf(f, " -v              Print guest vm_entry/vm_exit stats\n");
  fprintf(f, " -c              Print system CPU stats\n");
  fprintf(f, " -l              Print system CPU load as bars\n");
  fprintf(f, " -m              Print system memory stats\n");
  fprintf(f, " -d <delay>      Delay in seconds (default 1 second)\n");
  fprintf(f, " -n <times>      Run this many times and then exit\n");
  fprintf(f, " -t              Print timestamp for each report\n");
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
  bool cpu_stats = false;
  bool guest_stats = false;
  bool cpu_load = false;
  bool mem_stats = false;
  zx_duration_t delay = ZX_SEC(1);
  int num_loops = -1;
  bool timestamp = false;

  int c;
  while ((c = getopt(argc, argv, "cvd:n:hlmt")) > 0) {
    switch (c) {
      case 'v':
        guest_stats = true;
        break;
      case 'c':
        cpu_stats = true;
        break;
      case 'd':
        delay = ZX_SEC(atoi(optarg));
        if (delay == 0) {
          fprintf(stderr, "Bad -d value '%s'\n", optarg);
          print_help(stderr);
          return 1;
        }
        break;
      case 'l':
        cpu_load = true;
        break;
      case 'n':
        num_loops = atoi(optarg);
        if (num_loops == 0) {
          fprintf(stderr, "Bad -n value '%s'\n", optarg);
          print_help(stderr);
          return 1;
        }
        break;
      case 'h':
        print_help(stdout);
        return 0;
      case 'm':
        mem_stats = true;
        break;
      case 't':
        timestamp = true;
        break;
      default:
        fprintf(stderr, "Unknown option\n");
        print_help(stderr);
        return 1;
    }
  }

  if (!cpu_stats && !mem_stats && !cpu_load && !guest_stats) {
    fprintf(stderr, "No statistics selected\n");
    print_help(stderr);
    return 1;
  }

  zx_handle_t root_resource;
  zx_status_t ret = get_root_resource(&root_resource);
  if (ret != ZX_OK) {
    return ret;
  }

  // set stdin to non blocking so we can intercept ctrl-c.
  // TODO: remove once ctrl-c works in the shell
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

  for (;;) {
    zx_time_t next_deadline = zx_deadline_after(delay);

    // Print the current UTC time with milliseconds as
    // an ISO 8601 string.
    if (timestamp) {
      struct timespec now;
      timespec_get(&now, TIME_UTC);
      struct tm nowtm;
      gmtime_r(&now.tv_sec, &nowtm);
      char tbuf[40];
      strftime(tbuf, sizeof(tbuf), "%FT%T", &nowtm);
      printf("\n--- %s.%03ldZ ---\n", tbuf, now.tv_nsec / (1000 * 1000));
    }

    if (cpu_load) {
      ret |= cpuload(root_resource, delay);
    }
    if (guest_stats) {
      ret |= gueststats(root_resource, delay);
    }
    if (cpu_stats) {
      ret |= cpustats(root_resource, delay);
    }
    if (mem_stats) {
      ret |= memstats(root_resource);
    }
    // Separate multiple runs with a blank line.
    printf("\n");

    if (ret != ZX_OK)
      break;

    if (num_loops > 0) {
      if (--num_loops == 0) {
        break;
      }
    } else {
      // TODO: replace once ctrl-c works in the shell
      char c;
      int err;
      while ((err = read(STDIN_FILENO, &c, 1)) > 0) {
        if (c == 0x3)
          return 0;
      }
    }

    zx_nanosleep(next_deadline);
  }

  zx_handle_close(root_resource);

  return ret;
}
