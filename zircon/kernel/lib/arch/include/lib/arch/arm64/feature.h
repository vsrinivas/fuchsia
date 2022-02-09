// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_FEATURE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_FEATURE_H_

#include <lib/arch/hwreg.h>
#include <lib/arch/sysreg.h>

namespace arch {

// [arm/sysreg]/ID_AA64ISAR0_EL1: AArch64 Instruction Set Attribute Register 0
struct ArmIdAa64IsaR0El1 : public SysRegBase<ArmIdAa64IsaR0El1, uint64_t> {
  enum class Rndr : uint8_t {
    kNone = 0b0000,
    kRng = 0b0001,
  };

  enum class Tlb : uint8_t {
    kNone = 0b0000,
    kTlbios = 0b0001,
    kkTlbirange = 0b0010,
  };

  enum class Ts : uint8_t {
    kNone = 0b0000,
    kFlagM = 0b0001,
    kFlagM2 = 0b0010,
  };

  enum class Fhm : uint8_t {
    kNone = 0b0000,
    kFhm = 0b0001,
  };

  enum class DotProd : uint8_t {
    kNone = 0b0000,
    kDotProd = 0b0001,
  };

  enum class Sm4 : uint8_t {
    kNone = 0b0000,
    kSm4 = 0b0001,
  };

  enum class Sm3 : uint8_t {
    kNone = 0b0000,
    kSm3 = 0b0001,
  };

  enum class Sha3 : uint8_t {
    kNone = 0b0000,
    kSha3 = 0b0001,
  };

  enum class Rdm : uint8_t {
    kNone = 0b0000,
    kRdm = 0b0001,
  };

  enum class Atomic : uint8_t {
    kNone = 0b0000,
    kLse = 0b0010,
  };

  enum class Crc32 : uint8_t {
    kNone = 0b0000,
    kCrc32 = 0b0001,
  };

  enum class Sha2 : uint8_t {
    kNone = 0b0000,
    kSha256 = 0b0001,
    kSha512 = 0b0010,
  };

  enum class Sha1 : uint8_t {
    kNone = 0b0000,
    kSha1 = 0b0001,
  };

  enum class Aes : uint8_t {
    kNone = 0b0000,
    kAes = 0b0001,
    kPmull = 0b0010,
  };

  DEF_ENUM_FIELD(Rndr, 63, 60, rndr);
  DEF_ENUM_FIELD(Tlb, 59, 56, tlb);
  DEF_ENUM_FIELD(Ts, 55, 52, ts);
  DEF_ENUM_FIELD(Fhm, 51, 48, fhm);
  DEF_ENUM_FIELD(DotProd, 47, 44, dp);
  DEF_ENUM_FIELD(Sm4, 43, 40, sm4);
  DEF_ENUM_FIELD(Sm3, 39, 36, sm3);
  DEF_ENUM_FIELD(Sha3, 35, 32, sha3);
  DEF_ENUM_FIELD(Rdm, 31, 28, rdm);
  DEF_RSVDZ_FIELD(27, 24);
  DEF_ENUM_FIELD(Atomic, 23, 20, atomic);
  DEF_ENUM_FIELD(Crc32, 19, 16, crc32);
  DEF_ENUM_FIELD(Sha2, 15, 12, sha2);
  DEF_ENUM_FIELD(Sha1, 11, 8, sha1);
  DEF_ENUM_FIELD(Aes, 7, 4, aes);
  DEF_RSVDZ_FIELD(3, 0);
};
ARCH_ARM64_SYSREG(ArmIdAa64IsaR0El1, "ID_AA64ISAR0_EL1");

// [arm/sysreg]/ID_AA64ISAR1_EL1: AArch64 Instruction Set Attribute Register 1
struct ArmIdAa64IsaR1El1 : public SysRegBase<ArmIdAa64IsaR1El1> {
  enum class Ls64 : uint8_t {
    kNone = 0b0000,
    kLs64 = 0b0001,
    kLs64V = 0b0010,
    kLs64Accdata = 0b0011,
  };

  enum class Xs : uint8_t {
    kNone = 0b0000,
    kXs = 0b0001,
  };

  enum class I8mm : uint8_t {
    kNone = 0b0000,
    kI8mm = 0b0001,
  };

  enum class Dgh : uint8_t {
    kNone = 0b0000,
    kDgh = 0b0001,
  };

