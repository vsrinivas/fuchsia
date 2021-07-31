// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PPB_CONFIG_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PPB_CONFIG_H_

#include <kernel/spinlock.h>

class PpbConfig {
 public:
  PpbConfig() = default;
  PpbConfig(const PpbConfig& to_copy) = delete;
  PpbConfig(PpbConfig&& to_move) = delete;
  PpbConfig& operator=(const PpbConfig& to_copy) = delete;
  PpbConfig& operator=(PpbConfig&& to_move) = delete;

  void set_enabled(bool enabled) {
    enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool enabled() {
    return enabled_.load(ktl::memory_order_relaxed);
  }

  // Changing this from true to false only takes effect during page allocation, not during a sweep.
  void set_non_pager_enabled(bool enabled) {
    non_pager_enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool non_pager_enabled() {
    return non_pager_enabled_.load(ktl::memory_order_relaxed);
  }

  void set_low_mem_sweeping_enabled(bool enabled) {
    low_mem_sweeping_enabled_.store(enabled, ktl::memory_order_relaxed);
  }
  bool low_mem_sweeping_enabled() {
    return low_mem_sweeping_enabled_.load(ktl::memory_order_relaxed);
  }

 private:
  ktl::atomic<bool> enabled_ = true;
  // on_ must also be true for non-pager VMOs to borrow loaned pages
  ktl::atomic<bool> non_pager_enabled_ = true;
  ktl::atomic<bool> low_mem_sweeping_enabled_ = true;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PPB_CONFIG_H_
