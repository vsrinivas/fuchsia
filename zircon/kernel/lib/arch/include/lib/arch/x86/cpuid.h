// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_

#include <string_view>
#include <type_traits>
#include <utility>

#include <hwreg/bitfields.h>

namespace arch {

// An hwreg-compatible interface for reading CPUID values, where the
// "addresses" correspond to the EAX, EBX, ECX, and EDX registers. The values
// are expected to be programmatically filled before use, e.g., using
// the compiler-supplied <cpuid.h> (not included here, since it is x86-only):
// ```
// cpuid_count(leaf, subleaf, values_[CpuidIo::kEax], values_[CpuidIo::kEbx],
//             values_[CpuidIo::kEcx], values_[CpuidIo::kEdx]);
// ```
struct CpuidIo {
  enum Register : uint32_t {
    kEax = 0,
    kEbx = 1,
    kEcx = 2,
    kEdx = 3,
  };

  // The API needs this to be a template even though only one type is valid.
  // In the general case, this is usually a template that admits multiple
  // possible integer types.  So calls to it from template-generic code use
  // `io.template Read<uint32_t>(offset)` and the like, which is invalid if
  // this is not a template function.
  template <typename T>
  T Read(uint32_t reg) const {
    static_assert(std::is_same_v<T, uint32_t>);
    ZX_ASSERT(reg < std::size(values_));
    return values_[reg];
  }

  uint32_t values_[4];
};

enum class Vendor {
  kUnknown,
  kIntel,
  kAmd,
};

// The list is not exhaustive and is in chronological order within groupings.
// Microarchictectures that use the same processor (and, say, differ only in
// performance or SoC composition) are regarded as equivalent.
enum class Microarchitecture {
  kUnknown,

  // Intel Core family (64-bit, display family 0x6).
  kIntelCore2,
  kIntelNehalem,
  kIntelWestmere,
  kIntelSandyBridge,
  kIntelIvyBridge,
  kIntelHaswell,
  kIntelBroadwell,
  // Includes Kaby/Coffee/Whiskey/Amber/Comet Lake.
  kIntelSkylake,
  // Includes Cascade/Cooper Lake.
  kIntelSkylakeServer,
  // A 10nm prototype only ever released on the Intel Core i3-8121U.
  kIntelCannonLake,

  // Intel Atom family.
  kIntelBonnell,
  kIntelSilvermont,
  kIntelAirmont,
  kIntelGoldmont,
  kIntelGoldmontPlus,
  kIntelTremont,

  // AMD families.
  kAmdFamily0x15,
  kAmdFamily0x16,
  kAmdFamily0x17,
  kAmdFamily0x19,
};

std::string_view ToString(Vendor vendor);
std::string_view ToString(Microarchitecture microarch);

// A convenient and self-documenting wrapper for defining CPUID value bitsets
// as hwreg register objects, along with their associated leaf and subleaf
// values (which are always expected to be defined as static constexpr
// members).
//
// We use Intel's terms of "leaf", "subleaf" over AMD's "function", "subfuntion"
// as the latter pair is more overloaded and ambiguous.
//
// Assembly macro generation requires the use of `hwreg::EnablePrinter`.
//
// TODO(joshuaseaton|mcgrathr): When global templated CpuidIo objects are
// defined, also define a templated getter that takes a CpuidValue type,
// consults kLeaf and kSubleaf, and returns the appropriate object (e.g.,
// `GetCpuidFor<ValueType>()`).
template <typename ValueType, uint32_t Leaf, uint32_t Subleaf, CpuidIo::Register OutputRegister>
struct CpuidValueBase : public hwreg::RegisterBase<ValueType, uint32_t, hwreg::EnablePrinter> {
  static constexpr uint32_t kLeaf = Leaf;
  static constexpr uint32_t kSubleaf = Subleaf;

