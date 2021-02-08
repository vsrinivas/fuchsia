// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_

#include <lib/arch/sysreg.h>

#include <optional>

#include <hwreg/bitfields.h>

namespace arch {

// This file defines hwreg accessor types for some of the AArch64 system
// registers used for the top-level generic control things.
//
// The names here are approximately the expanded names used in the [arm/sysreg]
// manual text.  This only defines the bit layouts and can be used portably.
// The ARCH_SYSREG types used to access the registers directly on hardware are
// declared in <lib/arch/sysreg.h>.  Both headers must be included to use the
// accessors for specific registers with the right layout types.

// [arm/sysreg]/currentel: CurrentEL, Current Exception Level
struct ArmCurrentEl : public SysRegBase<ArmCurrentEl> {
  DEF_FIELD(3, 2, el);
};

ARCH_ARM64_SYSREG(ArmCurrentEl, "CurrentEL");

// This type covers three register formats:
//  * [arm/sysreg]/sctlr_el1: System Control Register (EL1)
//  * [arm/sysreg]/sctlr_el2: System Control Register (EL2)
//  * [arm/sysreg]/sctlr_el3: System Control Register (EL3)
// Some fields (mostly things relating to EL0) are only used in EL1 and are
// reserved in the other registers.  Missing bits are reserved in all cases.
struct ArmSystemControlRegister : public SysRegDerivedBase<ArmSystemControlRegister> {
  enum class TagCheckFault : uint64_t {
    kNone = 0b00,             // Faults have no effect.
    kSynchronous = 0b01,      // All faults cause a synchronous exception.
    kAsynchronous = 0b10,     // All faults accumulate asynchronously.
    kSynchronousRead = 0b11,  // Synchronous for read, asynchronous for write.
  };

  std::optional<uint64_t> twedel_cycles() const {
    if (tweden()) {
      return 1u << twedel() << 8;  // This is the minimum delay in cycles.
    }
    return std::nullopt;  // Implementation-defined.
  }

  DEF_BIT(57, epan);                            // EL1
  DEF_BIT(56, enals);                           // EL1
  DEF_BIT(55, enas0);                           // EL1
  DEF_BIT(54, enasr);                           // EL1
  DEF_FIELD(49, 46, twedel);                    // EL1
  DEF_BIT(45, tweden);                          // EL1
  DEF_BIT(44, dsbss);                           // EL1, EL2, EL3
  DEF_BIT(43, ata);                             // EL1, EL2, EL3
  DEF_BIT(42, ata0);                            // EL1
  DEF_ENUM_FIELD(TagCheckFault, 41, 40, tcf);   // EL1, EL2, EL3
  DEF_ENUM_FIELD(TagCheckFault, 39, 38, tcf0);  // EL1
  DEF_BIT(37, itfsb);                           // EL1, EL2, EL3
  DEF_BIT(36, bt);                              // EL1, EL2, EL3
  DEF_BIT(35, bt0);                             // EL1
  DEF_BIT(31, enia);                            // EL1, EL2, EL3
  DEF_BIT(30, enib);                            // EL1, EL2, EL3
  DEF_BIT(29, lsmaoc);                          // EL1
  DEF_BIT(28, ntlsmd);                          // EL1
  DEF_BIT(27, enda);                            // EL1, EL2, EL3
  DEF_BIT(26, uci);                             // EL1
  DEF_BIT(25, ee);                              // EL1, EL2, EL3
  DEF_BIT(24, e0e);                             // EL1
  DEF_BIT(23, span);                            // EL1
  DEF_BIT(22, eis);                             // EL1, EL2, EL3
  DEF_BIT(21, iesb);                            // EL1, EL2, EL3
  DEF_BIT(20, tscxt);                           // EL1
  DEF_BIT(19, wxn);                             // EL1, EL2, EL3
  DEF_BIT(18, ntwe);                            // EL1
  DEF_BIT(16, ntwi);                            // EL1
  DEF_BIT(15, uct);                             // EL1
  DEF_BIT(14, dze);                             // EL1, EL2, EL3
  DEF_BIT(13, endb);                            // EL1, EL2, EL3
  DEF_BIT(12, i);                               // EL1, EL2, EL3
  DEF_BIT(11, eos);                             // EL1, EL2, EL3
  DEF_BIT(10, enrctx);                          // EL1
  DEF_BIT(9, uma);                              // EL1
  DEF_BIT(8, sed);                              // EL1
  DEF_BIT(7, itd);                              // EL1
  DEF_BIT(6, naa);                              // EL1, EL2, EL3
  DEF_BIT(5, cp15ben);                          // EL1
  DEF_BIT(4, sa0);                              // EL1
  DEF_BIT(3, sa);                               // EL1, EL2, EL3
  DEF_BIT(2, c);                                // EL1, EL2, EL3
  DEF_BIT(1, a);                                // EL1, EL2, EL3
  DEF_BIT(0, m);                                // EL1, EL2, EL3
};

struct ArmSctlrEl1 : public arch::SysRegDerived<ArmSctlrEl1, ArmSystemControlRegister> {};
ARCH_ARM64_SYSREG(ArmSctlrEl1, "sctlr_el1");

struct ArmSctlrEl2 : public arch::SysRegDerived<ArmSctlrEl2, ArmSystemControlRegister> {};
ARCH_ARM64_SYSREG(ArmSctlrEl2, "sctlr_el2");

struct ArmSctlrEl3 : public arch::SysRegDerived<ArmSctlrEl3, ArmSystemControlRegister> {};
ARCH_ARM64_SYSREG(ArmSctlrEl3, "sctlr_el3");

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_
