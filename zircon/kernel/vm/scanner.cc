// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <zircon/time.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <lk/init.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

namespace {

constexpr uint32_t kScannerFlagPrint = 1u << 0;
constexpr uint32_t kScannerOpDisable = 1u << 1;
constexpr uint32_t kScannerOpEnable = 1u << 2;
constexpr uint32_t kScannerOpDump = 1u << 3;
constexpr uint32_t kScannerOpReclaimAll = 1u << 4;
constexpr uint32_t kScannerOpRotateQueues = 1u << 5;
constexpr uint32_t kScannerOpReclaim = 1u << 6;
constexpr uint32_t kScannerOpHarvestAccessed = 1u << 7;
constexpr uint32_t kScannerOpEnablePTReclaim = 1u << 8;
constexpr uint32_t kScannerOpDisablePTReclaim = 1u << 9;

// Amount of time between pager queue rotations.
constexpr zx_duration_t kQueueRotateTime = ZX_SEC(10);

// If not set on the cmdline this becomes the default zero page scans per second to target. This
// value was chosen to consume, in the worst case, 5% CPU on a lower-end arm device. Individual
// configurations may wish to tune this higher (or lower) as needed.
constexpr uint64_t kDefaultZeroPageScansPerSecond = 20000;

// A rough percentage of page evictions that should be satisfied from discardable vmos (as opposed
// to pager-backed vmos). Will require tuning when discardable vmos start being used. Currently sets
// the number of discardable pages to evict to 0, putting all the burden of eviction on pager-backed
// pages.
constexpr uint32_t kDefaultDiscardableEvictionsPercent = 0;

uint32_t discardable_evictions_percent = kDefaultDiscardableEvictionsPercent;

// Number of pages to attempt to de-dupe back to zero every second. This not atomic as it is only
// set during init before the scanner thread starts up, at which point it becomes read only.
uint64_t zero_page_scans_per_second = 0;

// Eviction is globally enabled/disabled on startup through the kernel cmdline.
bool eviction_enabled = false;

enum class PageTableReclaim {
  Always,
  Never,
  OnRequest,
};

PageTableReclaim page_table_reclaim_policy = PageTableReclaim::Always;

// Tracks what the scanner should do when it is next woken up.
ktl::atomic<uint32_t> scanner_operation = 0;

// Eviction target state is grouped together behind a lock to allow different threads to safely
// trigger and perform the eviction.
struct EvictionTarget {
  bool pending = false;
  // The desired value to get pmm_count_free_pages() to
  uint64_t free_target_pages = 0;
  // A minimum amount of pages we want to evict, regardless of how much free memory is available.
  uint64_t min_pages_free = 0;
  scanner::EvictionLevel level = scanner::EvictionLevel::OnlyOldest;
};
DECLARE_SINGLETON_SPINLOCK(scanner_eviction_target_lock);
EvictionTarget scanner_eviction_target TA_GUARDED(scanner_eviction_target_lock::Get()) = {};

// Event to signal the scanner thread to wake up and perform work.
AutounsignalEvent scanner_request_event;

// Event that is signaled whenever the scanner is disabled. This is used to synchronize disable
// requests with the scanner thread.
Event scanner_disabled_event;
DECLARE_SINGLETON_MUTEX(scanner_disabled_lock);
uint32_t scanner_disable_count TA_GUARDED(scanner_disabled_lock::Get()) = 0;

KCOUNTER(zero_scan_requests, "vm.scanner.zero_scan.requests")
KCOUNTER(zero_scan_ends_empty, "vm.scanner.zero_scan.queue_emptied")
KCOUNTER(zero_scan_pages_scanned, "vm.scanner.zero_scan.total_pages_considered")
KCOUNTER(zero_scan_pages_deduped, "vm.scanner.zero_scan.pages_deduped")

KCOUNTER(eviction_pages_evicted, "vm.scanner.eviction.pages_evicted")

void scanner_print_stats(zx_duration_t time_till_queue_rotate) {
  uint64_t zero_pages = VmObject::ScanAllForZeroPages(false);
  printf("[SCAN]: Found %lu zero pages across all of memory\n", zero_pages);
  PageQueues::Counts queue_counts = pmm_page_queues()->DebugQueueCounts();
  for (size_t i = 0; i < PageQueues::kNumPagerBacked; i++) {
    printf("[SCAN]: Found %lu user-pager backed pages in queue %zu\n", queue_counts.pager_backed[i],
           i);
  }
  printf("[SCAN]: Found %lu user-pager backed pages in inactive queue\n",
         queue_counts.pager_backed_inactive);
  printf("[SCAN]: Found %lu zero forked pages\n", queue_counts.unswappable_zero_fork);

  VmCowPages::DiscardablePageCounts counts = VmCowPages::DebugDiscardablePageCounts();
  printf("[SCAN]: Found %lu locked pages in discardable vmos\n", counts.locked);
  printf("[SCAN]: Found %lu unlocked pages in discardable vmos\n", counts.unlocked);

  printf("[SCAN]: Next queue rotation in %ld ms\n", time_till_queue_rotate / ZX_MSEC(1));
}

zx_time_t calc_next_zero_scan_deadline(zx_time_t current) {
  return zero_page_scans_per_second > 0 ? zx_time_add_duration(current, ZX_SEC(1))
                                        : ZX_TIME_INFINITE;
}

// Performs a synchronous request to evict the requested number of pager-backed pages. Evicted pages
// are placed in the passed |free_list| and become owned by the caller, with the return value being
// the number of free pages. The |eviction_level| is a rough control that maps to how old a page
// needs to be for being considered for eviction. This may acquire arbitrary vmo and aspace locks.
uint64_t scanner_evict_pager_backed(uint64_t max_pages, scanner::EvictionLevel eviction_level,
                                    list_node_t *free_list) {
  if (!eviction_enabled) {
    return 0;
  }

  uint64_t count = 0;

  while (count < max_pages) {
    // Avoid evicting from the newest queue to prevent thrashing.
    const size_t lowest_evict_queue = eviction_level == scanner::EvictionLevel::IncludeNewest
                                          ? 1
                                          : PageQueues::kNumPagerBacked - 1;
    if (ktl::optional<PageQueues::VmoBacklink> backlink =
            pmm_page_queues()->PeekPagerBacked(lowest_evict_queue)) {
      if (!backlink->cow) {
        continue;
      }
      if (backlink->cow->EvictPage(backlink->page, backlink->offset)) {
        list_add_tail(free_list, &backlink->page->queue_node);
        count++;
      }
    } else {
      break;
    }
  }

  eviction_pages_evicted.Add(count);
  return count;
}

// Performs a synchronous request to evict the requested number of pages from discardable vmos. The
// return value is the number of pages evicted. This may acquire arbitrary vmo and aspace locks.
uint64_t scanner_evict_discardable_vmos(uint64_t max_pages) {
  if (!eviction_enabled) {
    return 0;
  }

  // Reclaim |max_pages| from discardable vmos that have been reclaimable for at least 10 seconds.
  return VmCowPages::ReclaimPagesFromDiscardableVmos(max_pages, ZX_SEC(10));
}

void scanner_do_evict(uint64_t *pages_freed_pager_backed_out,
                      uint64_t *pages_freed_discardable_out) {
  // Create a local copy of the eviction target to operate against.
  EvictionTarget target;
  {
    Guard<SpinLock, IrqSave> guard{scanner_eviction_target_lock::Get()};
    target = scanner_eviction_target;
    scanner_eviction_target = {};
  }
  if (!target.pending) {
    return;
  }

  DEBUG_ASSERT(pages_freed_pager_backed_out);
  DEBUG_ASSERT(pages_freed_discardable_out);
  *pages_freed_pager_backed_out = 0;
  *pages_freed_discardable_out = 0;

  uint64_t total_pages_freed = 0;

  do {
    const uint64_t free_mem = pmm_count_free_pages();
    uint64_t pages_to_free = 0;
    if (total_pages_freed < target.min_pages_free) {
      pages_to_free = target.min_pages_free - total_pages_freed;
    } else if (free_mem < target.free_target_pages) {
      pages_to_free = target.free_target_pages - free_mem;
    } else {
      break;
    }

    DEBUG_ASSERT(discardable_evictions_percent <= 100);

    // Compute the desired number of discardable pages to free (vs pager-backed).
    uint64_t pages_to_free_discardable = pages_to_free * discardable_evictions_percent / 100;

    uint64_t pages_freed = scanner_evict_discardable_vmos(pages_to_free_discardable);
    *pages_freed_discardable_out += pages_freed;
    total_pages_freed += pages_freed;

    // Free pager backed memory to get to |pages_to_free|.
    uint64_t pages_to_free_pager_backed = pages_to_free - pages_freed;

    list_node_t free_list;
    list_initialize(&free_list);
    uint64_t pages_freed_pager_backed =
        scanner_evict_pager_backed(pages_to_free_pager_backed, target.level, &free_list);
    pmm_free(&free_list);
    *pages_freed_pager_backed_out += pages_freed_pager_backed;
    total_pages_freed += pages_freed_pager_backed;

    pages_freed += pages_freed_pager_backed;

    // Should we fail to free any pages then we give up and consider the eviction request complete.
    if (pages_freed == 0) {
      break;
    }
  } while (1);
}

int scanner_request_thread(void *) {
  bool disabled = false;
  bool pt_eviction_enabled = false;
  zx_time_t next_rotate_deadline = zx_time_add_duration(current_time(), kQueueRotateTime);
  zx_time_t next_zero_scan_deadline = calc_next_zero_scan_deadline(current_time());
  while (1) {
    if (disabled) {
      scanner_request_event.Wait(Deadline::infinite());
    } else {
      scanner_request_event.Wait(
          Deadline::no_slack(ktl::min(next_rotate_deadline, next_zero_scan_deadline)));
    }
    int32_t op = scanner_operation.exchange(0);
    // It is possible for enable and disable to happen at the same time. This indicates the disabled
    // count went from 1->0->1 and so we want to remain disabled. We do this by performing the
    // enable step first. We know that the scenario of 0->1->0 is not possible as the 0->1 part of
    // that holds the mutex until complete.
    if (op & kScannerOpEnable) {
      op &= ~kScannerOpEnable;
      disabled = false;
    }
    if (op & kScannerOpDisable) {
      op &= ~kScannerOpDisable;
      disabled = true;
      scanner_disabled_event.Signal();
    }
    if (disabled) {
      // put the remaining ops back and resume waiting.
      scanner_operation.fetch_or(op);
      continue;
    }

    zx_time_t current = current_time();

    if (current >= next_rotate_deadline || (op & kScannerOpRotateQueues)) {
      op &= ~kScannerOpRotateQueues;
      pmm_page_queues()->RotatePagerBackedQueues();
      next_rotate_deadline = zx_time_add_duration(current, kQueueRotateTime);
      // Accessed harvesting currently happens in sync with rotating pager queues.
      op |= kScannerOpHarvestAccessed;
    }

    bool print = false;
    if (op & kScannerFlagPrint) {
      op &= ~kScannerFlagPrint;
      print = true;
    }
    bool reclaim_all = false;
    if (op & kScannerOpReclaimAll) {
      op &= ~kScannerOpReclaimAll;
      reclaim_all = true;
      Guard<SpinLock, IrqSave> guard{scanner_eviction_target_lock::Get()};
      scanner_eviction_target.pending = true;
      scanner_eviction_target.level = scanner::EvictionLevel::IncludeNewest;
      scanner_eviction_target.free_target_pages = UINT64_MAX;
    }
    if ((op & kScannerOpReclaim) || reclaim_all) {
      op &= ~kScannerOpReclaim;
      uint64_t pager_backed = 0, discardable = 0;
      if (print) {
        printf("[SCAN]: Free memory before eviction is %zuMB\n",
               pmm_count_free_pages() * PAGE_SIZE / MB);
      }
      scanner_do_evict(&pager_backed, &discardable);
      if (print) {
        printf("[SCAN]: Evicted %lu user pager backed pages\n", pager_backed);
        printf("[SCAN]: Evicted %lu pages from discardable vmos\n", discardable);
        printf("[SCAN]: Free memory after eviction is %zuMB\n",
               pmm_count_free_pages() * PAGE_SIZE / MB);
      }
    }
    if (op & kScannerOpDump) {
      op &= ~kScannerOpDump;
      scanner_print_stats(zx_time_sub_time(next_rotate_deadline, current));
    }
    if (op & kScannerOpEnablePTReclaim) {
      pt_eviction_enabled = true;
      op &= ~kScannerOpEnablePTReclaim;
    }
    if (op & kScannerOpDisablePTReclaim) {
      pt_eviction_enabled = false;
      op &= ~kScannerOpDisablePTReclaim;
    }
    if (op & kScannerOpHarvestAccessed) {
      op &= ~kScannerOpHarvestAccessed;
      // Determine if our architecture requires us to harvest the terminal accessed bits in order
      // to perform page table reclamation.
      const bool pt_reclaim_harvest_terminal = !ArchVmAspace::HasNonTerminalAccessedFlag() &&
                                               page_table_reclaim_policy != PageTableReclaim::Never;
      // Potentially reclaim any unaccessed user page tables. This must be done before the other
      // accessed bit harvesting, otherwise if we do not have non-terminal accessed flags we will
      // always reclaim everything.
      if (page_table_reclaim_policy != PageTableReclaim::Never) {
        const VmAspace::NonTerminalAction action =
            page_table_reclaim_policy == PageTableReclaim::Always || pt_eviction_enabled
                ? VmAspace::NonTerminalAction::FreeUnaccessed
                : VmAspace::NonTerminalAction::Retain;
        VmAspace::HarvestAllUserPageTables(action);
      }
      // Accessed information for page mappings for VMOs impacts page eviction and page table
      // reclamation. For page table reclamation it is only needed if we do not have non-terminal
      // accessed flags.
      if (pt_reclaim_harvest_terminal || eviction_enabled) {
        VmObject::HarvestAllAccessedBits();
      }
    }
    if (current >= next_zero_scan_deadline || reclaim_all) {
      const uint64_t scan_limit = reclaim_all ? UINT64_MAX : zero_page_scans_per_second;
      const uint64_t pages = scanner_do_zero_scan(scan_limit);
      if (print) {
        printf("[SCAN]: De-duped %lu pages that were recently forked from the zero page\n", pages);
      }
      next_zero_scan_deadline = calc_next_zero_scan_deadline(current);
    }
    DEBUG_ASSERT(op == 0);
  }
  return 0;
}

void scanner_dump_info() {
  Guard<Mutex> guard{scanner_disabled_lock::Get()};
  if (scanner_disable_count > 0) {
    printf("[SCAN]: Scanner disabled with disable count of %u\n", scanner_disable_count);
  } else {
    printf("[SCAN]: Scanner enabled. Triggering informational scan\n");
    scanner_operation.fetch_or(kScannerOpDump);
    scanner_request_event.Signal();
  }
}

}  // namespace

