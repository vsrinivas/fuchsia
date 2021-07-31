// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/counters.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <zircon/time.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <ktl/algorithm.h>
#include <lk/init.h>
#include <vm/loan_sweeper.h>
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
constexpr uint32_t kScannerOpHarvestAccessed = 1u << 7;
constexpr uint32_t kScannerOpEnablePTReclaim = 1u << 8;
constexpr uint32_t kScannerOpDisablePTReclaim = 1u << 9;

// Amount of time between pager queue rotations.
constexpr zx_duration_t kQueueRotateTime = ZX_SEC(10);

// Number of pages to attempt to de-dupe back to zero every second. This not atomic as it is only
// set during init before the scanner thread starts up, at which point it becomes read only.
uint64_t zero_page_scans_per_second = 0;

PageTableEvictionPolicy page_table_reclaim_policy = PageTableEvictionPolicy::kAlways;

// Tracks what the scanner should do when it is next woken up.
ktl::atomic<uint32_t> scanner_operation = 0;

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

void scanner_print_stats(zx_duration_t time_till_queue_rotate) {
  uint64_t zero_pages = VmObject::ScanAllForZeroPages(false);
  printf("[SCAN]: Found %lu zero pages across all of memory\n", zero_pages);
  PageQueues::Counts queue_counts = pmm_page_queues()->QueueCounts();
  for (size_t i = 0; i < PageQueue::kNumPagerBacked; i++) {
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
      // Re-enable eviction if it was originally enabled.
      if (gBootOptions->page_scanner_enable_eviction) {
        pmm_evictor()->EnableEviction();
      }
      disabled = false;
    }
    if (op & kScannerOpDisable) {
      op &= ~kScannerOpDisable;
      // Make sure no eviction is happening either.
      pmm_evictor()->DisableEviction();
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
      pmm_evictor()->SetOneShotEvictionTarget(Evictor::EvictionTarget{
          .pending = true,
          .free_pages_target = UINT64_MAX,
          .level = Evictor::EvictionLevel::IncludeNewest,
          .print_counts = print,
      });
      pmm_evictor()->EvictOneShotFromPreloadedTarget();
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
      const VmAspace::NonTerminalAction action =
          page_table_reclaim_policy == PageTableEvictionPolicy::kAlways || pt_eviction_enabled
              ? VmAspace::NonTerminalAction::FreeUnaccessed
              : VmAspace::NonTerminalAction::Retain;
      // If we neither have page eviction or page table eviction then we can skip harvesting
      // accessed bits.
      if (action == VmAspace::NonTerminalAction::FreeUnaccessed ||
          pmm_evictor()->IsEvictionEnabled()) {
        VmAspace::HarvestAllUserAccessedBits(action);
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
  if (page_table_reclaim_policy != PageTableEvictionPolicy::kOnRequest) {
    return;
  }
  scanner_operation.fetch_or(kScannerOpEnablePTReclaim);
  scanner_request_event.Signal();
}

void scanner_disable_page_table_reclaim() {
  if (page_table_reclaim_policy != PageTableEvictionPolicy::kOnRequest) {
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
  zero_page_scans_per_second = gBootOptions->page_scanner_zero_page_scans_per_second;
  if (!gBootOptions->page_scanner_start_at_boot) {
    Guard<Mutex> guard{scanner_disabled_lock::Get()};
    scanner_disable_count++;
    scanner_operation.fetch_or(kScannerOpDisable);
    scanner_request_event.Signal();
  }
  if (gBootOptions->page_scanner_promote_no_clones) {
    VmObject::EnableEvictionPromoteNoClones();
  }
  page_table_reclaim_policy = gBootOptions->page_scanner_page_table_eviction_policy;

  if (gBootOptions->page_scanner_enable_eviction) {
    pmm_evictor()->EnableEviction();
  }
  pmm_evictor()->SetDiscardableEvictionsPercent(
      gBootOptions->page_scanner_discardable_evictions_percent);
  zx_time_t eviction_interval = ZX_SEC(gBootOptions->page_scanner_eviction_interval_seconds);
  pmm_evictor()->SetContinuousEvictionInterval(
      (eviction_interval < kQueueRotateTime) ? kQueueRotateTime : eviction_interval);

  pmm_loan_sweeper()->Init();

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
    if (!pmm_evictor()->IsEvictionEnabled()) {
      printf("%s is false, reclamation request will have no effect\n",
             kPageScannerEnableEvictionName.data());
    }
    Evictor::EvictionLevel eviction_level = Evictor::EvictionLevel::IncludeNewest;
    if (argc >= 4 && !strcmp(argv[3].str, "only_old")) {
      eviction_level = Evictor::EvictionLevel::OnlyOldest;
    }
    const uint64_t bytes = argv[2].u * MB;
    pmm_evictor()->EvictOneShotAsynchronous(bytes, 0, eviction_level, Evictor::Output::Print);
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
    if (page_table_reclaim_policy == PageTableEvictionPolicy::kAlways) {
      printf("Page table reclamation set to always by command line, cannot adjust\n");
    } else if (page_table_reclaim_policy == PageTableEvictionPolicy::kNever) {
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
