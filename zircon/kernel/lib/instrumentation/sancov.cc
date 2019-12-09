// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdint>

#include <ktl/atomic.h>
#include <vm/vm.h>

namespace {

// The sancov file format is trivial: magic number and array of PCs.
// Each word after the first is either 0 or a PC that was hit.

// TODO(mcgrathr): Move the constant into a header shared with other impls.
constexpr uint64_t kMagic64 = 0xC0BFFFFFFFFFFF64ULL;

using Guard = ktl::atomic<uint32_t>;
static_assert(sizeof(Guard) == sizeof(uint32_t));

// Go back from the return address to the call site.
// Note this must exactly match the calculation in the sancov tool.
#ifdef __aarch64__
// Fixed-size instructions, so go back to the previous instruction exactly.
constexpr uintptr_t kReturnAddressBias = 4;
#else
// Variable-sized instructions, so just go back one byte into the middle.
constexpr uintptr_t kReturnAddressBias = 1;
#endif

extern "C" {

// These are defined by the linker script.  The __sancov_guards section is
// populated by the compiler.  The __sancov_pc_table has one element for
// each element in __sancov_guards (statically allocated via linker script).
extern Guard __start___sancov_guards[], __stop___sancov_guards[];
extern uintptr_t __sancov_pc_table[];

void __sanitizer_cov_trace_pc_guard_init(Guard* start, Guard* end) {
  // The compiler generates a call to this in every TU but via COMDAT so
  // there is only one.
  ZX_ASSERT(start[1] == 0);

  // It's always called with the bounds of the section, which for the
  // kernel are known statically anyway.
  ZX_ASSERT(start == __start___sancov_guards);
  ZX_ASSERT(end == __stop___sancov_guards);

  // Index 0 is special.  The first slot is reserved for the magic number.
  __sancov_pc_table[0] = kMagic64;

  // Each guard is assigned a nonzero index into the PC table.  The
  // location that uses that guard will store its PC in this slot to
  // indicate it was covered.
  for (uint32_t i = 0; i < end - start; ++i) {
    start[i].store(i + 1, ktl::memory_order_relaxed);
  }
}

// This is called every time through a covered event.  The guard is zero
// before the initializer above has run, so any early hits are ignored.
// The first hit resets the guard to zero so later hits don't do anything.
// It then stores the covered PC value (i.e. this call's return address) in
// the slot in the table corresponding to the guard.
void __sanitizer_cov_trace_pc_guard(Guard* guard) {
  if (unlikely(guard->load(ktl::memory_order_relaxed) != 0)) {
    if (auto i = guard->exchange(0, ktl::memory_order_relaxed)) {
      // This is now the only path that will ever use this slot in
      // the table, so storing there doesn't need to be atomic.
      __sancov_pc_table[i] =
          // Take the raw return address.
          reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
          // Adjust it to point into the call instruction.
          kReturnAddressBias -
          // Adjust it from runtime to link-time addresses so no
          // further adjustment is needed to decode the data.
          reinterpret_cast<uintptr_t>(__code_start) + KERNEL_BASE;
    }
  }
}

}  // extern "C"

}  // namespace
