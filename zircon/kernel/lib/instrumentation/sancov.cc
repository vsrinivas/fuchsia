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

#include <ktl/atomic.h>
#include <ktl/span.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_paged.h>

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
extern uintptr_t __sancov_pc_table[], __sancov_pc_table_end[], __sancov_pc_table_vmo_end[];
extern Count __sancov_pc_counts[], __sancov_pc_counts_end[], __sancov_pc_counts_vmo_end[];

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

// These are kept alive forever to keep a permanent reference to the VMO so
// that the memory always remains valid, even if userspace closes the last
// handle.
fbl::RefPtr<VmObjectPaged> gSancovPcVmo, gSancovCountsVmo;

}  // namespace

InstrumentationDataVmo SancovGetPcVmo() {
  ktl::span contents(__sancov_pc_table, __sancov_pc_table_end);
  ktl::span contents_vmo(__sancov_pc_table, __sancov_pc_table_vmo_end);
  if (contents.empty()) {
    return {};
  }

  // This is kept alive forever to keep a permanent reference to the VMO so
  // that the memory always remains valid, even if userspace closes the last
  // handle.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateFromWiredPages(contents_vmo.data(),
                                                           contents_vmo.size_bytes(), false, &vmo);
  ZX_ASSERT(status == ZX_OK);

  gSancovPcVmo = vmo;

  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> handle;
  status =
      VmObjectDispatcher::Create(ktl::move(vmo), contents.size_bytes(),
                                 VmObjectDispatcher::InitialMutability::kMutable, &handle, &rights);
  ZX_ASSERT(status == ZX_OK);
  handle.dispatcher()->set_name(kPcVmoName.data(), kPcVmoName.size());

  return {
      .announce = "SanitizerCoverage",
      .sink_name = "sancov",
      .units = "PCs",
      .scale = sizeof(__sancov_pc_table[0]),
      .handle = Handle::Make(ktl::move(handle), rights & ~ZX_RIGHT_WRITE).release(),
  };
}

InstrumentationDataVmo SancovGetCountsVmo() {
  ktl::span contents(__sancov_pc_counts, __sancov_pc_counts_end);
  ktl::span contents_vmo(__sancov_pc_counts, __sancov_pc_counts_vmo_end);
  if (contents.empty()) {
    return {};
  }

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateFromWiredPages(contents_vmo.data(),
                                                           contents_vmo.size_bytes(), false, &vmo);
  ZX_ASSERT(status == ZX_OK);

  gSancovCountsVmo = vmo;

  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> handle;
  status =
      VmObjectDispatcher::Create(ktl::move(vmo), contents.size_bytes(),
                                 VmObjectDispatcher::InitialMutability::kMutable, &handle, &rights);
  ZX_ASSERT(status == ZX_OK);
  handle.dispatcher()->set_name(kCountsVmoName.data(), kCountsVmoName.size());

  return {
      .announce = "SanitizerCoverage Counts",
      .sink_name = "sancov-counts",
      .units = "counters",
      .scale = sizeof(__sancov_pc_counts[0]),
      .handle = Handle::Make(ktl::move(handle), rights & ~ZX_RIGHT_WRITE).release(),
  };
}

#endif  // HAVE_SANCOV