void scanner_trigger_asynchronous_evict(uint64_t min_free_target, uint64_t free_mem_target,
                                        scanner::EvictionLevel eviction_level,
                                        scanner::Output output) {
  if (!eviction_enabled) {
    return;
  }
  {
    Guard<SpinLock, IrqSave> guard{scanner_eviction_target_lock::Get()};
    scanner_eviction_target.pending = true;
    scanner_eviction_target.level = ktl::max(scanner_eviction_target.level, eviction_level);
    // Convert the targets from bytes to pages and combine with any existing requests.
    scanner_eviction_target.min_pages_free += min_free_target / PAGE_SIZE;
    scanner_eviction_target.free_target_pages =
        ktl::max(scanner_eviction_target.free_target_pages, free_mem_target / PAGE_SIZE);
  }

  const uint32_t op =
      kScannerOpReclaim | (output == scanner::Output::Print ? kScannerFlagPrint : 0);
  scanner_operation.fetch_or(op);
  scanner_request_event.Signal();
}

uint64_t scanner_synchronous_evict(uint64_t max_pages, scanner::EvictionLevel eviction_level,
                                   scanner::Output output) {
  if (!eviction_enabled) {
    return 0;
  }

  DEBUG_ASSERT(discardable_evictions_percent <= 100);

  // Compute the desired number of discardable pages to free (vs pager-backed).
  uint64_t pages_to_free_discardable = max_pages * discardable_evictions_percent / 100;

  uint64_t pages_freed = scanner_evict_discardable_vmos(pages_to_free_discardable);
  if (output == scanner::Output::Print && pages_freed > 0) {
    printf("[SCAN]: Evicted %lu pages from discardable vmos\n", pages_freed);
  }

  // Free pager backed memory to get to |max_pages|.
  uint64_t pages_to_free_pager_backed = max_pages - pages_freed;

  list_node_t free_list;
  list_initialize(&free_list);
  uint64_t pages_freed_pager_backed =
      scanner_evict_pager_backed(pages_to_free_pager_backed, eviction_level, &free_list);
  pmm_free(&free_list);

  if (output == scanner::Output::Print && pages_freed_pager_backed > 0) {
    printf("[SCAN]: Evicted %lu user pager backed pages\n", pages_freed_pager_backed);
  }
  pages_freed += pages_freed_pager_backed;

  return pages_freed;
}

