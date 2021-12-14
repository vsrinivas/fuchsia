// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_

#include <kernel/spinlock.h>

enum class PhysicalPageBorrowingSite : uint32_t {
  kSupplyPages,
};

// The PmmNode has an instance of this class, which will allow the ppb kernel command to dynamically
// control whether physical page borrowing is enabled or disabled (for pager-backed VMOs only for
// now).
//
// TODO(dustingreen):
//  * Change the default from false to true.
class PhysicalPageBorrowingConfig {
 public:
  PhysicalPageBorrowingConfig() = default;
  PhysicalPageBorrowingConfig(const PhysicalPageBorrowingConfig& to_copy) = delete;
  PhysicalPageBorrowingConfig(PhysicalPageBorrowingConfig&& to_move) = delete;
  PhysicalPageBorrowingConfig& operator=(const PhysicalPageBorrowingConfig& to_copy) = delete;
  PhysicalPageBorrowingConfig& operator=(PhysicalPageBorrowingConfig&& to_move) = delete;

  bool is_any_borrowing_enabled() {
    return is_any_borrowing_enabled_.load(ktl::memory_order_relaxed);
  }

  // true - allow page borrowing for newly-allocated pages of pager-backed VMOs
  // false - disallow any page borrowing for newly-allocated pages
  void set_borrowing_in_supplypages_enabled(bool enabled) {
    borrowing_in_supplypages_enabled_.store(enabled, ktl::memory_order_relaxed);
    OnBorrowingSettingsChanged();
  }
  bool is_borrowing_in_supplypages_enabled() {
    return borrowing_in_supplypages_enabled_.load(ktl::memory_order_relaxed);
  }

  // true - allow page borrowing when a page is logically moved to MRU queue
  // false - disallow page borrowing when a page is logically moved to MRU queue
  void set_borrowing_on_mru_enabled(bool enabled) {
    borrowing_on_mru_enabled_.store(enabled, ktl::memory_order_relaxed);
    OnBorrowingSettingsChanged();
  }
  bool is_borrowing_on_mru_enabled() {
    return borrowing_on_mru_enabled_.load(ktl::memory_order_relaxed);
  }

  // true - decommitted contiguous VMO pages will decommit+loan the pages.
  // false - decommit of a contiguous VMO page zeroes instead of decommitting+loaning.
  void set_loaning_enabled(bool enabled) {
    loaning_enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool is_loaning_enabled() { return loaning_enabled_.load(ktl::memory_order_relaxed); }

 private:
  void OnBorrowingSettingsChanged() {
    bool enabled = is_borrowing_in_supplypages_enabled() || is_borrowing_on_mru_enabled();
    is_any_borrowing_enabled_.store(enabled, ktl::memory_order_relaxed);
  }

  // True iff any borrowing is enabled.
  ktl::atomic<bool> is_any_borrowing_enabled_ = false;

  // Enable page borrowing by SupplyPages().  If this is false, no page borrowing will occur in
  // SupplyPages().  If this is true, SupplyPages() will copy supplied pages into borrowed pages.
  // Can be dynamically changed, but dynamically changing this value doesn't automaticallly sweep
  // existing pages to conform to the new setting.
  ktl::atomic<bool> borrowing_in_supplypages_enabled_ = false;

  // Enable page borrowing when a page is logically moved to the MRU queue.  If true, replace an
  // accessed non-loaned page with loaned on access.  If false, this is disabled.
  ktl::atomic<bool> borrowing_on_mru_enabled_ = false;

  // Enable page loaning.  If false, no page loaning will occur.  If true, decommitting pages of a
  // contiguous VMO will loan the pages.  This can be dynamically changed, but changes will only
  // apply to subsequent decommit of contiguous VMO pages.
  ktl::atomic<bool> loaning_enabled_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSICAL_PAGE_BORROWING_CONFIG_H_
