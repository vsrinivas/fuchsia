// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_

#include <stdio.h>

#include <arch/defines.h>
#include <ktl/atomic.h>
#include <ktl/optional.h>
#include <vm/page.h>

// |PmmChecker| is used to detect memory corruption.  It is logically part of |PmmNode|.
//
// Usage is as follows:
//
//   PmmChecker checker;
//
//   // Check only the first 16 bytes of each page.
//   checker.SetFillSize(16);
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
  // The action to take when page validation fails.
  enum class Action : uint32_t { OOPS, PANIC };

  static constexpr Action DefaultAction = Action::OOPS;

  // Returns ktl::nullopt if |action_string| is invalid.
  static ktl::optional<Action> ActionFromString(const char* action_string);

  static const char* ActionToString(Action action);

  // Returns true if |fill_size| is a valid value.  Valid values are mutliples of 8 between 8 and
  // PAGE_SIZE, inclusive.
  static bool IsValidFillSize(size_t fill_size);

  // Sets the size of the pattern to be written / validated.
  //
  // It is an error to call this method with an invalid fill size (see |IsValidFillSize|.
  //
  // It is an error to call this method if the checker |IsArmed|.  After changing the fill size, be
  // sure to re-fill any free pages to ensure that a future call to |ValidatePattern| or
  // |AssertPattern| won't supriously report corruption.
  void SetFillSize(size_t fill_size);

  // Returns the fill size.
  size_t GetFillSize() const { return fill_size_; }

  void SetAction(Action action) { action_ = action; }
  Action GetAction() const { return action_; }

  // Returns true if armed.
  bool IsArmed() const { return armed_; }

  void Arm();
  void Disarm();

  void PrintStatus(FILE* f) const;

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

  static int64_t get_validation_failed_count();

 private:
  // The number of bytes to fill/validate.
  size_t fill_size_ = PAGE_SIZE;

  Action action_ = DefaultAction;

  bool armed_ = false;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PMM_CHECKER_H_
