// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_

#include <fbl/macros.h>

// TODO: Convert the scanner to be a proper singleton object and use that to namespace these enums
// instead.
namespace scanner {
enum class EvictionLevel : uint8_t {
  OnlyOldest = 0,
  IncludeNewest = 1,
};
enum class Output : bool {
  Print = true,
  NoPrint = false,
};
}  // namespace scanner

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

// Instructs the scanner to reclaim memory until free memory equals the |free_mem_target| and at
// least |min_free_target| has been reclaimed. Reclamation will happen asynchronously and this
// function returns immediately. Once the target is reached, or there is no more memory that can be
// reclaimed, this process will stop and the free memory target will be cleared. The eviction_level
// is a rough control on how hard to try and evict. Multiple calls to scanner_trigger_evict will
// cause all the targets to get merged by adding together |min_free_target|, taking the max of
// |free_mem_target| and the highest or most aggressive of any eviction_level.
void scanner_trigger_evict(
    uint64_t min_free_target, uint64_t free_mem_target,
    scanner::EvictionLevel eviction_level = scanner::EvictionLevel::OnlyOldest,
    scanner::Output output = scanner::Output::NoPrint);

// Performs a synchronous request to evict the requested number of pages. Evicted pages are placed
// in the passed |free_list| and become owned by the caller, with the return value being the number
// of free pages. The |eviction_level| is a rough control that maps to how old a page needs to be
// for being considered for eviction. This may acquire arbitrary vmo and aspace locks.
uint64_t scanner_evict_pager_backed(uint64_t max_pages, scanner::EvictionLevel eviction_level,
                                    list_node_t *free_list);

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

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
