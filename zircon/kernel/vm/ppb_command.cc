// Copyright 2021 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <kernel/event.h>
#include <lib/console.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <ktl/iterator.h>
#include <vm/pmm.h>
#include <vm/ppb_config.h>
#include <vm/loan_sweeper.h>

// DO NOT SUBMIT - TODO:
//  * gather some stats / info / data
//  * maybe optimize PageListNode loaned tracking / backlink upkeep avoidance / combined/faster merge
//  * graphs over time, somehow
//  * stress test from user mode with overlapped commits / decommits / usage of loaned pages.

static constexpr uint64_t kBInMiB = 1024 * 1024;

static int cmd_ppb(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND("ppb", "control contiguous physical page borrowing", &cmd_ppb)
STATIC_COMMAND_END(kernel)

DECLARE_SINGLETON_MUTEX(ppb_stats_lock);
static Thread *ppb_stats_thread TA_GUARDED(ppb_stats_lock::Get()) = nullptr;
static Event ppb_stats_thread_stop_event(false);

static void cmd_ppb_on() {  
  pmm_ppb_config()->set_enabled(true);
  printf("borrowing enabled\n");
}

static void cmd_ppb_off() {  
  pmm_ppb_config()->set_enabled(false);
  printf("borrowing disabled\n");
}

static void cmd_ppb_non_pager_on() {  
  pmm_ppb_config()->set_non_pager_enabled(true);
  printf("non-pager-backed borrowing enabled\n");
}

static void cmd_ppb_non_pager_off() {  
  pmm_ppb_config()->set_non_pager_enabled(false);
  printf("non-pager-backed borrowing disabled\n");
}

static void cmd_ppb_sweep() {  
  uint64_t freed_page_count = pmm_loan_sweeper()->ForceSynchronousSweep(/*is_continuous_sweep=*/false, /*also_replace_recently_pinned=*/true);
  printf("freed_page_count: %" PRIu64 " freed MiB: %" PRIu64 "\n", freed_page_count, freed_page_count * PAGE_SIZE / kBInMiB);
}

static void cmd_ppb_low_mem_sweeping_on() {  
  pmm_ppb_config()->set_low_mem_sweeping_enabled(true);
  printf("low mem sweeping enabled\n");
}

static void cmd_ppb_low_mem_sweeping_off() {  
  pmm_ppb_config()->set_low_mem_sweeping_enabled(false);
  printf("low mem sweeping disabled\n");
}

static void cmd_ppb_stats() {
  uint64_t free_pages = pmm_count_free_pages();
  uint64_t loaned_free_pages = pmm_count_loaned_free_pages();
  uint64_t loaned_pages = pmm_count_loaned_pages();
  uint64_t loan_cancelled_pages = pmm_count_loan_cancelled_pages();
  uint64_t total_bytes = pmm_count_total_bytes();
  uint64_t used_loaned_pages = pmm_count_loaned_used_pages();
  printf("PPB stats:\n"
         "  free pages: %" PRIu64 " free MiB: %" PRIu64 "\n"
         "  loaned free pages: %" PRIu64 " loaned free MiB: %" PRIu64 "\n"
         "  loaned pages: %" PRIu64 " loaned MiB: %" PRIu64 "\n"
         "  used loaned pages: %" PRIu64 " used loaned MiB: %" PRIu64 "\n"
         "  loan cancelled pages: %" PRIu64 " loan cancelled MIB: %" PRIu64 "\n"
         "  total physical pages: %" PRIu64 " total MiB: %" PRIu64 "\n",
         free_pages, free_pages * PAGE_SIZE / kBInMiB,
         loaned_free_pages, loaned_free_pages * PAGE_SIZE / kBInMiB,
         loaned_pages, loaned_pages * PAGE_SIZE / kBInMiB,
         used_loaned_pages, used_loaned_pages * PAGE_SIZE / kBInMiB,
         loan_cancelled_pages, loan_cancelled_pages * PAGE_SIZE / kBInMiB,
         total_bytes / PAGE_SIZE, total_bytes / kBInMiB);
}

static void cmd_ppb_stats_on() {
  Thread* local_ppb_stats_thread;
  {  // scope guard
    Guard<Mutex> guard(ppb_stats_lock::Get());
    auto stats_thread = [](void* arg) -> int {
      while (true) {
        cmd_ppb_stats();
        zx_status_t status = ppb_stats_thread_stop_event.Wait(Deadline::after(ZX_SEC(1)));
        if (status == ZX_OK) {
          return 0;
        }
        DEBUG_ASSERT(status == ZX_ERR_TIMED_OUT);
      }
    };
    ppb_stats_thread = Thread::Create("ppb-stats-thread", stats_thread, nullptr, LOW_PRIORITY);
    ASSERT(ppb_stats_thread);
    local_ppb_stats_thread = ppb_stats_thread;
  }  // ~guard
  local_ppb_stats_thread->Resume();
}

static void cmd_ppb_stats_off() {
  Thread* local_ppb_stats_thread;
  {  // scope guard
    Guard<Mutex> guard(ppb_stats_lock::Get());
    local_ppb_stats_thread = ppb_stats_thread;
    ppb_stats_thread = nullptr;
  }  // ~guard
  ppb_stats_thread_stop_event.Signal();
  int retcode;
  local_ppb_stats_thread->Join(&retcode, ZX_TIME_INFINITE);
  DEBUG_ASSERT(!retcode);
  ppb_stats_thread_stop_event.Unsignal();
}

using CmdFunc = void (*)();
struct Cmd {
  const char* name;
  CmdFunc func;
};

Cmd commands[] = {
  {"on", cmd_ppb_on},
  {"off", cmd_ppb_off},
  {"non_pager_on", cmd_ppb_non_pager_on},
  {"non_pager_off", cmd_ppb_non_pager_off},
  {"sweep", cmd_ppb_sweep},
  {"low_mem_sweeping_on", cmd_ppb_low_mem_sweeping_on},
  {"low_mem_sweeping_off", cmd_ppb_low_mem_sweeping_off},
  {"stats", cmd_ppb_stats},
  {"stats_on", cmd_ppb_stats_on},
  {"stats_off", cmd_ppb_stats_off},
};

// k ppb on
//   * this is the default on boot
//   * enables page borrowing for new allocations (does not sweep)
//   * see also k ppb off
// k ppb off
//   * disables page borrowing for new allocations (does not sweep)
//   * see also k ppb on
// k ppb non_pager_on
//   * this is the default on boot (for now)
//   * only has an effect when ppb on
//   * enables borrowing by non-pager-backed VMOs in addition to pager-backed VMOs (does not sweep)
//   * see also k ppb non_pager_off
// k ppb non_pager_off
//   * disables borrowing by non-pager-backed VMOs, for new allocations (does not sweep)
//   * this does not take effect in the next sweep; already-loaned pages in non-pager VMOs stay
//     unless all loaned page usage is swept away by k ppb off then k ppb sweep.
// k ppb sweep
//   * If ppb is on, borrows as many pages as possible in a single sweep
//   * If ppb is off, un-borrows all borrowed pages (may cause OOM, depending)
//   * The sweep also respects non_pager_on / non_pager_off, etc
// k ppb low_mem_sweeping_on
//   * enable continuous sweeping during OOM WARNING or worse (the default)
// k ppb low_mem_sweeping_off
//   * disable continuous sweeping during OOM WARNING or worse
// k ppb stats
//   * output ppb-related stats (once)
// k ppb stats_on
//   * repeatedly output ppb-relevant stats (fairly frequently, for observing usage scenarios)
// k ppb stats_off
//   * stop repeatedly outputting ppb-relevant stats
static int cmd_ppb(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc != 2) {
    printf("2 arguments expected\n");
    printf("usage:\n");
    printf("ppb <cmd>\n");
    printf("command list:\n");
    for (auto& cmd : commands) {
      printf("%s\n", cmd.name);
    }
    return -1;
  }

  for (auto& cmd : commands) {
    if (!strcmp(argv[1].str, cmd.name)) {
      cmd.func();
      return 0;
    }
  }
  printf("sub-command not found - available sub-commands:\n");
  for (auto& cmd : commands) {
    printf("%s\n", cmd.name);
  }
  return -1;
}
