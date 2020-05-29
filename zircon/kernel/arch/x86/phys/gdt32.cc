// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/descriptor.h>

#include <array>

namespace {

enum Segments { kNull, kCode32, kData32, kGs32, kCode64, kNumEntries };

constexpr auto MakePhys32Gdt() {
  std::array<arch::Desc32, kNumEntries> gdt{};

  gdt[kCode32].make_flat().set_type(arch::Desc32::CODE_RX);
  gdt[kCode64].make_flat().set_type(arch::Desc32::CODE_RX).set_long_mode(true);
  gdt[kData32].make_flat().set_type(arch::Desc32::DATA_RW);
  gdt[kGs32].make_flat().set_type(arch::Desc32::DATA_RW);

  return gdt;
}

}  // namespace

#if __cplusplus > 201703L
#define CONSTINIT constinit
#elif defined(__clang__)
#define CONSTINIT [[clang::require_constant_initialization]]
#else
#define CONSTINIT  // Cross fingers.
#endif

extern "C" CONSTINIT const auto gPhys32Gdt = MakePhys32Gdt();

#ifdef GENERATE
#include <hwreg/asm.h>

int main(int argc, char** argv) {
  constexpr auto gs_offset = kGs32 * sizeof(arch::Desc32);

  return hwreg::AsmHeader()
      .Macro("PHYS32_GDT_SIZE", sizeof(gPhys32Gdt))
      .Macro("PHYS32_GS_BASE_LO16_OFFSET", gs_offset + 2)
      .Macro("PHYS32_GS_BASE_MID8_OFFSET", gs_offset + 4)
      .Macro("PHYS32_GS_BASE_HI8_OFFSET", gs_offset + 7)
      .Macro("PHYS32_CODE32_SEL", kCode32 << 3)
      .Macro("PHYS32_DATA32_SEL", kData32 << 3)
      .Macro("PHYS32_GS32_SEL", kGs32 << 3)
      .Macro("PHYS32_CODE64_SEL", kCode64 << 3)
      .Main(argc, argv);
}
#endif
