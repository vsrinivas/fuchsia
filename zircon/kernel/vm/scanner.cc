// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/event.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

namespace {

// Atomic used to track whether a scanner thread has been created yet or not.
ktl::atomic<bool> scanner_initialized = false;
// Tracks whether the scanner should perform reclamation the next time it does a scan pass
ktl::atomic<bool> scanner_perform_reclamation = false;

// Event to signal the scanner thread to perform a scan.
event_t scanner_request_event =
    EVENT_INITIAL_VALUE(scanner_request_event, false, EVENT_FLAG_AUTOUNSIGNAL);

void scanner_do_single_scan(bool reclaim) {
  uint32_t zero_pages = VmObject::ScanAllForZeroPages(reclaim);
  printf("[SCAN]: Found %u zero pages that %s de-duped\n", zero_pages,
         reclaim ? "were" : "could be");
}

int scanner_request_thread(void *) {
  while (1) {
    event_wait(&scanner_request_event);
    bool reclaim = scanner_perform_reclamation.exchange(false);
    scanner_do_single_scan(reclaim);
  }
  return 0;
}

}  // namespace

void scanner_trigger_scan(bool reclaim) {
  // Lazily create the scanner thread the first time a scan is triggered.
  bool was_initialized = scanner_initialized.exchange(true);
  if (!was_initialized) {
    thread_t *thread =
        thread_create("scanner-request-thread", scanner_request_thread, nullptr, LOW_PRIORITY);
    DEBUG_ASSERT(thread);
    thread_resume(thread);
  }
  // For multiple triggers we want to ensure any reclaim request is sticky until a scan runs.
  if (reclaim) {
    scanner_perform_reclamation = true;
  }
  event_signal(&scanner_request_event, true);
}
