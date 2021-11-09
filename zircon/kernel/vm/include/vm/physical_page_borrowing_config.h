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
//  * Wire up to command line flag.
//  * Add the remaining code to make enabled true really do any borrowing (we're adding the config
//    mechanism first so the other code can depend on the config mechanism from the start).
//  * Add a kernel command to control + a kernel-command-only sweeper that makes the current
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
  void set_enabled(bool enabled) { enabled_.store(enabled, ktl::memory_order_relaxed); }
  bool enabled() { return enabled_.load(ktl::memory_order_relaxed); }

 private:
  // Enable page borrowing.  If this is false, no page borrowing will occur.  Can be dynamically
  // changed, but dynamically changing this value doesn't automaticallly sweep existing pages to
  // conform to the new setting.
  ktl::atomic<bool> enabled_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_
