// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SYSTEM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SYSTEM_H_

#include <lib/arch/hwreg.h>
#include <lib/arch/intrin.h>
#include <lib/arch/sysreg.h>

#include <optional>

namespace arch {

// This file defines hwreg accessor types for the main x86 system registers.
// These are all here in this file since there are only a few.  Many more
// things are represented as MSR (see <lib/arch/x86/msr.h>).
//
// The names here are approximated from the Intel manual's wording introducing
// each register, since all the registers have opaque numeric names.  This only
// defines the bit layouts and can be used portably.  The HWREG_SYSREG types
// used to access the registers directly on hardware are declared in
// <lib/arch/sysreg.h>.  Both headers must be included to use the accessors for
// specific registers with the right layout types.

// [intel/vol3]: 2.5 Control Registers: CR0
struct X86Cr0 : public SysRegBase<X86Cr0, uint64_t> {
  DEF_RSVDZ_FIELD(63, 32);
  DEF_BIT(31, pg);  // Paging enabled
  DEF_BIT(30, cd);  // Cache disabled
  DEF_BIT(29, nw);  // Not write-through
  // Bits [28:19] are reserved.
  DEF_BIT(18, am);  // Alignment mask (support alignment checking)
  // Bit 17 is reserved.
  DEF_BIT(16, wp);  // Write protect (prevent supervisor writing to RO pages)
  // Bits [15:6] are reserved.
  DEF_BIT(5, ne);  // Numeric error (control FPU exceptions)
  DEF_BIT(4, et);  // Extension type (reserved on modern CPUs, always 1)
  DEF_BIT(3, ts);  // Task switched (trap on FPU/MMX/SSE/etc reg access)
  DEF_BIT(2, em);  // Emulation (trap on FPU/MMX/SSE/etc instructions)
  DEF_BIT(1, mp);  // Monitor Coprocessor
  DEF_BIT(0, pe);  // Protection Enable (enable protected mode)
};

ARCH_X86_SYSREG(X86Cr0, "cr0");

// There is no CR1.

// [intel/vol3]: 2.5 Control Registers: CR2
struct X86Cr2 : public SysRegBase<X86Cr2> {
  DEF_FIELD(63, 0, address);
};

ARCH_X86_SYSREG(X86Cr2, "cr2");

// [intel/vol3]: 2.5 Control Registers: CR3
struct X86Cr3 : public SysRegBase<X86Cr3> {
  DEF_UNSHIFTED_FIELD(63, 12, base);  // 4k-aligned physical byte address.

  // Bits [12:5] and [2:0] are reserved and ignored, but "assumed to be zero".
  // In case of future additions it's probably best to write them back as
  // written rather than RSVDZ them.

  DEF_BIT(4, pcd);  // Page-level Cache Disable
  DEF_BIT(3, pwt);  // Page-level Write-Through
};

ARCH_X86_SYSREG(X86Cr3, "cr3");

// [intel/vol3]: 2.5 Control Registers: CR4
struct X86Cr4 : public SysRegBase<X86Cr4> {
  DEF_RSVDZ_FIELD(63, 32);

  // The Intel manual lists these in ascending bit order instead of descending
  // bit order like most other control registers, so we follow suit.
  DEF_BIT(0, vme);         // Virtual-8086 Mode Extensions
  DEF_BIT(1, pvi);         // Protected-Mode Virtual Interrupts
  DEF_BIT(2, tsd);         // Time Stamp Disable
  DEF_BIT(3, de);          // Debugging Extensions
  DEF_BIT(4, pse);         // Page Size Extensions
  DEF_BIT(5, pae);         // Physical Address Extension
  DEF_BIT(6, mce);         // Machine-Check Enable
  DEF_BIT(7, pge);         // Page Global Enable
  DEF_BIT(8, pce);         // Performance-Monitoring Counter Enable
  DEF_BIT(9, osfxsr);      // OS supports FXSAVE and FXRSTOR
  DEF_BIT(10, osmmexcpt);  // OS supports unmasked SIMD FP Exceptions
  DEF_BIT(11, umip);       // User-Mode Instruction Prevention
  DEF_BIT(12, la57);       // 57-bit linear addresses
  DEF_BIT(13, vmxe);       // VMX-Enable Bit
  DEF_BIT(14, smxe);       // SMX-Enable Bit
  // Bit 15 is reserved.
  DEF_BIT(16, fsgsbase);  // FSGSBASE-Enable Bit
  DEF_BIT(17, pcide);     // PCID-Enable Bit
  DEF_BIT(18, osxsave);   // XSAVE and Processor Extended States-Enable Bit
  // Bit 19 is reserved.
  DEF_BIT(20, smep);  // SMEP-Enable Bit
  DEF_BIT(21, smap);  // SMAP-Enable Bit
  DEF_BIT(22, pke);   // Enable protection keys for user-mode pages
  DEF_BIT(23, cet);   // Control-flow Enforcement Technology
  DEF_BIT(24, pks);   // Enable protection keys for supervisor-mode pages

  // Bits [31:25] are reserved.
};

ARCH_X86_SYSREG(X86Cr4, "cr4");

// There is no CR5, CR6, or CR7.

// [Intel/vol3]: 2.5 Control Registers: CR8
struct X86Cr8 : SysRegBase<X86Cr8> {
  DEF_FIELD(3, 0, tpl);  // Task Priority Level
};

ARCH_X86_SYSREG(X86Cr8, "cr8");

// [Intel/vol3]: 2.6 Extended Control Registers
struct X86Xcr0 : SysRegBase<X86Xcr0> {
  // Bit 63 of XCR0 is reserved for future expansion and will not represent a
  // processor state component.
  DEF_RSVDZ_BIT(63);

  // The Intel manual lists these in ascending bit order instead of descending
  // bit order like most other control registers, so we follow suit.
  DEF_BIT(0, x87);
  DEF_BIT(1, sse);
  DEF_BIT(2, avx);
  DEF_BIT(3, bndreg);
  DEF_BIT(4, bndcsr);
  DEF_BIT(5, opmask);
  DEF_BIT(6, zmm_hi256);
  DEF_BIT(7, hi16_zmm);
  DEF_RSVDZ_BIT(8);
  DEF_BIT(9, pkru);

  DEF_RSVDZ_FIELD(62, 10);  // Reserved for future expansion.
};

// XCR0 is accessed differently than other system registers.  It could have its
// own IO provider like MSRs, but making it a special case of the system
// registers fits better especially since there is only actually one XCR.
// ARCH_X86_SYSREG() in <lib/arch/sysreg.h> does these definitions for others.
#if defined(__Fuchsia__) && (defined(__x86_64__) || defined(__i386__))
template <>
inline void SysReg::WriteRegister<X86Xcr0>(uint64_t value) {
  _xsetbv(0, value);
}

template <>
inline uint64_t SysReg::ReadRegister<X86Xcr0>() {
  return _xgetbv(0);
}
#endif

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SYSTEM_H_