uint64_t scanner_do_zero_scan(uint64_t limit) {
  uint64_t deduped = 0;
  uint64_t considered;
  zero_scan_requests.Add(1);
  for (considered = 0; considered < limit; considered++) {
    if (ktl::optional<PageQueues::VmoBacklink> backlink =
            pmm_page_queues()->PopUnswappableZeroFork()) {
      if (!backlink->cow) {
        continue;
      }
      if (backlink->cow->DedupZeroPage(backlink->page, backlink->offset)) {
        deduped++;
      }
    } else {
      zero_scan_ends_empty.Add(1);
      break;
    }
  }

  zero_scan_pages_scanned.Add(considered);
  zero_scan_pages_deduped.Add(deduped);
  return deduped;
}

void scanner_enable_page_table_reclaim() {
  if (page_table_reclaim_policy != PageTableReclaim::OnRequest) {
    return;
  }
  scanner_operation.fetch_or(kScannerOpEnablePTReclaim);
  scanner_request_event.Signal();
}

void scanner_disable_page_table_reclaim() {
  if (page_table_reclaim_policy != PageTableReclaim::OnRequest) {
    return;
  }
  scanner_operation.fetch_or(kScannerOpDisablePTReclaim);
  scanner_request_event.Signal();
}

void scanner_push_disable_count() {
  Guard<Mutex> guard{scanner_disabled_lock::Get()};
  if (scanner_disable_count == 0) {
    scanner_operation.fetch_or(kScannerOpDisable);
    scanner_request_event.Signal();
  }
  scanner_disable_count++;
  scanner_disabled_event.Wait(Deadline::infinite());
}