  enum class Bf16 : uint8_t {
    kNone = 0b0000,
    kBf16 = 0b0001,
  };

  enum class Specres : uint8_t {
    kNone = 0b0000,
    kSpecres = 0b0001,
  };

  enum class Sb : uint8_t {
    kNone = 0b0000,
    kSb = 0b0001,
  };

  enum class Frintts : uint8_t {
    kNone = 0b0000,
    kFrintts = 0b0001,
  };

  enum class Gpi : uint8_t {
    kNone = 0b0000,
    kGpi = 0b0001,
  };

  enum class Gpa : uint8_t {
    kNone = 0b0000,
    kGpa = 0b0001,
  };

  enum class Lrcpc : uint8_t {
    kNone = 0b0000,
    kLrcpc = 0b0001,
    kLrcpc2 = 0b0010,
  };

  enum class Fcma : uint8_t {
    kNone = 0b0000,
    kFcma = 0b0001,
  };

  enum class Jscvt : uint8_t {
    kNone = 0b0000,
    kJscvt = 0b0001,
  };

  enum class Pauth : uint8_t {
    kNone = 0b0000,
    kPauth = 0b0001,
    kPauthEnhanced = 0b0010,
    kPauth2 = 0b0011,
    kFpac = 0b0100,
    kFpacCombined = 0b0101,
  };

  enum class Dpb : uint8_t {
    kNone = 0b0000,
    kDpb = 0b0001,
    kDpb2 = 0b0010,
  };

  DEF_ENUM_FIELD(Ls64, 63, 60, ls64);
  DEF_ENUM_FIELD(Xs, 59, 56, xs);
  DEF_ENUM_FIELD(I8mm, 55, 52, i8mm);
  DEF_ENUM_FIELD(Dgh, 51, 48, dgh);
  DEF_ENUM_FIELD(Bf16, 47, 44, bf16);
  DEF_ENUM_FIELD(Specres, 43, 40, specres);
  DEF_ENUM_FIELD(Sb, 39, 36, sb);
  DEF_ENUM_FIELD(Frintts, 35, 32, frintts);
  DEF_ENUM_FIELD(Gpi, 31, 28, gpi);
  DEF_ENUM_FIELD(Gpa, 27, 24, gpa);
  DEF_ENUM_FIELD(Lrcpc, 23, 20, lrcpc);
  DEF_ENUM_FIELD(Fcma, 19, 16, fcma);
  DEF_ENUM_FIELD(Jscvt, 15, 12, jscvt);
  DEF_ENUM_FIELD(Pauth, 11, 8, api);
  DEF_ENUM_FIELD(Pauth, 7, 4, apa);
  DEF_ENUM_FIELD(Dpb, 3, 0, dpb);
};
ARCH_ARM64_SYSREG(ArmIdAa64IsaR1El1, "ID_AA64ISAR1_EL1");

// [arm/sysreg]/ID_AA64PFR0_EL1: AArch64 Processor Feature Register 0
struct ArmIdAa64Pfr0El1 : public SysRegBase<ArmIdAa64Pfr0El1> {
  enum class Csv3 : uint8_t {
    kNone = 0b0000,
    kCsv3 = 0b0001,
  };

  enum class Csv2 : uint8_t {
    kNone = 0b0000,
    kCsv2 = 0b0001,
    kCsv2_2 = 0b0010,
  };

  enum class Dit : uint8_t {
    kNone = 0b0000,
    kDit = 0b0001,
  };

  enum class Amu : uint8_t {
    kNone = 0b0000,
    kAmuv1 = 0b0001,
    kAmuv1p1 = 0b0010,
  };

  enum class Mpam : uint8_t {
    kNone = 0b0000,
    kMpam = 0b0001,
  };

  enum class Sel2 : uint8_t {
    kNone = 0b0000,
    kSel2 = 0b0001,
  };

  enum class Sve : uint8_t {
    kNone = 0b0000,
    kSve = 0b0001,
  };

  enum class Ras : uint8_t {
    kNone = 0b0000,
    kRas = 0b0001,
    kRasv1p1 = 0b0010,
  };

  enum class Gic : uint8_t {
    kNone = 0b0000,
    kGic4 = 0b0001,
    kGic4_1 = 0b0010,
  };

