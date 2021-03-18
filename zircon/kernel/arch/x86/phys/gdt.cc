// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/descriptor-regs.h>
#include <lib/arch/x86/descriptor.h>

#include <phys/arch.h>

namespace {

// System-wide Global Descriptor Table.
struct Gdt {
  // Null descriptor.
  arch::Desc32 null{};

  // 64-bit code descriptor.
  arch::Desc32 code64 = arch::Desc32{}
                            .set_present(1)
                            .set_base(0)
                            .set_limit(0xf'ffff)
                            .set_system(arch::Desc32::NONSYSTEM)
                            .set_type(arch::Desc32::CODE_RX)
                            .set_long_mode(1)
                            .set_granularity(1);

  // 64-bit TSS descriptor (16 bytes / two slots).
  //
  // Initialized in `ArchSetUp`.
  arch::SystemSegmentDesc64 tss64{};

} __ALIGNED(8) __PACKED;
static_assert(std::is_standard_layout<Gdt>::value);

// Selectors for the various entries.
constexpr arch::SegmentSelector kCode64 =
    arch::SegmentSelector::FromGdtIndex(offsetof(Gdt, code64) / sizeof(arch::Desc32));
constexpr arch::SegmentSelector kTss64 =
    arch::SegmentSelector::FromGdtIndex(offsetof(Gdt, tss64) / sizeof(arch::Desc32));

// Global GDT instance.
Gdt g_physboot_gdt;

// Global TSS instance.
arch::TaskStateSegment64 g_global_tss;

}  // namespace

void ArchSetUp() {
  // Set up the `base` pointer in the TSS64 GDT entry.
  //
  // We can't set up tss64 statically because of the non-trivial bit
  // packing required by `set_base` with a non-constant value.
  g_physboot_gdt.tss64 = arch::SystemSegmentDesc64{}
                             .set_present(1)
                             .set_type(arch::SystemSegmentDesc64::SegmentType::TSS_AVAILABLE)
                             .set_base(reinterpret_cast<uintptr_t>(&g_global_tss))
                             .set_limit(sizeof(arch::TaskStateSegment64) - 1);

  // Install the new GDT.
  arch::GdtRegister64 gdt_reg = {
      .limit = sizeof(g_physboot_gdt) - 1,
      .base = reinterpret_cast<uint64_t>(&g_physboot_gdt),
  };
  arch::LoadGdt(gdt_reg);

  // Activate the new GDT.
  arch::LoadCodeSegmentSelector(kCode64);

  // Install the new task state segment.
  arch::LoadTaskRegister64(kTss64);
}
