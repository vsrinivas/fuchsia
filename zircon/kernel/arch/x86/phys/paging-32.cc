// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/extension.h>
#include <lib/arch/x86/system.h>
#include <lib/page-table/types.h>
#include <zircon/assert.h>

#include <hwreg/x86msr.h>
#include <phys/allocation.h>
#include <phys/arch.h>
#include <phys/symbolize.h>

#include "address-space.h"

namespace {

// On x86-32, the page tables are set up before paging is enabled, so there is
// no bootstrapping issue with accessing page table memory.  Conversely, the
// fixed .bss location based on the fixed 1 MiB load address may overlap with
// areas that should be reserved.  So it's preferable to go directly to the
// physical page allocator that respects explicitly reserved ranges.

class AllocationMemoryManager final : public page_table::MemoryManager {
 public:
  explicit AllocationMemoryManager(memalloc::Allocator& allocator) : allocator_(allocator) {}

  ktl::byte* Allocate(size_t size, size_t alignment) final {
    auto result = allocator_.Allocate(size, alignment);
    if (result.is_error()) {
      printf("Cannot allocate %zu bytes @ %zu for bootstrap page tables!\n", size, alignment);
      return nullptr;
    }
    return reinterpret_cast<ktl::byte*>(static_cast<uintptr_t>(result.value()));
  }

  page_table::Paddr PtrToPhys(ktl::byte* ptr) final {
    // We have a 1:1 virtual/physical mapping.
    return page_table::Paddr(reinterpret_cast<uintptr_t>(ptr));
  }

  ktl::byte* PhysToPtr(page_table::Paddr phys) final {
    // We have a 1:1 virtual/physical mapping.
    return reinterpret_cast<ktl::byte*>(static_cast<uintptr_t>(phys.value()));
  }

 private:
  memalloc::Allocator& allocator_;
};

}  // namespace

void ArchSetUpAddressSpace(memalloc::Allocator& allocator, const zbitl::MemRangeTable& table) {
  ZX_ASSERT_MSG(arch::BootCpuid<arch::CpuidAmdFeatureFlagsD>().lm(),
                "CPU does not support 64-bit mode!");
  ZX_ASSERT_MSG(arch::BootCpuid<arch::CpuidFeatureFlagsD>().pse(), "x86-64 requires PSE support!");
  ZX_ASSERT_MSG(arch::BootCpuid<arch::CpuidFeatureFlagsD>().pae(), "x86-64 requires PAE support!");
  ZX_ASSERT_MSG(arch::BootCpuid<arch::CpuidAmdFeatureFlagsD>().nx(), "x86-64 requires NX support!");
  ZX_ASSERT_MSG(arch::BootCpuid<arch::CpuidFeatureFlagsD>().fxsr(), "x86-64 requires SSE support!");

  // Configure the CPU for the 64-bit (4-level) style of page tables.  The
  // LME and PAE bits together enable the 64-bit page table format even
  // when executing in 32-bit mode.  OSFXSR enables SSE instructions, which
  // x86-64 CPUs always support; the compiler might generate those under
  // the -msse switch, which is necessary for it to generate the cmpxchg8b
  // instruction, which is used in the page-table code.
  hwreg::X86MsrIo msr;
  auto efer = arch::X86ExtendedFeatureEnableRegisterMsr::Get().ReadFrom(&msr);
  efer.set_lme(true)  // Enable Long mode (x86-64).
      .set_nxe(true)  // Enable No-Execute bit in page table entries.
      .WriteTo(&msr);
  arch::X86Cr4::Read()
      .set_pse(true)     // Enable 32-bit 4M pages, required for 64-bit.
      .set_pae(true)     // Enable 64-bit-wide page table entries.
      .set_osfxsr(true)  // Enable SSE-related instructions.
      .set_la57(false)   // 4-level, not 5-level.
      .Write();

  // Set up the identity-mapping page tables.  This installs the %cr3 pointer.
  AllocationMemoryManager pt_allocator(allocator);
  InstallIdentityMapPageTables(pt_allocator, table);

  // Now actually turn on paging.  This affects us immediately in 32-bit mode,
  // as well as being mandatory for 64-bit mode.
  printf("%s: Enabling MMU with x86-64 page tables... ", Symbolize::kProgramName_);
  arch::X86Cr0::Read().set_pg(true).Write();

  ZX_ASSERT(efer.ReadFrom(&msr).lma());
  printf("Long mode active!\n");
}
