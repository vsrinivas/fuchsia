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
#include <vm/physical_page_borrowing_config.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

#include <ktl/enforce.h>

namespace {

constexpr uint32_t kScannerFlagPrint = 1u << 0;
constexpr uint32_t kScannerOpDisable = 1u << 1;
constexpr uint32_t kScannerOpEnable = 1u << 2;
constexpr uint32_t kScannerOpDump = 1u << 3;
constexpr uint32_t kScannerOpReclaimAll = 1u << 4;
constexpr uint32_t kScannerOpUpdateHarvestTime = 1u << 6;
constexpr uint32_t kScannerOpEnablePTReclaim = 1u << 8;
constexpr uint32_t kScannerOpDisablePTReclaim = 1u << 9;

// Amount of time between page table evictions. This is not atomic as it is only set during init
// before the scanner thread starts up, at which point it becomes read only.
zx_duration_t page_table_evict_time = ZX_SEC(10);

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

// Mutex used to ensure only a single access scan is happening at once.
DECLARE_SINGLETON_MUTEX(accessed_scanner_lock);
// To avoid redundant scanning we remember when the last accessed scan happened. As an accessed scan
// might or might not harvest track the last time either kind of scan completed. Since harvesting is
// a super set of scanning last_accessed_scan_complete >= last_harvest_accessed_scan_complete.
ktl::atomic<zx_time_t> last_accessed_scan_complete = ZX_TIME_INFINITE_PAST;
ktl::atomic<zx_time_t> last_harvest_accessed_scan_complete = ZX_TIME_INFINITE_PAST;

// The accessed scan rate starts matched to the minimum aging period, since scanning more frequently
// than that does not produce any fidelity of information.
ktl::atomic<zx_duration_t> accessed_scan_period = PageQueues::kDefaultMinMruRotateTime;

ktl::atomic<bool> reclaim_pt_next_accessed_scan = false;

KCOUNTER(zero_scan_requests, "vm.scanner.zero_scan.requests")
KCOUNTER(zero_scan_ends_empty, "vm.scanner.zero_scan.queue_emptied")
KCOUNTER(zero_scan_pages_scanned, "vm.scanner.zero_scan.total_pages_considered")
KCOUNTER(zero_scan_pages_deduped, "vm.scanner.zero_scan.pages_deduped")

void scanner_print_stats() {
  PageQueues::Counts queue_counts = pmm_page_queues()->QueueCounts();
  for (size_t i = 0; i < PageQueues::kNumReclaim; i++) {
    printf("[SCAN]: Found %lu reclaimable pages in queue %zu\n", queue_counts.reclaim[i], i);
  }
  printf("[SCAN]: Found %lu reclaimable pages in DontNeed queue\n", queue_counts.reclaim_dont_need);
  printf("[SCAN]: Found %lu zero forked pages\n", queue_counts.anonymous_zero_fork);

  VmCowPages::DiscardablePageCounts counts = VmCowPages::DebugDiscardablePageCounts();
  printf("[SCAN]: Found %lu locked pages in discardable vmos\n", counts.locked);
  printf("[SCAN]: Found %lu unlocked pages in discardable vmos\n", counts.unlocked);
  pmm_page_queues()->Dump();
}

zx_time_t calc_next_zero_scan_deadline(zx_time_t current) {
  return zero_page_scans_per_second > 0 ? zx_time_add_duration(current, ZX_SEC(1))
                                        : ZX_TIME_INFINITE;
}

zx_time_t calc_next_pt_evict_deadline(zx_time_t current, bool pt_enable_override) {
  if (page_table_reclaim_policy == PageTableEvictionPolicy::kAlways || pt_enable_override) {
    return zx_time_add_duration(current, page_table_evict_time);
  } else {
    return ZX_TIME_INFINITE;
  }
}

int scanner_request_thread(void *) {
  bool disabled = false;
  bool pt_eviction_enabled = false;
  zx_time_t last_pt_evict = ZX_TIME_INFINITE_PAST;
  zx_time_t next_zero_scan_deadline = calc_next_zero_scan_deadline(current_time());
  zx_time_t next_harvest_deadline = zx_time_add_duration(current_time(), accessed_scan_period);
  while (1) {
    if (disabled) {
      scanner_request_event.Wait(Deadline::infinite());
    } else {
      zx_time_t next_pt_evict_deadline =
          calc_next_pt_evict_deadline(last_pt_evict, pt_eviction_enabled);
      scanner_request_event.Wait(Deadline::no_slack(ktl::min(
          next_pt_evict_deadline, ktl::min(next_zero_scan_deadline, next_harvest_deadline))));
    }
    int32_t op = scanner_operation.exchange(0);
    // It is possible for enable and disable to happen at the same time. This indicates the disabled
    // count went from 1->0->1 and so we want to remain disabled. We do this by performing the
    // enable step first. We know that the scenario of 0->1->0 is not possible as the 0->1 part of
    // that holds the mutex until complete.
    if (op & kScannerOpEnable) {
      op &= ~kScannerOpEnable;
      pmm_page_queues()->EnableAging();
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
      pmm_page_queues()->DisableAging();
      // Grab the harvester lock to wait for any in progress scans to complete.
      { Guard<Mutex> guard{accessed_scanner_lock::Get()}; }
      scanner_disabled_event.Signal();
    }
    if (disabled) {
      // put the remaining ops back and resume waiting.
      scanner_operation.fetch_or(op);
      continue;
    }

    zx_time_t current = current_time();

    if (current >= calc_next_pt_evict_deadline(last_pt_evict, pt_eviction_enabled) ||
        (op & kScannerOpReclaimAll)) {
      // Make sure a scan has happened since we last expected page table reclamation to happen.
      // This in effect will cause scanning to happen at least once every pt reclamation period, and
      // therefore for reclamation to happen, on average, once every target period.
      // This is fine, and the goal of this is to ensure that we avoid triggering additional
      // accessed scans if we can avoid it, and that we additionally do not reclaim page tables too
      // often.
      scanner_wait_for_accessed_scan(last_pt_evict, false);
      // Trigger pt eviction to happen next time, which in the worst case will be once we timeout
      // and call scanner_wait_for_accessed_scan above. In essence this is introducing some slack to
      // the reclamation timeout to maximize the chance that the reclamation gets paired with a
      // separate accessed harvest.
      reclaim_pt_next_accessed_scan = true;
      // Set now to our last pt evict so we retry again next period.
      last_pt_evict = current;
    }

    if (current >= next_harvest_deadline) {
      scanner_wait_for_accessed_scan(next_harvest_deadline, false);
      op |= kScannerOpUpdateHarvestTime;
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
      // To ensure any page table eviction that was set earlier actually occurs, force an accessed
      // scan to happen right now.
      scanner_wait_for_accessed_scan(current_time(), true);
    }
    if (op & kScannerOpDump) {
      op &= ~kScannerOpDump;
      scanner_print_stats();
    }
    if (op & kScannerOpEnablePTReclaim) {
      pt_eviction_enabled = true;
      op &= ~kScannerOpEnablePTReclaim;
    }
    if (op & kScannerOpDisablePTReclaim) {
      pt_eviction_enabled = false;
      op &= ~kScannerOpDisablePTReclaim;
    }
    if (op & kScannerOpUpdateHarvestTime) {
      op &= ~kScannerOpUpdateHarvestTime;
      next_harvest_deadline =
          zx_time_add_duration(last_accessed_scan_complete, accessed_scan_period);
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

// Currently accessed scanning happens completely inline, and so this does one of three things
// 1. Returns immediately if a sufficiently recent scan already happened
// 2. Waits for an in progress scan to finish, and then most likely returns unless update_time is
//    ZX_TIME_INFINITE
// 3. Performs an entire scan and then returns.
// The public definition of this method is abstract to allow for this to, in the future, not
// necessarily perform a scan itself, but sync up with the scanner thread that might be slowly
// scanning in the background.
void scanner_wait_for_accessed_scan(zx_time_t update_time, bool clear_bits) {
  if (update_time <=
      (clear_bits ? last_harvest_accessed_scan_complete : last_accessed_scan_complete)) {
    // scanning is sufficiently up to date.
    return;
  }
  Guard<Mutex> guard{accessed_scanner_lock::Get()};
  // Re-check now that we hold the lock in case a scan just finished and we were blocked on it.
  if (update_time <=
      (clear_bits ? last_harvest_accessed_scan_complete : last_accessed_scan_complete)) {
    return;
  }
  bool reclaim_pt = reclaim_pt_next_accessed_scan.exchange(false);
  // Perform a scan.
  // If we neither have page eviction or page table eviction then we can skip harvesting
  // accessed bits.
  if (reclaim_pt || pmm_evictor()->IsEvictionEnabled()) {
    const VmAspace::NonTerminalAction action = reclaim_pt
                                                   ? VmAspace::NonTerminalAction::FreeUnaccessed
                                                   : VmAspace::NonTerminalAction::Retain;
    pmm_page_queues()->BeginAccessScan();
    VmAspace::HarvestAllUserAccessedBits(action, clear_bits
                                                     ? VmAspace::TerminalAction::UpdateAgeAndHarvest
                                                     : VmAspace::TerminalAction::UpdateAge);
    pmm_page_queues()->EndAccessScan();
  }
  last_accessed_scan_complete = current_time();
  if (clear_bits) {
    last_harvest_accessed_scan_complete = last_accessed_scan_complete.load();
  }
}

PageQueues::ActiveInactiveCounts scanner_synchronized_active_inactive_counts() {
  // Acquire the scanner lock so we know EndAccessScan has been called
  Guard<Mutex> guard{accessed_scanner_lock::Get()};
  // Now we know that the pages queues will return us live, and not cached, values.
  PageQueues::ActiveInactiveCounts counts = pmm_page_queues()->GetActiveInactiveCounts();
  DEBUG_ASSERT(!counts.cached);
  return counts;
}

uint64_t scanner_do_zero_scan(uint64_t limit) {
  uint64_t deduped = 0;
  uint64_t considered;
  zero_scan_requests.Add(1);
  for (considered = 0; considered < limit; considered++) {
    if (ktl::optional<PageQueues::VmoBacklink> backlink =
            pmm_page_queues()->PopAnonymousZeroFork()) {
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
  page_table_reclaim_policy = gBootOptions->page_scanner_page_table_eviction_policy;
  page_table_evict_time =
      ZX_SEC(ktl::max(gBootOptions->page_scanner_page_table_eviction_period, 1u));

  if (gBootOptions->page_scanner_enable_eviction) {
    pmm_evictor()->EnableEviction();
  }
  pmm_evictor()->SetDiscardableEvictionsPercent(
      gBootOptions->page_scanner_discardable_evictions_percent);
  zx_time_t eviction_interval = ZX_SEC(gBootOptions->page_scanner_eviction_interval_seconds);
  pmm_evictor()->SetContinuousEvictionInterval(eviction_interval);

  pmm_page_queues()->SetActiveRatioMultiplier(gBootOptions->page_scanner_active_ratio_multiplier);
  pmm_page_queues()->StartThreads(ZX_SEC(gBootOptions->page_scanner_min_aging_interval),
                                  ZX_SEC(gBootOptions->page_scanner_max_aging_interval));
  // Ensure at least 1 second between access scans.
  accessed_scan_period = ZX_SEC(ktl::max(gBootOptions->page_scanner_min_aging_interval, 1u));

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
    pmm_page_queues()->RotateReclaimQueues();
  } else if (!strcmp(argv[1].str, "harvest_accessed")) {
    scanner_wait_for_accessed_scan(current_time(), true);
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
