// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_STANDARD_SEGMENTS_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_STANDARD_SEGMENTS_H_

#include <lib/arch/x86/descriptor.h>

#include <type_traits>

namespace arch {

// This defines the standard x86-64 segmentation setup for 64-bit code only.
// This is default-constructible without static constructors.
class X86StandardSegments {
 public:
  // Install the new GDT and TSS and switch to the new 64-bit code segment.
  // (This is only provided on actual x86-64 hardware.)
  void Load();

  // Install the new GDT and TSS and switch to the new 64-bit code segment at
  // the given absolute entry point, with the argument value in %rsi.
  // (This is only provided on actual x86 hardware, both 64-bit and 32-bit.)
  [[noreturn]] void Load(uintptr_t entry, uintptr_t arg);

 private:
  struct Gdt64 {
    arch::Desc32 null;                // Null descriptor.
    arch::Desc32 code64;              // 64-bit code descriptor.
    arch::SystemSegmentDesc64 tss64;  // 64-bit TSS descriptor (double slot).
  };
  static_assert(std::is_standard_layout_v<Gdt64>);

  // Return the %cs selector for 64-bit code.
  static constexpr auto kCs64 =
      arch::SegmentSelector::FromGdtIndex(offsetof(Gdt64, code64) / sizeof(arch::Desc32));

  // Return the TSS selector for LTR.
  static constexpr auto kTr64 =
      arch::SegmentSelector::FromGdtIndex(offsetof(Gdt64, tss64) / sizeof(arch::Desc32));

  void Init();

  // Return the GDT pointer to load with LGDT.
  arch::GdtRegister64 gdt_pointer();

  Gdt64 gdt_{};
  arch::TaskStateSegment64 tss_{};
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_STANDARD_SEGMENTS_H_
