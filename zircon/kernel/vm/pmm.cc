// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/pmm.h"

#include <assert.h>
#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <lib/ktrace.h>
#include <platform.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <new>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <lk/init.h>
#include <vm/bootalloc.h>
#include <vm/physmap.h>
#include <vm/pmm_checker.h>
#include <vm/scanner.h>
#include <vm/vm.h>

#include "pmm_arena.h"
#include "pmm_node.h"
#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

// Number of bytes available in the PMM after kernel init, but before userspace init.
KCOUNTER(boot_memory_bytes, "boot.memory.post_init_free_bytes")

// The (currently) one and only pmm node
static PmmNode pmm_node;

static void pmm_fill_free_pages(uint level) { pmm_node.FillFreePagesAndArm(); }
LK_INIT_HOOK(pmm_fill, &pmm_fill_free_pages, LK_INIT_LEVEL_VM)

vm_page_t* paddr_to_vm_page(paddr_t addr) { return pmm_node.PaddrToPage(addr); }

zx_status_t pmm_add_arena(const pmm_arena_info_t* info) { return pmm_node.AddArena(info); }

size_t pmm_num_arenas() { return pmm_node.NumArenas(); }

zx_status_t pmm_get_arena_info(size_t count, uint64_t i, pmm_arena_info_t* buffer,
                               size_t buffer_size) {
  return pmm_node.GetArenaInfo(count, i, buffer, buffer_size);
}

zx_status_t pmm_alloc_page(uint alloc_flags, paddr_t* pa) {
  VM_KTRACE_DURATION(3, "pmm_alloc_page");
  return pmm_node.AllocPage(alloc_flags, nullptr, pa);
}

zx_status_t pmm_alloc_page(uint alloc_flags, vm_page_t** page) {
  VM_KTRACE_DURATION(3, "pmm_alloc_page");
  return pmm_node.AllocPage(alloc_flags, page, nullptr);
}

zx_status_t pmm_alloc_page(uint alloc_flags, vm_page_t** page, paddr_t* pa) {
  VM_KTRACE_DURATION(3, "pmm_alloc_page");
  return pmm_node.AllocPage(alloc_flags, page, pa);
}

zx_status_t pmm_alloc_pages(size_t count, uint alloc_flags, list_node* list) {
  VM_KTRACE_DURATION(3, "pmm_alloc_pages");
  return pmm_node.AllocPages(count, alloc_flags, list);
}

zx_status_t pmm_alloc_range(paddr_t address, size_t count, list_node* list) {
  VM_KTRACE_DURATION(3, "pmm_alloc_range");
  return pmm_node.AllocRange(address, count, list);
}

zx_status_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t alignment_log2,
                                 paddr_t* pa, list_node* list) {
  VM_KTRACE_DURATION(3, "pmm_alloc_contiguous");
  // if we're called with a single page, just fall through to the regular allocation routine
  if (unlikely(count == 1 && alignment_log2 <= PAGE_SIZE_SHIFT)) {
    vm_page_t* page;
    zx_status_t status = pmm_node.AllocPage(alloc_flags, &page, pa);
    if (status != ZX_OK) {
      return status;
    }
    list_add_tail(list, &page->queue_node);
    return ZX_OK;
  }

  return pmm_node.AllocContiguous(count, alloc_flags, alignment_log2, pa, list);
}

void pmm_alloc_pages(uint alloc_flags, page_request_t* req) {
  VM_KTRACE_DURATION(3, "pmm_alloc_pages");
  pmm_node.AllocPages(alloc_flags, req);
}

bool pmm_clear_request(page_request_t* req) { return pmm_node.ClearRequest(req); }

void pmm_swap_request(page_request_t* old, page_request_t* new_req) {
  pmm_node.SwapRequest(old, new_req);
}

void pmm_free(list_node* list) {
  VM_KTRACE_DURATION(3, "pmm_free");
  pmm_node.FreeList(list);
}

void pmm_free_page(vm_page* page) {
  VM_KTRACE_DURATION(3, "pmm_free_page");
  pmm_node.FreePage(page);
}

uint64_t pmm_count_free_pages() { return pmm_node.CountFreePages(); }

uint64_t pmm_count_total_bytes() { return pmm_node.CountTotalBytes(); }

PageQueues* pmm_page_queues() { return pmm_node.GetPageQueues(); }

Evictor* pmm_evictor() { return pmm_node.GetEvictor(); }

