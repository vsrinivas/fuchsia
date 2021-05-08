// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/descriptor-regs.h>
#include <lib/arch/x86/standard-segments.h>
#include <zircon/assert.h>

namespace arch {

void X86StandardSegments::Init() {
  gdt_.code64.MakeCode64();  // Initialize code segment.

  gdt_.tss64  // Initialize TSS.
      .set_present(true)
      .set_type(arch::SystemSegmentDesc64::SegmentType::TSS_AVAILABLE)
      .set_base(reinterpret_cast<uintptr_t>(&tss_))
      .set_limit(sizeof(tss_) - 1);
}

arch::GdtRegister64 X86StandardSegments::gdt_pointer() {
  return {
      .limit = sizeof(gdt_) - 1,
      .base = reinterpret_cast<uintptr_t>(&gdt_),
  };
}

#ifdef __x86_64__
void X86StandardSegments::Load() {
  // Initialize the tables.
  Init();

  // Install the new GDT.
  arch::LoadGdt({gdt_pointer()});

  // Switch to the code segment descriptor in the new GDT.
  arch::LoadCodeSegmentSelector(kCs64);

  // Install the new Task State Segment.
  arch::LoadTaskRegister64(kTr64);
}
#endif

}  // namespace arch
