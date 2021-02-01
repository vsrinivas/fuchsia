// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/msr.h>

#include <hwreg/asm.h>

int main(int argc, char** argv) {
  return hwreg::AsmHeader()
      .Macro("MSR_IA32_FS_BASE", static_cast<uint32_t>(arch::X86Msr::IA32_FS_BASE))
      .Macro("MSR_IA32_GS_BASE", static_cast<uint32_t>(arch::X86Msr::IA32_GS_BASE))
      .Macro("MSR_IA32_KERNEL_GS_BASE", static_cast<uint32_t>(arch::X86Msr::IA32_KERNEL_GS_BASE))
      .Main(argc, argv);
}
