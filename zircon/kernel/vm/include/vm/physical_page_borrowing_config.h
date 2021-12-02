// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_

#include <kernel/spinlock.h>

// The PmmNode has an instance of this class, which will allow the ppb kernel command to dynamically
// control whether physical page borrowing is enabled or disabled (for pager-backed VMOs only for
// now).
//
// TODO(dustingreen):
//  * Wire up to command line flags.
//  * Add the remaining code to make is_borrowing_enabled() true really do any borrowing (we're
//    adding the config mechanism first so the other code can depend on the config mechanism from
//    the start).
//  * Add a kernel command (k ppb) to control + a kernel-command-only sweeper that makes the current
//    situation consistent with the setting.
//  * Change the default from false to true.
class PhysicalPageBorrowingConfig {
 public:
  PhysicalPageBorrowingConfig() = default;
  PhysicalPageBorrowingConfig(const PhysicalPageBorrowingConfig& to_copy) = delete;
  PhysicalPageBorrowingConfig(PhysicalPageBorrowingConfig&& to_move) = delete;
  PhysicalPageBorrowingConfig& operator=(const PhysicalPageBorrowingConfig& to_copy) = delete;
  PhysicalPageBorrowingConfig& operator=(PhysicalPageBorrowingConfig&& to_move) = delete;

  // true - allow page borrowing for newly-allocated pages of pager-backed VMOs
  // false - disallow any page borrowing for newly-allocated pages
  void set_borrowing_enabled(bool enabled) {
    borrowing_enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool is_borrowing_enabled() { return borrowing_enabled_.load(ktl::memory_order_relaxed); }

  // true - decommitted contiguous VMO pages will decommit+loan the pages.
  // false - decommit of a contiguous VMO page zeroes instead of decommitting+loaning.
  void set_loaning_enabled(bool enabled) {
    loaning_enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool is_loaning_enabled() { return loaning_enabled_.load(ktl::memory_order_relaxed); }

 private:
  // Enable page borrowing.  If this is false, no page borrowing will occur.  Can be dynamically
  // changed, but dynamically changing this value doesn't automaticallly sweep existing pages to
  // conform to the new setting.
  ktl::atomic<bool> borrowing_enabled_ = false;

  // Enable page loaning.  If false, no page loaning will occur.  If true, decommitting pages of a
  // contiguous VMO will loan the pages.  This can be dynamically changed, but changes will only
  // apply to subsequent decommit of contiguous VMO pages.
  ktl::atomic<bool> loaning_enabled_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_