void scanner_pop_disable_count() {
  Guard<Mutex> guard{scanner_disabled_lock::Get()};
  DEBUG_ASSERT(scanner_disable_count > 0);
  scanner_disable_count--;
  if (scanner_disable_count == 0) {
    scanner_operation.fetch_or(kScannerOpEnable);
    scanner_request_event.Signal();
    scanner_disabled_event.Unsignal();
  }
}

static void scanner_init_func(uint level) {
  Thread *thread =
      Thread::Create("scanner-request-thread", scanner_request_thread, nullptr, LOW_PRIORITY);
  DEBUG_ASSERT(thread);
  eviction_enabled = gCmdline.GetBool(kernel_option::kPageScannerEnableEviction, true);
  zero_page_scans_per_second = gCmdline.GetUInt64(kernel_option::kPageScannerZeroPageScansPerSecond,
                                                  kDefaultZeroPageScansPerSecond);
  if (!gCmdline.GetBool(kernel_option::kPageScannerStartAtBoot, true)) {
    Guard<Mutex> guard{scanner_disabled_lock::Get()};
    scanner_disable_count++;
    scanner_operation.fetch_or(kScannerOpDisable);
    scanner_request_event.Signal();
  }
  if (gCmdline.GetBool(kernel_option::kPageScannerPromoteNoClones, false)) {
    VmObject::EnableEvictionPromoteNoClones();
  }
  const char *pt_eviction = gCmdline.GetString(kernel_option::kPageScannerPageTableEvictionPolicy);
  if (pt_eviction && !strcmp(pt_eviction, "never")) {
    page_table_reclaim_policy = PageTableReclaim::Never;
  } else if (pt_eviction && !strcmp(pt_eviction, "always")) {
    page_table_reclaim_policy = PageTableReclaim::Always;
  } else if (pt_eviction && !strcmp(pt_eviction, "on_request")) {
    page_table_reclaim_policy = PageTableReclaim::OnRequest;
  } else {
    // Leave the policy at the default.
  }

  uint32_t discardable_evictions = gCmdline.GetUInt32(
      kernel_option::kPageScannerDiscardableEvictionsPercent, kDefaultDiscardableEvictionsPercent);
  if (discardable_evictions <= 100) {
    discardable_evictions_percent = discardable_evictions;
  }

  thread->Resume();
}