  static auto Get() { return hwreg::RegisterAddr<ValueType>(OutputRegister); }
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x0.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.1  Function 0h—Maximum Standard Function Number and Vendor String.
//---------------------------------------------------------------------------//

// [amd/vol3]: E.3.1, CPUID Fn0000_0000_EAX Largest Standard Function Number.
struct CpuidMaximumLeaf : public CpuidValueBase<CpuidMaximumLeaf, 0x0, 0x0, CpuidIo::kEax> {};

// [amd/vol3]: E.3.1, CPUID Fn0000_0000_E[D,C,B]X Processor Vendor.
struct CpuidVendorB : public CpuidValueBase<CpuidVendorB, 0x0, 0x0, CpuidIo::kEbx> {};
struct CpuidVendorC : public CpuidValueBase<CpuidVendorC, 0x0, 0x0, CpuidIo::kEcx> {};
struct CpuidVendorD : public CpuidValueBase<CpuidVendorD, 0x0, 0x0, CpuidIo::kEdx> {};

template <typename CpuidIoProvider>
Vendor GetVendor(CpuidIoProvider&& io) {
  using namespace std::string_view_literals;

  const uint32_t ids[] = {
      io.template Read<CpuidVendorB>().reg_value(),
      io.template Read<CpuidVendorD>().reg_value(),
      io.template Read<CpuidVendorC>().reg_value(),
  };
  std::string_view name{reinterpret_cast<const char*>(ids), sizeof(ids)};
  if (name == "GenuineIntel"sv) {
    return Vendor::kIntel;
  } else if (name == "AuthenticAMD"sv) {
    return Vendor::kAmd;
  }
  return Vendor::kUnknown;
}

//---------------------------------------------------------------------------//
// Leaf/Function 0x1.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.2  Function 1h—Processor and Processor Feature Identifiers
//---------------------------------------------------------------------------//

// [intel/vol2]: Figure 3-6.  Version Information Returned by CPUID in EAX.
// [amd/vol3]: E.3.2, CPUID Fn0000_0001_EAX  Family, Model, Stepping Identifiers.
struct CpuidVersionInfo : public CpuidValueBase<CpuidVersionInfo, 0x1, 0x0, CpuidIo::kEax> {
  // [intel/vol2]: Table 3-9.  Processor Type Field.
  enum class IntelProcessorType : uint8_t {
    kOriginalOem = 0b00,
    kIntelOverdrive = 0b01,
    kDual = 0b10,
    kReserved = 0b11,
  };

  // Bits [31:28] are reserved.
  DEF_FIELD(27, 20, extended_family);
  DEF_FIELD(19, 16, extended_model);
  // Bits [15:14] are reserved.
  DEF_ENUM_FIELD(IntelProcessorType, 13, 12, intel_processor);  // Reserved on AMD.
  DEF_FIELD(11, 8, base_family);
  DEF_FIELD(7, 4, base_model);
  DEF_FIELD(3, 0, stepping);

  uint8_t family() const;
  uint8_t model() const;

