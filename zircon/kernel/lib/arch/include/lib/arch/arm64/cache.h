// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_CACHE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_CACHE_H_

#ifndef __ASSEMBLER__

#include <lib/arch/sysreg.h>

#include <hwreg/bitfields.h>

namespace arch {

enum class ArmL1ICachePolicy : uint8_t {
  VPIPT = 0b00,
  AIVIVT = 0b01,
  VIPT = 0b10,
  PIPT = 0b11,
};

// [arm/v8]: D13.2.33  CTR_EL0, Cache Type Register.
struct CacheTypeEl0 : public SysRegBase<CacheTypeEl0> {
  DEF_RSVDZ_FIELD(63, 38);
  DEF_FIELD(37, 32, tmin_line);
  // Bit 31 is reserved as 1.
  DEF_RSVDZ_BIT(30);
  DEF_BIT(29, dic);
  DEF_BIT(28, idc);
  DEF_FIELD(27, 24, cwg);
  DEF_FIELD(23, 20, erg);
  DEF_FIELD(19, 16, dmin_line);
  DEF_ENUM_FIELD(ArmL1ICachePolicy, 15, 14, l1_ip);
  DEF_RSVDZ_FIELD(13, 4);
  DEF_FIELD(3, 0, imin_line);

  // `dmin_line` gives log2 of the number of words in the smallest data cache
  // line. Similarly so for `imin_line`.
  size_t dcache_line_size() const { return (1 << dmin_line()) * sizeof(uint32_t); }
  size_t icache_line_size() const { return (1 << imin_line()) * sizeof(uint32_t); }
};

ARCH_ARM64_SYSREG(CacheTypeEl0, "ctr_el0");

}  // namespace arch

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_CACHE_H_
