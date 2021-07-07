// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/system.h>

#include <hwreg/asm.h>

int main(int argc, char** argv) {
  return hwreg::AsmHeader()  //
      .Register<arch::ArmSctlrEl1>("SCTLR_EL1_")
      .Register<arch::ArmSctlrEl2>("SCTLR_EL2_")
      .Main(argc, argv);
}