  // Attempts to derives the microarchitecture with the assumption that the
  // system relates to a particular vendor.
  Microarchitecture microarchitecture(Vendor vendor) const;
};

template <typename CpuidIoProvider>
Microarchitecture GetMicroarchitecture(CpuidIoProvider&& io) {
  auto vendor = GetVendor(io);
  return std::forward<CpuidIoProvider>(io).template Read<CpuidVersionInfo>().microarchitecture(
      vendor);
}

// [intel/vol2]: Table 3-10.  Feature Information Returned in the ECX Register.
// [amd/vol3]: E.3.2, CPUID Fn0000_0001_ECX Feature Identifiers.
struct CpuidFeatureFlagsC : public CpuidValueBase<CpuidFeatureFlagsC, 0x1, 0x0, CpuidIo::kEcx> {
  DEF_RSVDZ_BIT(31);
  DEF_BIT(30, rdrand);
  DEF_BIT(29, f16c);
  DEF_BIT(28, avx);
  DEF_BIT(27, osxsave);
  DEF_BIT(26, xsave);
  DEF_BIT(25, aes);
  DEF_BIT(24, tsc_deadline);
  DEF_BIT(23, popcnt);
  DEF_BIT(22, movbe);
  DEF_BIT(21, x2apic);
  DEF_BIT(20, sse4_2);
  DEF_BIT(19, sse4_1);
  DEF_BIT(18, dca);
  DEF_BIT(17, pcid);
  // Bit 16 is reserved.
  DEF_BIT(15, pdcm);
  DEF_BIT(14, xtpr);
  DEF_BIT(13, cmpxchg16b);
  DEF_BIT(12, fma);
  DEF_BIT(11, sdbg);
  DEF_BIT(10, cnxt_id);
  DEF_BIT(9, ssse3);
  DEF_BIT(8, tm2);
  DEF_BIT(7, eist);
  DEF_BIT(6, smx);
  DEF_BIT(5, vmx);
  DEF_BIT(4, ds_cpl);
  DEF_BIT(3, monitor);
  DEF_BIT(2, dtes64);
  DEF_BIT(1, pclmulqdq);
  DEF_BIT(0, sse3);
};

// [intel/vol2]: Table 3-11.  More on Feature Information Returned in the EDX Register.
// [amd/vol3]: E.3.6  Function 7h—Structured Extended Feature Identifiers.
struct CpuidFeatureFlagsD : public CpuidValueBase<CpuidFeatureFlagsD, 0x1, 0x0, CpuidIo::kEdx> {
  DEF_BIT(31, pbe);
  // Bit 30 is reserved.
  DEF_BIT(29, tm);
  DEF_BIT(28, htt);
  DEF_BIT(27, ss);
  DEF_BIT(26, sse2);
  DEF_BIT(25, sse);
  DEF_BIT(24, fxsr);
  DEF_BIT(23, mmx);
  DEF_BIT(22, acpi);
  DEF_BIT(21, ds);
  // Bit 20 is reserved.
  DEF_BIT(19, clfsh);
  DEF_BIT(18, psn);
  DEF_BIT(17, pse36);
  DEF_BIT(16, pat);
  DEF_BIT(15, cmov);
  DEF_BIT(14, mca);
  DEF_BIT(13, pge);
  DEF_BIT(12, mtrr);
  DEF_BIT(11, sep);
  // Bit 10 is reserved.
  DEF_BIT(9, apic);
  DEF_BIT(8, cx8);
  DEF_BIT(7, mce);
  DEF_BIT(6, pae);
  DEF_BIT(5, msr);
  DEF_BIT(4, tsc);
  DEF_BIT(3, pse);
  DEF_BIT(2, de);
  DEF_BIT(1, vme);
  DEF_BIT(0, fpu);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x7.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.6  Function 7h—Structured Extended Feature Identifier
//---------------------------------------------------------------------------//

// [amd/vol3]: E.3.6, CPUID Fn0000_0007_EBX_x0 Structured Extended Feature Identifiers (ECX=0).
struct CpuidExtendedFeatureFlagsB
    : public CpuidValueBase<CpuidExtendedFeatureFlagsB, 0x7, 0x0, CpuidIo::kEbx> {
  DEF_BIT(31, avx512vl);
  DEF_BIT(30, avx512bw);
  DEF_BIT(29, sha);
  DEF_BIT(28, avx512cd);
  DEF_BIT(27, avx512er);
  DEF_BIT(26, avx512pf);
  DEF_BIT(25, intel_pt);
  DEF_BIT(24, clwb);
  DEF_BIT(23, clflushopt);
  // Bit 22 is reserved.
  DEF_BIT(21, avx512_ifma);
  DEF_BIT(20, smap);
  DEF_BIT(19, adx);
  DEF_BIT(18, rdseed);
  DEF_BIT(17, avx512dq);
  DEF_BIT(16, avx512f);
  DEF_BIT(15, rdt_a);
  DEF_BIT(14, mpx);
  DEF_BIT(13, fpu_cs_ds_deprecated);
  DEF_BIT(12, rdt_m);
  DEF_BIT(11, rtm);
  DEF_BIT(10, invpcid);
  DEF_BIT(9, enhanced_rep_movsb_stosb);
  DEF_BIT(8, bmi2);
  DEF_BIT(7, smep);
  DEF_BIT(6, fdp_excptn_only_x87);
  DEF_BIT(5, avx2);
  DEF_BIT(4, hle);
  DEF_BIT(3, bmi1);
  DEF_BIT(2, sgx);
  DEF_BIT(1, tsc_adjust);
  DEF_BIT(0, fsgsbase);
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
