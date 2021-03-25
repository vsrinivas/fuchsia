// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_REGS_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_REGS_H_

#include <lib/arch/x86/descriptor.h>
#include <zircon/assert.h>

namespace arch {

// Load the system Task Register.
//
// `selector` should be an index in the GDT containing a SystemSegmentDesc64 entry of type
// SegmentType::TSS_AVAILABLE.
inline void LoadTaskRegister64(SegmentSelector selector) {
  // Load the task register.
  __asm__ __volatile__("ltr %w0"
                       : /* no outputs */
                       : "r"(selector.raw)
                       : "memory"  // Ensure compiler writes out changes to GDT/TSS.
  );
}

// Load the x86-64 GDT register to the given value.
inline void LoadGdt(const GdtRegister64& gdt) {
  // Load the GDT.
  __asm__ __volatile__("lgdt %0"
                       : /* no outputs */
                       : "m"(gdt)
                       : "memory"  // Ensure compiler writes out changes to GDT.
  );
}
inline void LoadGdt(const AlignedGdtRegister64& gdt) {
  LoadGdt(gdt.reg);
}

// Activate the given code selector and data selector.
//
// The two selectors should be indexes into the currently loaded GDT.
extern "C" void LoadCodeSegmentSelector(SegmentSelector code_segment);

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_DESCRIPTOR_REGS_H_
