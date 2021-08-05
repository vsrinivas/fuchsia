// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_

#include <lib/arch/arm64/feature.h>
#include <lib/arch/internal/bits.h>
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
struct ArmCurrentEl : public SysRegBase<ArmCurrentEl, uint64_t, hwreg::EnablePrinter> {
  DEF_FIELD(3, 2, el);
};

ARCH_ARM64_SYSREG(ArmCurrentEl, "CurrentEL");

// This type covers three register formats:
//  * [arm/sysreg]/sctlr_el1: System Control Register (EL1)
//  * [arm/sysreg]/sctlr_el2: System Control Register (EL2)
//  * [arm/sysreg]/sctlr_el3: System Control Register (EL3)
// Some fields (mostly things relating to EL0) are only used in EL1 and are
// reserved in the other registers.  Missing bits are reserved in all cases.
struct ArmSystemControlRegister
    : public SysRegDerivedBase<ArmSystemControlRegister, uint64_t, hwreg::EnablePrinter> {
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

// TCR_EL1 Cache Attributes
//
// Used in multiple bitfields for TCR_EL1 and TCR_EL2.
//
// [arm/v8]: D13.2.120 TCR_EL1, Translation Control Register (EL1)
// [arm/v8]: D13.2.121 TCR_EL2, Translation Control Register (EL2)
enum class ArmTcrCacheAttr {
  kNonCacheable = 0b00,
  kWriteBackWriteAllocate = 0b01,
  kWriteThrough = 0b10,
  kWriteBack = 0b11,
};

// Granule size values for the TCR_EL1 and TCR_EL2 fields.
//
// WARNING: The encodings for the TG0 field and TG1 field are different.
//
// [arm/v8]: D13.2.120 TCR_EL1, Translation Control Register (EL1)
// [arm/v8]: D13.2.121 TCR_EL2, Translation Control Register (EL2)
enum class ArmTcrTg0Value {
  k4KiB = 0b00,
  k16KiB = 0b10,
  k64KiB = 0b01,
};
enum class ArmTcrTg1Value {
  k4KiB = 0b10,
  k16KiB = 0b01,
  k64KiB = 0b11,
};

// Cache shareability attribute for the TCR_EL1 and TCR_EL2 fields.
//
// [arm/v8]: D13.2.120 TCR_EL1, Translation Control Register (EL1)
// [arm/v8]: D13.2.121 TCR_EL2, Translation Control Register (EL2)
enum class ArmTcrShareAttr {
  kNonShareable = 0b00,
  kOuterShareable = 0b10,
  kInnerShareable = 0b11,
};

// Translation Control Register (TCR) for EL1.
//
// The TCR controls the settings relating to the page table, including
// the layout (such as granule size setting and size of the address
// space).
//
// [arm/v8]: D13.2.120 TCR_EL1, Translation Control Register (EL1)
class ArmTcrEl1 : public SysRegBase<ArmTcrEl1> {
 public:
  // Bits [63:60] reserved.
  DEF_BIT(59, ds);
  DEF_BIT(58, tcma1);
  DEF_BIT(57, tcma0);
  DEF_BIT(56, e0pd1);
  DEF_BIT(55, e0pd0);
  DEF_BIT(54, nfd1);
  DEF_BIT(53, nfd0);
  DEF_BIT(52, tbid1);  // TTBR1 Top Byte Ignored for Data only
  DEF_BIT(51, tbid0);  // TTBR0 Top Byte Ignored for Data only
  DEF_BIT(50, hwu162);
  DEF_BIT(49, hwu161);
  DEF_BIT(48, hwu160);
  DEF_BIT(47, hwu159);
  DEF_BIT(46, hwu062);
  DEF_BIT(45, hwu061);
  DEF_BIT(44, hwu060);
  DEF_BIT(43, hwu059);
  DEF_BIT(42, hpd1);  // TTBR0 Hierarchical Permission Disable
  DEF_BIT(41, hpd0);  // TTBR0 Hierarchical Permission Disable
  DEF_BIT(40, hd);    // Hardware Dirty state management
  DEF_BIT(39, ha);    // Hardware Access flag updated
  DEF_BIT(38, tbi1);  // TTBR1 Top Byte Ignored
  DEF_BIT(37, tbi0);  // TTBR0 Top Byte Ignored
  DEF_BIT(36, as);    // ASID size: 0 = 8-bit, 1 = 16-bit
  // Bit 35 reserved.
  DEF_ENUM_FIELD(ArmPhysicalAddressSize, 34, 32, ips);  // Intermediate physical address size.
  DEF_ENUM_FIELD(ArmTcrTg1Value, 31, 30, tg1);          // TTBR1 granule size
  DEF_ENUM_FIELD(ArmTcrShareAttr, 29, 28, sh1);         // TTBR1 cache sharability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 27, 26, orgn1);       // TTBR1 outer cacheability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 25, 24, irgn1);       // TTBR1 inner cacheability
  DEF_BIT(23, epd1);                                    // TTBR1 table walks disabled
  DEF_BIT(22, a1);                                      // ASID select: 0 = TTBR0, 1 = TTBR1
  DEF_FIELD(21, 16, t1sz);                              // TTBR0 size offset
  DEF_ENUM_FIELD(ArmTcrTg0Value, 15, 14, tg0);          // TTBR0 granule size
  DEF_ENUM_FIELD(ArmTcrShareAttr, 13, 12, sh0);         // TTBR0 cache sharability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 11, 10, orgn0);       // TTBR0 outer cacheability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 9, 8, irgn0);         // TTBR0 inner cacheability
  DEF_BIT(7, epd0);                                     // TTBR0 table walks disabled
  // Bit 6 reserved.
  DEF_FIELD(5, 0, t0sz);  // TTBR0 size offset
};

ARCH_ARM64_SYSREG(ArmTcrEl1, "tcr_el1");

// Translation Control Register (TCR) for EL2.
//
// This register layout is only valid when HCR_EL2.E2H == 0 (that is,
// Virtualization Host Extensions are disabled).
//
// [arm/v8]: D13.2.121 TCR_EL2, Translation Control Register (EL2)
struct ArmTcrEl2 : public SysRegBase<ArmTcrEl2> {
  // Bits [63:33] reserved.
  DEF_BIT(32, ds);
  DEF_BIT(31, res1_bit32);  // RES1: should be preserved or written as 1.
  DEF_BIT(30, tcma);
  DEF_BIT(29, tbid);
  DEF_BIT(28, hwu62);
  DEF_BIT(27, hwu61);
  DEF_BIT(26, hwu60);
  DEF_BIT(25, hwu59);
  DEF_BIT(24, hpd);                                    // Hierarchical Permission Disable
  DEF_BIT(23, res1_bit23);                             // RES1: should be preserved or written as 1.
  DEF_BIT(22, hd);                                     // Hardware Dirty state management
  DEF_BIT(21, ha);                                     // Hardware Access flag updated
  DEF_BIT(20, tbi);                                    // Top byte ignored
  DEF_ENUM_FIELD(ArmPhysicalAddressSize, 18, 16, ps);  // Physical address size
  DEF_ENUM_FIELD(ArmTcrTg0Value, 15, 14, tg0);         // TTBR0 Granule size
  DEF_ENUM_FIELD(ArmTcrShareAttr, 13, 12, sh0);        // TTBR0 Cache sharability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 11, 10, orgn0);      // TTBR0 Outer cacheability
  DEF_ENUM_FIELD(ArmTcrCacheAttr, 9, 8, irgn0);        // TTBR0 Inner cacheability
  // Bits [7:6] reserved.
  DEF_FIELD(5, 0, t0sz);  // TTBR0 size offset

  ArmTcrEl2() {
    // Bits marked RES1 need to be either preserved or set to 1. If constructing
    // the register from scratch, set them to 1.
    //
    // TODO(fxbug.dev/75300): Consider adding RES1 support to hwreg library.
    set_res1_bit32(1);
    set_res1_bit23(1);
  }
};

ARCH_ARM64_SYSREG(ArmTcrEl2, "tcr_el2");

// Page table root pointer.
//
// This common format is used for several registers which contain
// the root of the page table.
//
// [arm/v8]: D13.2.132 TTBR0_EL1, Translation Table Base Register 0 (EL1)
// [arm/v8]: D13.2.133 TTBR0_EL2, Translation Table Base Register 0 (EL2)
// [arm/v8]: D13.2.134 TTBR0_EL3, Translation Table Base Register 0 (EL3)
// [arm/v8]: D13.2.135 TTBR1_EL1, Translation Table Base Register 1 (EL1)
// [arm/v8]: D13.2.136 TTBR1_EL2, Translation Table Base Register 1 (EL2)
struct ArmTranslationTableBaseRegister
    : public SysRegDerivedBase<ArmTranslationTableBaseRegister, uint64_t> {
  DEF_FIELD(63, 48, asid);
  DEF_UNSHIFTED_FIELD(47, 1, addr);  // Bits [47:1] of the root table physical address.
  DEF_BIT(0, cnp);                   // Common not private.
};

struct ArmTtbr0El1 : public arch::SysRegDerived<ArmTtbr0El1, ArmTranslationTableBaseRegister> {};
ARCH_ARM64_SYSREG(ArmTtbr0El1, "ttbr0_el1");

struct ArmTtbr0El2 : public arch::SysRegDerived<ArmTtbr0El2, ArmTranslationTableBaseRegister> {};
ARCH_ARM64_SYSREG(ArmTtbr0El2, "ttbr0_el2");

struct ArmTtbr0El3 : public arch::SysRegDerived<ArmTtbr0El3, ArmTranslationTableBaseRegister> {};
ARCH_ARM64_SYSREG(ArmTtbr0El3, "ttbr0_el3");

struct ArmTtbr1El1 : public arch::SysRegDerived<ArmTtbr1El1, ArmTranslationTableBaseRegister> {};
ARCH_ARM64_SYSREG(ArmTtbr1El1, "ttbr1_el1");

struct ArmTtbr1El2 : public arch::SysRegDerived<ArmTtbr1El2, ArmTranslationTableBaseRegister> {};
ARCH_ARM64_SYSREG(ArmTtbr1El2, "ttbr1_el2");

// Memory attributes.
//
// This is a list of used memory attributes, and not comprehensive.
enum class ArmMemoryAttribute : uint8_t {
  // Device memory: non write combining, no reorder, no early ack.
  kDevice_nGnRnE = 0b0000'0000,

  // Device memory: non write combining, no reorder, early ack.
  kDevice_nGnRE = 0b0000'0100,

  // Normal Memory, Outer Write-back non-transient Read/Write allocate, Inner
  // Write-back non-transient Read/Write allocate
  kNormalCached = 0b1111'1111,

  // Normal memory, Inner/Outer uncached, Write Combined
  kNormalUncached = 0b0100'0100,
};

// Memory Attribute Indirection Register
//
// [arm/v8]: D13.2.95  MAIR_EL1, Memory Attribute Indirection Register, EL1
// [arm/v8]: D13.2.96  MAIR_EL2, Memory Attribute Indirection Register, EL2
struct ArmMemoryAttrIndirectionRegister
    : public SysRegDerivedBase<ArmMemoryAttrIndirectionRegister, uint64_t> {
  DEF_ENUM_FIELD(ArmMemoryAttribute, 63, 56, attr7);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 55, 48, attr6);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 47, 40, attr5);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 39, 32, attr4);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 31, 24, attr3);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 23, 16, attr2);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 15, 8, attr1);
  DEF_ENUM_FIELD(ArmMemoryAttribute, 7, 0, attr0);

  static constexpr size_t kNumAttributes = 8;

  // Get the ArmMemoryAttribute at the given index.
  //
  // TODO(fxbug.dev/78027): Ideally hwreg would support this natively.
  ArmMemoryAttribute GetAttribute(size_t index) const {
    ZX_DEBUG_ASSERT(index < kNumAttributes);
    size_t low_bit = index * kAttributeBits;
    size_t high_bit = low_bit + kAttributeBits - 1;
    return static_cast<ArmMemoryAttribute>(internal::ExtractBits(high_bit, low_bit, reg_value()));
  }

  // Set the ArmMemoryAttribute at the given index.
  //
  // TODO(fxbug.dev/78027): Ideally hwreg would support this natively.
  ArmMemoryAttrIndirectionRegister& SetAttribute(size_t index, ArmMemoryAttribute value) {
    ZX_DEBUG_ASSERT(index < kNumAttributes);

    size_t low_bit = index * kAttributeBits;
    size_t high_bit = low_bit + kAttributeBits - 1;
    set_reg_value(
        internal::UpdateBits(high_bit, low_bit, reg_value(), static_cast<uint64_t>(value)));
    return *this;
  }

 private:
  static constexpr size_t kAttributeBits = 8;  // Width of each attribute (in bits).
};