LK_INIT_HOOK(scanner_init, &scanner_init_func, LK_INIT_LEVEL_LAST)

static int cmd_scanner(int argc, const cmd_args *argv, uint32_t flags) {
  if (argc < 2) {
  usage:
    printf("not enough arguments\n");
    printf("usage:\n");
    printf("%s dump                    : dump scanner info\n", argv[0].str);
    printf("%s push_disable            : increase scanner disable count\n", argv[0].str);
    printf("%s pop_disable             : decrease scanner disable count\n", argv[0].str);
    printf("%s reclaim_all             : attempt to reclaim all possible memory\n", argv[0].str);
    printf("%s rotate_queue            : immediately rotate the page queues\n", argv[0].str);
    printf("%s reclaim <MB> [only_old] : attempt to reclaim requested MB of memory.\n",
           argv[0].str);
    printf("%s pt_reclaim [on|off]     : turn unused page table reclamation on or off\n",
           argv[0].str);
    printf("%s harvest_accessed        : harvest all page accessed information\n", argv[0].str);
    return ZX_ERR_INTERNAL;
  }
  if (!strcmp(argv[1].str, "dump")) {
    scanner_dump_info();
  } else if (!strcmp(argv[1].str, "push_disable")) {
    scanner_push_disable_count();
  } else if (!strcmp(argv[1].str, "pop_disable")) {
    scanner_pop_disable_count();
  } else if (!strcmp(argv[1].str, "reclaim_all")) {
    scanner_operation.fetch_or(kScannerOpReclaimAll | kScannerFlagPrint);
    scanner_request_event.Signal();
  } else if (!strcmp(argv[1].str, "rotate_queue")) {
    scanner_operation.fetch_or(kScannerOpRotateQueues);
    scanner_request_event.Signal();
  } else if (!strcmp(argv[1].str, "harvest_accessed")) {
    scanner_operation.fetch_or(kScannerOpHarvestAccessed | kScannerFlagPrint);
    scanner_request_event.Signal();
  } else if (!strcmp(argv[1].str, "reclaim")) {
    if (argc < 3) {
      goto usage;
    }
    if (!eviction_enabled) {
      printf("%s is false, reclamation request will have no effect\n",
             kernel_option::kPageScannerEnableEviction);
    }
    scanner::EvictionLevel eviction_level = scanner::EvictionLevel::IncludeNewest;
    if (argc >= 4 && !strcmp(argv[3].str, "only_old")) {
      eviction_level = scanner::EvictionLevel::OnlyOldest;
    }
    const uint64_t bytes = argv[2].u * MB;
    scanner_trigger_asynchronous_evict(bytes, 0, eviction_level, scanner::Output::Print);
  } else if (!strcmp(argv[1].str, "pt_reclaim")) {
    if (argc < 3) {
      goto usage;
    }
    bool enable = false;
    if (!strcmp(argv[2].str, "on")) {
      enable = true;
    } else if (!strcmp(argv[2].str, "off")) {
      enable = false;
    } else {
      goto usage;
    }
    if (page_table_reclaim_policy == PageTableReclaim::Always) {
      printf("Page table reclamation set to always by command line, cannot adjust\n");
    } else if (page_table_reclaim_policy == PageTableReclaim::Never) {
      printf("Page table reclamation set to never by command line, cannot adjust\n");
    } else {
      if (enable) {
        scanner_enable_page_table_reclaim();
      } else {
        scanner_disable_page_table_reclaim();
      }
    }
  } else {
    printf("unknown command\n");
    goto usage;
  }
  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("scanner", "active memory scanner", &cmd_scanner, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(scanner)
