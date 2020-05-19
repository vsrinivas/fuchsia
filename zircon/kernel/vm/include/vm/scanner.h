// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_

#include <fbl/macros.h>

// Increase the disable count of the scanner. This may need to block until the scanner finishes any
// current work and so should not be called with other locks held that may conflict with the
// scanner. Generally this is expected to be used by unittests.
void scanner_push_disable_count();

// Decrease the disable count of the scanner, potentially re-enabling the scanner if it reaches
// zero.
void scanner_pop_disable_count();

// Attempts to scan for, and dedupe, zero pages. Page candidates are pulled from the
// unswappable_zero_fork page queue. It will consider up to `limit` candidates, and return the
// number of pages actually deduped.
// This is expected to be used internally by the scanner thread, but is exposed for testing,
// debugging and other code to use.
uint64_t scanner_do_zero_scan(uint64_t limit);

// AutoVmScannerDisable is an RAII helper for disabling scanning using the
// scanner_push_disable_count()/scanner_pop_disable_count(). Disabling the scanner is useful in test
// code where it is not possible or practical to hold locks to prevent the scanner from taking
// actions.
class AutoVmScannerDisable {
 public:
  AutoVmScannerDisable() { scanner_push_disable_count(); }
  ~AutoVmScannerDisable() { scanner_pop_disable_count(); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoVmScannerDisable);
};

// Instructs the scanner to reclaim memory until free memory equals the target. Reclamation will
// happen asynchronously and this function returns immediately. Once the target is reached, or
// there is no more memory that can be reclaimed, this process will stop and the free memory target
// will be cleared. When print=true the results of the eviction will get sent to the debuglog.
void scanner_trigger_reclaim(uint64_t free_mem_target, bool print = false);

// Performs a synchronous request to evict the requested number of pages. Evicted pages are placed
// in the passed |free_list| and become owned by the caller, with the return value being the number
// of free pages. This may acquire arbitrary vmo and aspace locks.
uint64_t scanner_evict_pager_backed(uint64_t max_pages, list_node_t *free_list);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
