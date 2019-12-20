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

constexpr uint64_t kCountsMagic = 0x0023766f436e6153ULL;  // "SanCov#" (LE)

using Guard = ktl::atomic<uint32_t>;
static_assert(sizeof(Guard) == sizeof(uint32_t));

using Count = ktl::atomic<uint64_t>;
static_assert(sizeof(Count) == sizeof(uint64_t));

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
// Likewise for __sancov_pc_counts.
extern Guard __start___sancov_guards[], __stop___sancov_guards[];
extern uintptr_t __sancov_pc_table[];
extern Count __sancov_pc_counts[];

// This is run along with static constructors, pretty early in startup.
// It's always run on the boot CPU before secondary CPUs are started up.
void __sanitizer_cov_trace_pc_guard_init(Guard* start, Guard* end) {
  // The compiler generates a call to this in every TU but via COMDAT so
  // there is only one.
  ZX_ASSERT(__sancov_pc_table[0] == 0);

  // It's always called with the bounds of the section, which for the
  // kernel are known statically anyway.
  ZX_ASSERT(start == __start___sancov_guards);
  ZX_ASSERT(end == __stop___sancov_guards);

  // Index 0 is special.  The first slot is reserved for the magic number.
  __sancov_pc_table[0] = kMagic64;
  __sancov_pc_counts[0].store(kCountsMagic, ktl::memory_order_relaxed);
}

// This is called every time through a covered event.
// This might be run before __sanitizer_cov_trace_pc_guard_init has run.
void __sanitizer_cov_trace_pc_guard(Guard* guard) {
  // Compute the table index based just on the address of the guard.
  // The __sancov_pc_table and __sancov_pc_counts arrays parallel the guards,
  // but the first slot in each of those is reserved for the magic number.
  size_t idx = guard - __start___sancov_guards + 1;

  // Every time through, increment the counter.
  __sancov_pc_counts[idx].fetch_add(1, ktl::memory_order_relaxed);

  // Use the guard as a simple flag to indicate whether the PC has been stored.
  if (unlikely(guard->load(ktl::memory_order_relaxed) == 0) &&
      likely(guard->exchange(1, ktl::memory_order_relaxed) == 0)) {
    // This is really the first time through this PC on any CPU.
    // This is now the only path that will ever use this slot in
    // the table, so storing there doesn't need to be atomic.
    __sancov_pc_table[idx] =
        // Take the raw return address.
        reinterpret_cast<uintptr_t>(__builtin_return_address(0)) -
        // Adjust it to point into the call instruction.
        kReturnAddressBias -
        // Adjust it from runtime to link-time addresses so no
        // further adjustment is needed to decode the data.
        reinterpret_cast<uintptr_t>(__code_start) + KERNEL_BASE;
  }
}

}  // extern "C"

}  // namespace
