// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_SYSTEM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_SYSTEM_H_

#include <lib/arch/internal/bits.h>
#include <lib/arch/sysreg.h>

#include <optional>

#include <hwreg/bitfields.h>

namespace arch {

// This file defines hwreg accessor types for some of the Riscv64 system
// registers used for the top-level generic control things.
//
// The names here are approximately the expanded names used in the
// manual text.  This only defines the bit layouts and can be used portably.
// The ARCH_SYSREG types used to access the registers directly on hardware are
// declared in <lib/arch/sysreg.h>.  Both headers must be included to use the
// accessors for specific registers with the right layout types.

// MMU Modes
enum class RiscvSatpModeValue {
  kNone = 0,
  kSv32 = 1,
  kSv39 = 8,
  kSv48 = 9,
  kSv57 = 10,
  kSv64 = 11,
};

struct RiscvSatp : public SysRegBase<RiscvSatp, uint64_t, hwreg::EnablePrinter> {
  DEF_ENUM_FIELD(RiscvSatpModeValue, 63, 60, mode);
  DEF_FIELD(59, 44, asid);
  DEF_FIELD(43, 0, ppn);
};
ARCH_RISCV64_SYSREG(RiscvSatp, "satp");

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_RISCV64_SYSTEM_H_
