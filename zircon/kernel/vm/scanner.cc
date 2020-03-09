// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/console.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

namespace {

static constexpr uint32_t kScannerFlagPrint = 1u << 0;
static constexpr uint32_t kScannerOpDisable = 1u << 1;
static constexpr uint32_t kScannerOpEnable = 1u << 2;
static constexpr uint32_t kScannerOpDump = 1u << 3;
static constexpr uint32_t kScannerOpReclaimAll = 1u << 4;

// Tracks what the scanner should do when it is next woken up.
ktl::atomic<uint32_t> scanner_operation = 0;

// Event to signal the scanner thread to wake up and perform work.
Event scanner_request_event{EVENT_FLAG_AUTOUNSIGNAL};

// Event that is signaled whenever the scanner is disabled. This is used to synchronize disable
// requests with the scanner thread.
Event scanner_disabled_event{0};
DECLARE_SINGLETON_MUTEX(scanner_disabled_lock);
uint32_t scanner_disable_count TA_GUARDED(scanner_disabled_lock::Get()) = 0;

void scanner_print_stats() {
  uint64_t zero_pages = VmObject::ScanAllForZeroPages(false);
  printf("[SCAN]: Found %lu zero pages that could be de-duped\n", zero_pages);
  PageQueues::Counts queue_counts = pmm_page_queues()->DebugQueueCounts();
  printf("[SCAN]: Found %lu user-paged backed pages\n", queue_counts.pager_backed);
}

void scanner_do_reclaim(bool print) {
  uint64_t zero_pages = VmObject::ScanAllForZeroPages(true);
  if (print) {
    printf("[SCAN]: Found %lu zero pages that were de-duped\n", zero_pages);
  }
}

int scanner_request_thread(void *) {
  bool disabled = false;
  while (1) {
    scanner_request_event.Wait(Deadline::infinite());
    uint32_t op = scanner_operation.exchange(0);
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
    bool print = false;
    if (op & kScannerFlagPrint) {
      op &= ~kScannerFlagPrint;
      print = true;
    }
    if (op & kScannerOpReclaimAll) {
      op &= ~kScannerOpReclaimAll;
      scanner_do_reclaim(print);
    }
    if (op & kScannerOpDump) {
      op &= ~kScannerOpDump;
      scanner_print_stats();
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
  if (!gCmdline.GetBool("kernel.page-scanner.start-at-boot", false)) {
    Guard<Mutex> guard{scanner_disabled_lock::Get()};
    scanner_disable_count++;
    scanner_operation.fetch_or(kScannerOpDisable);
    scanner_request_event.Signal();
  }
  thread->Resume();
}

LK_INIT_HOOK(scanner_init, &scanner_init_func, LK_INIT_LEVEL_LAST)

static int cmd_scanner(int argc, const cmd_args *argv, uint32_t flags) {
  if (argc < 2) {
  usage:
    printf("not enough arguments\n");
    printf("usage:\n");
    printf("%s dump         : dump scanner info\n", argv[0].str);
    printf("%s push_disable : increase scanner disable count\n", argv[0].str);
    printf("%s pop_disable  : decrease scanner disable count\n", argv[0].str);
    printf("%s reclaim_all  : attempt to reclaim all possible memory\n", argv[0].str);
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
  } else {
    printf("unknown command\n");
    goto usage;
  }
  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("scanner", "active memory scanner", &cmd_scanner, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(scanner)