zx_status_t pmm_init_reclamation(const uint64_t* watermarks, uint8_t watermark_count,
                                 uint64_t debounce, void* context,
                                 mem_avail_state_updated_callback_t callback) {
  return pmm_node.InitReclamation(watermarks, watermark_count, debounce, context, callback);
}

void pmm_checker_check_all_free_pages() { pmm_node.CheckAllFreePages(); }

#if __has_feature(address_sanitizer)
void pmm_asan_poison_all_free_pages() { pmm_node.PoisonAllFreePages(); }
#endif

int64_t pmm_get_alloc_failed_count() { return PmmNode::get_alloc_failed_count(); }

static void pmm_checker_enable(size_t fill_size, PmmChecker::Action action) {
  // We might be changing the fill size.  If we increase the fill size while the checker is active,
  // we might spuriously assert so disable the checker.
  pmm_node.DisableChecker();

  // Enable filling of pages going forward.
  pmm_node.EnableFreePageFilling(fill_size, action);

  // From this point on, pages will be filled when they are freed.  However, the free list may still
  // have a lot of unfilled pages so make a pass over them and fill them all.
  pmm_node.FillFreePagesAndArm();

  // All free pages have now been filled with |fill_size| and the checker is armed.
}

static void pmm_checker_disable() { pmm_node.DisableChecker(); }

static bool pmm_checker_is_enabled() { return pmm_node.Checker()->IsArmed(); }

static void pmm_checker_print_status() { pmm_node.Checker()->PrintStatus(stdout); }

void pmm_checker_init_from_cmdline() {
  bool enabled = gBootOptions->pmm_checker_enabled;
  if (enabled) {
    size_t fill_size = gBootOptions->pmm_checker_fill_size;
    if (!PmmChecker::IsValidFillSize(fill_size)) {
      printf("PMM: value from %s is invalid (%lu), using PAGE_SIZE instead\n",
             kPmmCheckerFillSizeName.data(), fill_size);
      fill_size = PAGE_SIZE;
    }

    PmmChecker::Action action = PmmChecker::DefaultAction;
    const char* const action_string = gBootOptions->pmm_checker_action.data();
    if (action_string != nullptr) {
      if (auto opt_action = PmmChecker::ActionFromString(action_string)) {
        action = opt_action.value();
      } else {
        printf("PMM: value from %s is invalid (\"%s\"), using \"%s\" instead\n",
               kPmmCheckerActionName.data(), action_string, PmmChecker::ActionToString(action));
      }
    }

    pmm_node.EnableFreePageFilling(fill_size, action);
  }
}

static void pmm_dump_timer(Timer* t, zx_time_t now, void*) {
  zx_time_t deadline = zx_time_add_duration(now, ZX_SEC(1));
  t->SetOneshot(deadline, &pmm_dump_timer, nullptr);
  pmm_node.DumpFree();
}

static void init_request_thread(unsigned int level) { pmm_node.InitRequestThread(); }

LK_INIT_HOOK(pmm, init_request_thread, LK_INIT_LEVEL_THREADING)

LK_INIT_HOOK(
    pmm_boot_memory,
    [](unsigned int /*level*/) {
      // Track the amount of free memory available in the PMM after kernel init, but before
      // userspace starts.
      //
      // We record this in a kcounter to be tracked by build infrastructure over time.
      dprintf(INFO, "Free memory after kernel init: %" PRIu64 " bytes.\n",
              pmm_node.CountFreePages() * PAGE_SIZE);
      boot_memory_bytes.Set(pmm_node.CountFreePages() * PAGE_SIZE);
    },
    LK_INIT_LEVEL_USER - 1)

static Timer dump_free_mem_timer;

