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

void X86StandardSegments::Load(uintptr_t entry, uintptr_t arg) {
  // Initialize the tables.
  Init();

#ifdef __x86_64__

  // Install the new GDT.
  arch::LoadGdt({gdt_pointer()});

  // Install the new Task State Segment.
  arch::LoadTaskRegister64(kTr64);

  // Do a far jump via far return since AMD processors don't handle 64-bit
  // offsets in ljmpq.  It's OK that this clobbers the stack because it never
  // returns anyway.  The frame pointer is cleared to avoid leaving any
  // misleading breadcrumbs for the new code.
  __asm__ volatile(
      R"""(
      push %[cs]
      .cfi_adjust_cfa_offset 8
      push %[pc]
      .cfi_adjust_cfa_offset 8
      xor %%ebp, %%ebp
      lretq
      .cfi_adjust_cfa_offset -16
      )"""
      :
      : [cs] "ir"(kCs64), [pc] "ir"(entry),  // lretq
        "S"(arg)                             // %rsi
      : "cc", "memory");

#else

  // In 32-bit mode, load the GDT and jump into the new code segment all in one
  // asm() since we can't mix 32-bit and 64-bit code from the compiler.
  const auto gdt64 = gdt_pointer();
  const struct {
    [[gnu::packed, gnu::aligned(2)]] uint16_t limit;
    [[gnu::packed, gnu::aligned(2)]] uint32_t base;
  } gdt_ptr = {gdt64.limit, static_cast<uint32_t>(gdt64.base)};
  ZX_ASSERT(gdt_ptr.base == gdt64.base);

  // Clear the stack and frame pointers so no misleading breadcrumbs are left.
  // But do those last in case the input operands use them.
  __asm__ volatile(
      R"""(
      cld
      cli
      lgdt %[gdt]
      ljmpl %w[cs], $0f
0:    .code64
      ltr %w[tr]
      xor %%ebp, %%ebp
      xor %%esp, %%esp
      jmp *%%rax
      .code32
      )"""
      :
      : [gdt] "m"(gdt_ptr),              // lgdt
        [tr] "r"(kTr64.raw), "m"(tss_),  // ltr
        [cs] "i"(kCs64.raw),             // ljmp
        "a"(entry),                      // jmp *%rax
        "S"(arg)                         // %rsi
      :  // The blanket "memory" clobber ensures that any stores to data
         // pointed to by the argument register, or the image itself, has
         // certainly been written first and not considered a dead store.
      "memory");

#endif

  __builtin_unreachable();
}

}  // namespace arch