  enum class Fp : uint8_t {
    kFp = 0b0000,
    kFp16 = 0b0001,
    kNone = 0b1111,
  };

  enum class El : uint8_t {
    kNone = 0b0000,  // EL[23] not implemented.
    k64 = 0b0001,    // ELn supported in AAarch64 state
    k32 = 0b0010,    // ELn supported in AAarch64 or AAarch32 state
  };

  DEF_ENUM_FIELD(Csv3, 63, 60, csv3);
  DEF_ENUM_FIELD(Csv2, 59, 56, csv2);
  DEF_RSVDZ_FIELD(55, 52);
  DEF_ENUM_FIELD(Dit, 51, 48, dit);
  DEF_ENUM_FIELD(Amu, 47, 44, amu);
  DEF_ENUM_FIELD(Mpam, 43, 40, mpam);
  DEF_ENUM_FIELD(Sel2, 39, 36, sel2);
  DEF_ENUM_FIELD(Sve, 35, 32, sve);
  DEF_ENUM_FIELD(Ras, 31, 28, ras);
  DEF_ENUM_FIELD(Gic, 27, 24, gic);
  DEF_ENUM_FIELD(Fp, 23, 20, advsimd);
  DEF_ENUM_FIELD(Fp, 19, 16, fp);
  DEF_ENUM_FIELD(El, 15, 12, el3);
  DEF_ENUM_FIELD(El, 11, 8, el2);
  DEF_ENUM_FIELD(El, 7, 4, el1);
  DEF_ENUM_FIELD(El, 3, 0, el0);
};
ARCH_ARM64_SYSREG(ArmIdAa64Pfr0El1, "ID_AA64PFR0_EL1");

// Physical address size.
//
// The same encoding is used by several registers, including system registers TCR_EL1.IPS,
// TCR_EL2.PS, and the feature register ID_AA64MMFR0_EL1.PARange.
//
// [arm/v8]: D13.2.64 ID_AA64MMFR0_EL1, AArch64 Memory Model Feature Register 0
// [arm/v8]: D13.2.120 Translation Control Register (EL1)
// [arm/v8]: D13.2.121 Translation Control Register (EL2)
enum class ArmPhysicalAddressSize {
  k32Bits = 0b000,
  k36Bits = 0b001,
  k40Bits = 0b010,
  k42Bits = 0b011,
  k44Bits = 0b100,
  k48Bits = 0b101,
  k52Bits = 0b110,
};

// ID_AA64MMFR0_EL1, AArch64 Memory Model Feature Register 0
//
// [arm/v8]: D13.2.64 ID_AA64MMFR0_EL1, AArch64 Memory Model Feature Register 0
struct ArmIdAa64Mmfr0El1 : public SysRegBase<ArmIdAa64Mmfr0El1> {
  DEF_FIELD(63, 60, ecv);  // Enhanced Counter Virtualization (FEAT_ECV)
  DEF_FIELD(59, 56, fgt);  // Fine-Grained Trap controls (FEAT_FGT)
  // Bits [55:48] reserved.
  DEF_FIELD(47, 44, exs);          // Disable context synchronizing exceptions (FEAT_ExS)
  DEF_FIELD(43, 40, tgran4_2);     // 4 kiB granules at stage 2
  DEF_FIELD(39, 36, tgran64_2);    // 64 kiB granules at stage 2
  DEF_FIELD(35, 32, tgran16_2);    // 16 kiB granules at stage 2
  DEF_FIELD(31, 28, tgran4);       // 4 kiB granules at stage 1
  DEF_FIELD(27, 24, tgran64);      // 64 kiB granules at stage 1
  DEF_FIELD(23, 20, tgran16);      // 16 kiB granules at stage 1
  DEF_FIELD(19, 16, big_end_el0);  // Mixed-endian support for EL0 only
  DEF_FIELD(15, 12, sns_mem);      // Secure and Non-secure Memory distinguished
  DEF_FIELD(11, 8, big_end);       // Mixed-endian support
  DEF_FIELD(7, 4, asid_bits);      // Number of ASID bits
  DEF_ENUM_FIELD(ArmPhysicalAddressSize, 3, 0, pa_range);  // Supported Physical Address range
};

ARCH_ARM64_SYSREG(ArmIdAa64Mmfr0El1, "id_aa64mmfr0_el1");

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_ARM64_FEATURE_H_