static int cmd_pmm(int argc, const cmd_args* argv, uint32_t flags) {
  const bool is_panic = flags & CMD_FLAG_PANIC;
  auto usage = [cmd_name = argv[0].str, is_panic]() -> int {
    printf("usage:\n");
    printf("%s dump                                     : dump pmm info \n", cmd_name);
    if (!is_panic) {
      printf("%s free                                     : periodically dump free mem count\n",
             cmd_name);
      printf(
          "%s oom [<rate>]                             : leak memory until oom is triggered, "
          "optionally specify the rate at which to leak (in MB per second)\n",
          cmd_name);
      printf(
          "%s oom hard                                 : leak memory aggressively and keep on "
          "leaking\n",
          cmd_name);
      printf(
          "%s oom signal                               : trigger oom signal without leaking "
          "memory\n",
          cmd_name);
      printf("%s mem_avail_state info                     : dump memory availability state info\n",
             cmd_name);
      printf(
          "%s mem_avail_state [step] <state> [<nsecs>] : allocate memory to go to memstate "
          "<state>, hold the state for <nsecs> (10s by default). Only works if going to <state> "
          "from current state requires allocating memory, can't free up pre-allocated memory. In "
          "optional [step] mode, allocation pauses for 1 second at each intermediate memory "
          "availability state until <state> is reached.\n",
          cmd_name);
      printf("%s drop_user_pt                             : drop all user hardware page tables\n",
             cmd_name);
      printf("%s checker status                           : prints the status of the pmm checker\n",
             cmd_name);
      printf(
          "%s checker enable [<size>] [oops|panic]     : enables the pmm checker with optional "
          "fill size and optional action\n",
          cmd_name);
      printf("%s checker disable                          : disables the pmm checker\n", cmd_name);
      printf(
          "%s checker check                            : forces a check of all free pages in the "
          "pmm\n",
          cmd_name);
    }
    return ZX_ERR_INTERNAL;
  };

  if (argc < 2) {
    printf("not enough arguments\n");
    return usage();
  }

  if (!strcmp(argv[1].str, "dump")) {
    pmm_node.Dump(is_panic);
  } else if (is_panic) {
    // No other operations will work during a panic.
    printf("Only the \"arenas\" command is available during a panic.\n");
    return usage();
  } else if (!strcmp(argv[1].str, "free")) {
    static bool show_mem = false;

    if (!show_mem) {
      printf("pmm free: issue the same command to stop.\n");
      zx_time_t deadline = zx_time_add_duration(current_time(), ZX_SEC(1));
      const TimerSlack slack{ZX_MSEC(20), TIMER_SLACK_CENTER};
      const Deadline slackDeadline(deadline, slack);
      dump_free_mem_timer.Set(slackDeadline, &pmm_dump_timer, nullptr);
      show_mem = true;
    } else {
      dump_free_mem_timer.Cancel();
      show_mem = false;
    }
  } else if (!strcmp(argv[1].str, "oom")) {
    if (argc > 3) {
      return usage();
    }

    uint64_t rate = 0;
    bool hard = false;
    if (argc > 2) {
      if (!strcmp(argv[2].str, "signal")) {
        pmm_node.DebugMemAvailStateCallback(0);
        return ZX_OK;
      }
      if (!strcmp(argv[2].str, "hard")) {
        hard = true;
      } else {
        rate = strtoul(argv[2].str, nullptr, 0) * 1024 * 1024 / PAGE_SIZE;
      }
    }

    // When we reach the oom state the kernel may 'try harder' to reclaim memory and prevent us from
    // hitting oom. To avoid this we disable the scanner to prevent additional memory from becoming
    // classified as evictable, and then evict anything that is already considered.
    printf("Disabling VM scanner\n");
    scanner_push_disable_count();
    uint64_t pages_evicted = pmm_evictor()->EvictOneShotSynchronous(
        UINT64_MAX, Evictor::EvictionLevel::IncludeNewest, Evictor::Output::NoPrint);
    if (pages_evicted > 0) {
      printf("Leaked %" PRIu64 " pages from eviction\n", pages_evicted);
    }

    uint64_t pages_till_oom;
    // In case we are racing with someone freeing pages we will leak in a loop until we are sure
    // we have hit the oom state.
    while ((pages_till_oom = pmm_node.DebugNumPagesTillMemState(0)) > 0) {
      list_node list = LIST_INITIAL_VALUE(list);
      if (rate > 0) {
        uint64_t pages_leaked = 0;
        while (pages_leaked < pages_till_oom) {
          uint64_t alloc_pages = ktl::min(rate, pages_till_oom - pages_leaked);
          if (pmm_node.AllocPages(alloc_pages, 0, &list) == ZX_OK) {
            pages_leaked += alloc_pages;
            printf("Leaked %lu pages\n", pages_leaked);
          }
          Thread::Current::SleepRelative(ZX_SEC(1));
        }
      } else {
        if (pmm_node.AllocPages(pages_till_oom, 0, &list) == ZX_OK) {
          printf("Leaked %lu pages\n", pages_till_oom);
        }
      }
      // Ignore any errors under the assumption we had a racy allocation and try again next time
      // around the loop.
    }

    if (hard) {
      printf("Continuing to leak pages forever\n");
      // Keep leaking as fast possible.
      while (true) {
        vm_page_t* p;
        pmm_alloc_page(0, &p);
      }
    }
  } else if (!strcmp(argv[1].str, "mem_avail_state")) {
    if (argc < 3) {
      return usage();
    }
    if (!strcmp(argv[2].str, "info")) {
      pmm_node.DumpMemAvailState();
    } else {
      bool step = false;
      int index = 2;
      if (!strcmp(argv[2].str, "step")) {
        step = true;
        index++;
      }

      uint8_t state = static_cast<uint8_t>(argv[index++].u);
      if (state > pmm_node.DebugMaxMemAvailState()) {
        printf("Invalid memstate %u. Specify a value between 0 and %u.\n", state,
               pmm_node.DebugMaxMemAvailState());
        return usage();
      }

      uint64_t pages_to_alloc, pages_to_free = 0;
      list_node list = LIST_INITIAL_VALUE(list);

      if (step) {
        uint8_t s = pmm_node.DebugMaxMemAvailState();
        while (true) {
          // In case we are racing with someone freeing pages we will leak in a loop until we are
          // sure we have hit the required memory availability state.
          uint64_t pages_allocated = 0;
          while ((pages_to_alloc = pmm_node.DebugNumPagesTillMemState(s)) > 0) {
            if (pmm_node.AllocPages(pages_to_alloc, 0, &list) == ZX_OK) {
              printf("Leaked %lu pages\n", pages_to_alloc);
              pages_allocated += pages_to_alloc;
            }
          }
          pages_to_free += pages_allocated;
          if (s == state) {
            break;
          }
          s--;
          if (pages_allocated) {
            printf("Sleeping for 1 second...\n");
            Thread::Current::SleepRelative(ZX_SEC(1));
          }
        }
      } else {
        // In case we are racing with someone freeing pages we will leak in a loop until we are
        // sure we have hit the required memory availability state.
        while ((pages_to_alloc = pmm_node.DebugNumPagesTillMemState(state)) > 0) {
          if (pmm_node.AllocPages(pages_to_alloc, 0, &list) == ZX_OK) {
            printf("Leaked %lu pages\n", pages_to_alloc);
            pages_to_free += pages_to_alloc;
          }
        }
      }

      if (pages_to_free > 0) {
        uint64_t nsecs = 10;
        if (argc > index) {
          nsecs = argv[index].u;
        }
        printf("Sleeping for %lu seconds...\n", nsecs);
        Thread::Current::SleepRelative(ZX_SEC(nsecs));
        pmm_node.FreeList(&list);
        printf("Freed %lu pages\n", pages_to_free);
      }
    }
  } else if (!strcmp(argv[1].str, "drop_user_pt")) {
    VmAspace::DropAllUserPageTables();
  } else if (!strcmp(argv[1].str, "checker")) {
    if (argc < 3 || argc > 5) {
      return usage();
    }
    if (!strcmp(argv[2].str, "status")) {
      pmm_checker_print_status();
    } else if (!strcmp(argv[2].str, "enable")) {
      size_t fill_size = PAGE_SIZE;
      PmmChecker::Action action = PmmChecker::DefaultAction;
      if (argc >= 4) {
        fill_size = argv[3].u;
        if (!PmmChecker::IsValidFillSize(fill_size)) {
          printf(
              "error: fill size must be a multiple of 8 and be between 8 and PAGE_SIZE, "
              "inclusive\n");
          return ZX_ERR_INTERNAL;
        }
      }
      if (argc == 5) {
        if (auto opt_action = PmmChecker::ActionFromString(argv[4].str)) {
          action = opt_action.value();
        } else {
          printf("error: invalid action\n");
          return ZX_ERR_INTERNAL;
        }
      }
      pmm_checker_enable(fill_size, action);
      // No need to print status as enabling automatically prints status.
    } else if (!strcmp(argv[2].str, "disable")) {
      pmm_checker_disable();
      pmm_checker_print_status();
    } else if (!strcmp(argv[2].str, "check")) {
      if (!pmm_checker_is_enabled()) {
        printf("error: pmm checker is not enabled\n");
        return ZX_ERR_INTERNAL;
      }
      printf("checking all free pages...\n");
      pmm_checker_check_all_free_pages();
      printf("done\n");
    } else {
      return usage();
    }
  } else {
    printf("unknown command\n");
    return usage();
  }

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("pmm", "physical memory manager", &cmd_pmm, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(pmm)
