// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "private.h"

#ifndef HAVE_SANCOV
#error "build system regression"
#endif

#if !HAVE_SANCOV

InstrumentationDataVmo SancovGetPcVmo() { return {}; }
InstrumentationDataVmo SancovGetCountsVmo() { return {}; }

#else  // HAVE_SANCOV

#include <stdint.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <lk/init.h>
#include <vm/vm_object_paged.h>

#include "kernel-mapped-vmo.h"

namespace {

// The sancov file format is trivial: magic number and array of PCs.
// Each word after the first is either 0 or a PC that was hit.

// TODO(mcgrathr): Move the constant into a header shared with other impls.
constexpr uint64_t kMagic64 = 0xC0BFFFFFFFFFFF64ULL;

constexpr uint64_t kCountsMagic = 0x0023766f436e6153ULL;  // "SanCov#" (LE)

// The sancov tool matches "<binaryname>" to "<binaryname>.%u.sancov".
constexpr ktl::string_view kPcVmoName = "data/zircon.elf.1.sancov";

// This follows the sancov PCs file name just for consistency.
constexpr ktl::string_view kCountsVmoName = "data/zircon.elf.1.sancov-counts";

// Go back from the return address to the call site.
// Note this must exactly match the calculation in the sancov tool.
#ifdef __aarch64__
// Fixed-size instructions, so go back to the previous instruction exactly.
constexpr uintptr_t kReturnAddressBias = 4;
#else
// Variable-sized instructions, so just go back one byte into the middle.
constexpr uintptr_t kReturnAddressBias = 1;
#endif

// These are defined by the linker script.  The __sancov_guards section is
// populated by the compiler with one slot corresponding to each instrumented
// PC location.
extern "C" uint32_t __start___sancov_guards[], __stop___sancov_guards[];

[[gnu::const]] size_t GuardsCount() { return __stop___sancov_guards - __start___sancov_guards; }

[[gnu::const]] size_t DataSize() { return (GuardsCount() + 1) * sizeof(uint64_t); }

// Instrumented code runs from the earliest point, before initialization.  The
// memory for storing the PCs and counts hasn't been set up.  However, code is
// running only on the boot CPU.  So in the pre-initialization period, we
// accumulate 32-bit counts in the __sancov_guard slots.  Then after the full
// buffers are set up, we copy those counts into the 64-bit counter slots and
// re-zero all the guard slots.  Thereafter, each guard slot serves as an
// atomic flag indicating whether its corresponding PC has been stored yet.
// This way, no early PC hits are lost in the counts.  However, for PCs whose
// only hits were before buffer setup, the nonzero counts will be paired with
// zero PC slots because the PC values are only saved in the real buffers.

uint64_t* gSancovPcTable = nullptr;
uint64_t* gSancovPcCounts = nullptr;

KernelMappedVmo gSancovPcVmo, gSancovCountsVmo;

void InitSancov(uint level) {
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, DataSize(), &vmo);
  ZX_ASSERT(status == ZX_OK);
  status = gSancovPcVmo.Init(ktl::move(vmo), 0, DataSize(), "sancov-pc-table");
  ZX_ASSERT(status == ZX_OK);

  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, DataSize(), &vmo);
  ZX_ASSERT(status == ZX_OK);
  status = gSancovCountsVmo.Init(ktl::move(vmo), 0, DataSize(), "sancov-pc-counts-table");
  ZX_ASSERT(status == ZX_OK);

  gSancovPcTable = reinterpret_cast<uint64_t*>(gSancovPcVmo.base());
  gSancovPcCounts = reinterpret_cast<uint64_t*>(gSancovCountsVmo.base());

  gSancovPcTable[0] = kMagic64;
  gSancovPcCounts[0] = kCountsMagic;

  // Move the counts accumulated in the guard slots into their proper places,
  // and reset the guards.
  for (size_t i = 0; i < GuardsCount(); ++i) {
    gSancovPcCounts[i + 1] = ktl::exchange(__start___sancov_guards[i], 0);
  }

  // Just in case of LTO or whatnot, ensure that everything is in place before
  // returning to run any instrumented code.
  ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
}

// This needs to happen after the full VM system is available, but while the
// kernel is still running only in the initial thread on the boot CPU.
LK_INIT_HOOK(InitSancov, InitSancov, LK_INIT_LEVEL_KERNEL)

}  // namespace

extern "C" {

// This is run along with static constructors, pretty early in startup.
// It's always run on the boot CPU before secondary CPUs are started up.
void __sanitizer_cov_trace_pc_guard_init(uint32_t* start, uint32_t* end) {
  // It's always called with the bounds of the section, which for the
  // kernel are known statically anyway.
  ZX_ASSERT(start == __start___sancov_guards);
  ZX_ASSERT(end == __stop___sancov_guards);
}

// This is called every time through a covered event.
// This might be run before __sanitizer_cov_trace_pc_guard_init has run.
void __sanitizer_cov_trace_pc_guard(uint32_t* guard_ptr) {
  // Compute the table index based just on the address of the guard.
  // The gSancovPcTable and gSancovPcCounts arrays parallel the guards,
  // but the first slot in each of those is reserved for the magic number.
  const size_t idx = guard_ptr - __start___sancov_guards + 1;

  if (!gSancovPcCounts) [[unlikely]] {
    // Pre-initialization, just count the hit in the guard slot.  See above.
    ++*guard_ptr;
    return;
  }

  // Every time through, increment the counter.
  ktl::atomic_ref count(gSancovPcCounts[idx]);
  count.fetch_add(1, ktl::memory_order_relaxed);

  // Use the guard as a simple flag to indicate whether the PC has been stored.
  ktl::atomic_ref guard(*guard_ptr);
  if (unlikely(guard.load(ktl::memory_order_relaxed) == 0) &&
      likely(guard.exchange(1, ktl::memory_order_relaxed) == 0)) {
    // This is really the first time through this PC on any CPU.
    // This is now the only path that will ever use this slot in
    // the table, so storing there doesn't need to be atomic.
    gSancovPcTable[idx] =
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

InstrumentationDataVmo SancovGetPcVmo() {
  return {
      .announce = "SanitizerCoverage",
      .sink_name = "sancov",
      .units = "PCs",
      .scale = sizeof(gSancovPcTable[0]),
      .handle = gSancovPcVmo.Publish(kPcVmoName, DataSize()),
  };
}

InstrumentationDataVmo SancovGetCountsVmo() {
  return {
      .announce = "SanitizerCoverage Counts",
      .sink_name = "sancov-counts",
      .units = "counters",
      .scale = sizeof(gSancovPcCounts[0]),
      .handle = gSancovCountsVmo.Publish(kCountsVmoName, DataSize()),
  };
}

#endif  // HAVE_SANCOV
