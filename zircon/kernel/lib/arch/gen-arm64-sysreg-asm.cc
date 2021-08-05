// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/feature.h>
#include <lib/arch/arm64/system.h>

#include <hwreg/asm.h>

int main(int argc, char** argv) {
  return hwreg::AsmHeader()  //
      .Register<arch::ArmIdAa64IsaR0El1>("ID_AA64ISAR0_EL1_")
      .Register<arch::ArmCurrentEl>("CURRENT_EL_")
      .Register<arch::ArmDaif>("DAIF_")
      .Register<arch::ArmDaifSetClr>("DAIFSETCLR_")
      .Main(argc, argv);
}