struct ArmMairEl1 : public arch::SysRegDerived<ArmMairEl1, ArmMemoryAttrIndirectionRegister> {};
ARCH_ARM64_SYSREG(ArmMairEl1, "mair_el1");

struct ArmMairEl2 : public arch::SysRegDerived<ArmMairEl2, ArmMemoryAttrIndirectionRegister> {};
ARCH_ARM64_SYSREG(ArmMairEl2, "mair_el2");

// This state is accessed via multiple registers with different bit placements.
// The three registers DAIF, DAIFSet, and DAIFClr are specified in:
// [arm/sysreg]/currentel: DAIF, Interrupt Mask Bits
struct ArmDaif : public SysRegBase<ArmDaif, uint64_t, hwreg::EnablePrinter> {
  DEF_BIT(9, d);
  DEF_BIT(8, a);
  DEF_BIT(7, i);
  DEF_BIT(6, f);
};
ARCH_ARM64_SYSREG(ArmDaif, "daif");

// This is the bit layout used in DAIFSet and DAIFClr for the same bits that
// can be read or modified with different placements via DAIF.  These two
// pseudo-registers are accessed via a special MSR instruction form that takes
// only a four-bit immediate value.  These registers can't really be used from
// C++ through the normal mechanism, because their intrinsics only accept a
// constant argument and any layers of inline function around the intrinsics
// prevent the compiler from allowing a value to be passed down even if it's
// all done as constexpr.
struct ArmDaifSetClr : public SysRegBase<ArmDaifSetClr, uint64_t, hwreg::EnablePrinter> {
  DEF_BIT(3, d);
  DEF_BIT(2, a);
  DEF_BIT(1, i);
  DEF_BIT(0, f);
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_SYSTEM_H_
