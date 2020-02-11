// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_

#include <ktl/atomic.h>
#include <vm/page.h>

// |PmmChecker| is used to detect memory corruption.  It is logically part of |PmmNode|.
//
// Usage is as follows:
//
//   PmmChecker checker;
//
//   // For all free pages...
//   for (...) {
//     checker.FillPattern(page);
//   }
//
//   // Now that all free pages have been filled with a pattern, we can arm the checker.
//   checker.Arm();
//   ...
//   checker.AssertPattern(page);
//
class PmmChecker {
 public:
  // Returns true if armed.
  bool IsArmed() const { return armed_; }

  void Arm();
  void Disarm();

  // Fills |page| with a pattern.
  //
  // It is an error to call this method with a page that is not free.  In other words,
  // page->is_page() must be true.
  void FillPattern(vm_page_t* page);

  // Returns true if |page| contains the expected fill pattern or |IsArmed| is false.
  //
  // Otherwise, returns false.
  __WARN_UNUSED_RESULT bool ValidatePattern(vm_page_t* page);

  // Panics the kernel if |page| does not contain the expected fill pattern and |IsArmed| is true.
  //
  // Otherwise, does nothing.
  void AssertPattern(vm_page_t* page);

 private:
  bool armed_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_
