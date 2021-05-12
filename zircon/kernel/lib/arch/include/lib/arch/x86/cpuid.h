// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_

#include <array>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include <hwreg/asm.h>
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

// Define a "CPUID value type" as any type with the following:
// * static constexpr uint32_t members, `kLeaf` and `kSubleaf` giving the
//   associated leaf and subleaf;
// * a static `Get()` method returning a `hwreg::RegisterAddr` object holding
//   an "address" of one of the `CpuidIo::Register` values.
//
// CPUID "I/O" providers will deal in such types. See arch::BootCpuidIo and
// arch::testing::FakeCpuidIo for instances of such contracts.
//
// Note: there is no inherent relationship between the CPUID value type and the
// return type of `Get()`. In practice, a CPUID value type might be precisely
// the hwreg register type expressing the bit layout or a sort of getter for
// such a type (the utility of which lies in the fact that hwreg register
// types cannot be templated, for various reasons).
//
// We use Intel's terms of "leaf" and "subleaf" over AMD's "function" and
// "subfunction" as the latter pair is more overloaded and ambiguous.

// CpuidIoValue is a convenience type for defining a CPUID value type.
template <typename ValueType, uint32_t Leaf, uint32_t Subleaf, CpuidIo::Register OutputRegister>
struct CpuidIoValue {
  static constexpr uint32_t kLeaf = Leaf;
  static constexpr uint32_t kSubleaf = Subleaf;

  static auto Get() { return hwreg::RegisterAddr<ValueType>(OutputRegister); }
};

// CpuidIoValueBase is a convenience type for defining both a CPUID value type
// as well as the associated register type. hwreg::EnableAsmGeneration may be
// provided for generation of assembly constants representing the fields; see
// <hwreg/asm.h> for more details and heed the provisos there.
template <typename ValueType, uint32_t Leaf, uint32_t Subleaf, CpuidIo::Register OutputRegister,
          typename AsmGeneration = void>
struct CpuidIoValueBase : public hwreg::RegisterBase<ValueType, uint32_t, AsmGeneration>,
                          public CpuidIoValue<ValueType, Leaf, Subleaf, OutputRegister> {};

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

  // Bulldozer/Piledriver/Steamroller/Excavator.
  kAmdFamilyBulldozer,
  // Jaguar.
  kAmdFamilyJaguar,
  // Zen 1, 1+, 2.
  kAmdFamilyZen,
  // Zen 3.
  kAmdFamilyZen3,
};

std::string_view ToString(Vendor vendor);
std::string_view ToString(Microarchitecture microarch);

//---------------------------------------------------------------------------//
// Leaf/Function 0x0.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.1  Function 0h—Maximum Standard Function Number and Vendor String.
//---------------------------------------------------------------------------//

// [amd/vol3]: E.3.1, CPUID Fn0000_0000_EAX Largest Standard Function Number.
struct CpuidMaximumLeaf : public CpuidIoValueBase<CpuidMaximumLeaf, 0x0, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, leaf);
};

// [amd/vol3]: E.3.1, CPUID Fn0000_0000_E[D,C,B]X Processor Vendor.
struct CpuidVendorB : public CpuidIoValueBase<CpuidVendorB, 0x0, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidVendorC : public CpuidIoValueBase<CpuidVendorC, 0x0, 0x0, CpuidIo::kEcx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidVendorD : public CpuidIoValueBase<CpuidVendorD, 0x0, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 0, value);
};

template <typename CpuidIoProvider>
Vendor GetVendor(CpuidIoProvider&& io) {
  using namespace std::string_view_literals;

  const uint32_t ids[] = {
      io.template Read<CpuidVendorB>().value(),
      io.template Read<CpuidVendorD>().value(),
      io.template Read<CpuidVendorC>().value(),
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
struct CpuidVersionInfo : public CpuidIoValueBase<CpuidVersionInfo, 0x1, 0x0, CpuidIo::kEax> {
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

struct CpuidProcessorInfo : public CpuidIoValueBase<CpuidProcessorInfo, 0x1, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 24, initial_apic_id);
  DEF_FIELD(23, 16, max_logical_processors);
  DEF_FIELD(15, 8, clflush_size);
  DEF_FIELD(7, 0, brand_index);

  size_t cache_line_size_bytes() const { return static_cast<size_t>(clflush_size()) * 8; }
};

// [intel/vol2]: Table 3-10.  Feature Information Returned in the ECX Register.
// [amd/vol3]: E.3.2, CPUID Fn0000_0001_ECX Feature Identifiers.
struct CpuidFeatureFlagsC : public CpuidIoValueBase<CpuidFeatureFlagsC, 0x1, 0x0, CpuidIo::kEcx,
                                                    hwreg::EnableAsmGeneration> {
  // AMD documented "RAZ. Reserved for use by hypervisor to indicate guest
  // status."; Intel documents "Not Used. Always returns 0.".
  DEF_BIT(31, hypervisor);
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
struct CpuidFeatureFlagsD : public CpuidIoValueBase<CpuidFeatureFlagsD, 0x1, 0x0, CpuidIo::kEdx,
                                                    hwreg::EnableAsmGeneration> {
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
// Leaf/Function 0x4.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.3  Functions 2h–4h—Reserved.
//---------------------------------------------------------------------------//

enum class X86CacheType : uint8_t {
  kNull = 0,
  kData = 1,
  kInstruction = 2,
  kUnified = 3,
};

std::string_view ToString(X86CacheType type);

struct CpuidCacheTopologyA : public hwreg::RegisterBase<CpuidCacheTopologyA, uint32_t> {
  DEF_FIELD(31, 26, max_cores);  // Reserved on AMD.
  DEF_FIELD(25, 14, max_sharing_logical_processors);
  // Bits [13:10] are reserved.
  DEF_BIT(9, fully_associative);
  DEF_BIT(8, self_initializing);
  DEF_FIELD(7, 5, cache_level);
  DEF_ENUM_FIELD(X86CacheType, 4, 0, cache_type);
};

struct CpuidCacheTopologyB : public hwreg::RegisterBase<CpuidCacheTopologyB, uint32_t> {
  DEF_FIELD(31, 22, ways);
  DEF_FIELD(21, 12, physical_line_partitions);
  DEF_FIELD(11, 0, system_coherency_line_size);
};

struct CpuidCacheTopologyC : public hwreg::RegisterBase<CpuidCacheTopologyC, uint32_t> {
  DEF_FIELD(31, 0, sets);
};

struct CpuidCacheTopologyD : public hwreg::RegisterBase<CpuidCacheTopologyD, uint32_t> {
  // Bits [31:3] are reserved.
  DEF_BIT(2, complex_cache_indexing);
  DEF_BIT(1, inclusive);
  DEF_BIT(0, wbinvd);
};

template <uint32_t Subleaf>
using CpuidIntelCacheTopologyA = CpuidIoValue<CpuidCacheTopologyA, 0x4, Subleaf, CpuidIo::kEax>;

template <uint32_t Subleaf>
using CpuidIntelCacheTopologyB = CpuidIoValue<CpuidCacheTopologyB, 0x4, Subleaf, CpuidIo::kEbx>;

template <uint32_t Subleaf>
using CpuidIntelCacheTopologyC = CpuidIoValue<CpuidCacheTopologyC, 0x4, Subleaf, CpuidIo::kEcx>;

template <uint32_t Subleaf>
using CpuidIntelCacheTopologyD = CpuidIoValue<CpuidCacheTopologyD, 0x4, Subleaf, CpuidIo::kEdx>;

//---------------------------------------------------------------------------//
// Leaf/Function 0x5.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.4  Function 5h—Monitor and MWait Features.
//---------------------------------------------------------------------------//

struct CpuidMonitorMwaitA : public CpuidIoValueBase<CpuidMonitorMwaitA, 0x5, 0x0, CpuidIo::kEax> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_FIELD(15, 0, smallest_monitor_line_size);
};

struct CpuidMonitorMwaitB : public CpuidIoValueBase<CpuidMonitorMwaitB, 0x5, 0x0, CpuidIo::kEbx> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_FIELD(15, 0, largest_monitor_line_size);
};

struct CpuidMonitorMwaitC : public CpuidIoValueBase<CpuidMonitorMwaitC, 0x5, 0x0, CpuidIo::kEcx> {
  // Bits [31: 2] are reserved.
  DEF_BIT(1, ibe);
  DEF_BIT(0, emx);
};

struct CpuidMonitorMwaitD : public CpuidIoValueBase<CpuidMonitorMwaitD, 0x5, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 28, c7_sub_c_states);
  DEF_FIELD(27, 24, c6_sub_c_states);
  DEF_FIELD(23, 20, c5_sub_c_states);
  DEF_FIELD(19, 16, c4_sub_c_states);
  DEF_FIELD(15, 12, c3_sub_c_states);
  DEF_FIELD(11, 8, c2_sub_c_states);
  DEF_FIELD(7, 4, c1_sub_c_states);
  DEF_FIELD(3, 0, c0_sub_c_states);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x6.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.5  Function 6h—Power Management Related Features.
//---------------------------------------------------------------------------//

struct CpuidThermalAndPowerFeatureFlagsA
    : public CpuidIoValueBase<CpuidThermalAndPowerFeatureFlagsA, 0x6, 0x0, CpuidIo::kEax> {
  // Bits [31:21] are reserved.
  DEF_BIT(20, ignoring_idle_logical_processor_hwp);
  DEF_BIT(19, hw_feedback);
  DEF_BIT(18, fast_access_mode);
  DEF_BIT(17, flexible_hwp);
  DEF_BIT(16, hwp_peci_override);
  DEF_BIT(15, hwp_capabilities);
  DEF_BIT(14, turbo_max);
  DEF_BIT(13, hdc);
  // Bit 12 is reserved.
  DEF_BIT(11, hwp_package_level_request);
  DEF_BIT(10, hwp_epp);
  DEF_BIT(9, hwp_activity_window);
  DEF_BIT(8, hwp_notification);
  DEF_BIT(7, hwp);
  DEF_BIT(6, ptm);
  DEF_BIT(5, ecmd);
  DEF_BIT(4, pln);
  // Bit 3 is reserved.
  DEF_BIT(2, arat);
  DEF_BIT(1, turbo);
  DEF_BIT(0, digital_temperature_sensor);
};

struct CpuidThermalAndPowerFeatureFlagsC
    : public CpuidIoValueBase<CpuidThermalAndPowerFeatureFlagsC, 0x6, 0x0, CpuidIo::kEcx> {
  DEF_RSVDZ_FIELD(31, 4);
  DEF_BIT(3, performance_energy_bias_preference);
  DEF_RSVDZ_FIELD(2, 1);
  DEF_BIT(0, hardware_coordination_feedback_capabality);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x7.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.6  Function 7h—Structured Extended Feature Identifier
//---------------------------------------------------------------------------//

// [amd/vol3]: E.3.6, CPUID Fn0000_0007_EBX_x0 Structured Extended Feature Identifiers (ECX=0).
struct CpuidExtendedFeatureFlagsB
    : public CpuidIoValueBase<CpuidExtendedFeatureFlagsB, 0x7, 0x0, CpuidIo::kEbx,
                              hwreg::EnableAsmGeneration> {
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
  DEF_BIT(9, erms);
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

struct CpuidExtendedFeatureFlagsD
    : public CpuidIoValueBase<CpuidExtendedFeatureFlagsD, 0x7, 0x0, CpuidIo::kEdx> {
  DEF_BIT(31, ssbd);
  DEF_BIT(30, ia32_core_capabilities);
  DEF_BIT(29, ia32_arch_capabilities);
  DEF_BIT(28, l1d_flush);
  DEF_BIT(27, stibp);
  DEF_BIT(26, ibrs_ibpb);
  // Bits [25:21] are reserved.
  DEF_BIT(20, cet_ibt);
  // Bits [19:16] are reserved.
  DEF_BIT(15, hybrid);
  // Bits [14:11] are reserved.
  DEF_BIT(10, md_clear);
  // Bits [9:5] are reserved.
  DEF_BIT(4, fsrm);
  DEF_BIT(3, avx512_4fmaps);
  DEF_BIT(2, avx512_4vnniw);
  // Bits [1:0] are reserved.
};

//---------------------------------------------------------------------------//
// Leaf/Function 0xa.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
//---------------------------------------------------------------------------//

struct CpuidPerformanceMonitoringA
    : public CpuidIoValueBase<CpuidPerformanceMonitoringA, 0xa, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 24, ebx_vector_length);
  DEF_FIELD(23, 16, general_counter_width);
  DEF_FIELD(15, 8, num_general_counters);
  DEF_FIELD(7, 0, version);
};

struct CpuidPerformanceMonitoringB
    : public CpuidIoValueBase<CpuidPerformanceMonitoringB, 0xa, 0x0, CpuidIo::kEbx> {
  DEF_RSVDZ_FIELD(31, 7);
  DEF_BIT(6, branch_mispredict_retired_event_unavailable);
  DEF_BIT(5, branch_instruction_retired_event_unavailable);
  DEF_BIT(4, last_level_cache_miss_event_unavailable);
  DEF_BIT(3, last_level_cache_reference_event_unavailable);
  DEF_BIT(2, reference_cycle_event_unavailable);
  DEF_BIT(1, instruction_retired_event_unavailable);
  DEF_BIT(0, core_cycle_event_unavailable);
};

struct CpuidPerformanceMonitoringD
    : public CpuidIoValueBase<CpuidPerformanceMonitoringD, 0xa, 0x0, CpuidIo::kEdx> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_BIT(15, anythread_deprecation);
  DEF_RSVDZ_FIELD(14, 13);
  DEF_FIELD(12, 5, fixed_counter_width);
  DEF_FIELD(4, 0, num_fixed_counters);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0xb.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
//---------------------------------------------------------------------------//

struct CpuidTopologyEnumerationA : public hwreg::RegisterBase<CpuidTopologyEnumerationA, uint32_t> {
  // Bits [31:5] are reserved
  DEF_FIELD(4, 0, next_level_apic_id_shift);
};

struct CpuidTopologyEnumerationB : public hwreg::RegisterBase<CpuidTopologyEnumerationB, uint32_t> {
  // Bits [31:16] are reserved
  DEF_FIELD(15, 0, num_logical_processors);
};

struct CpuidTopologyEnumerationC : public hwreg::RegisterBase<CpuidTopologyEnumerationC, uint32_t> {
  enum class TopologyLevelType : uint8_t {
    kInvalid = 0,
    kSmt = 1,
    kCore = 2,
    kModule = 3,
    kTile = 4,
    kDie = 5,
  };

  // Bits [31:16] are reserved
  DEF_ENUM_FIELD(TopologyLevelType, 15, 8, level_type);
  DEF_FIELD(7, 0, level_number);
};

struct CpuidTopologyEnumerationD : public hwreg::RegisterBase<CpuidTopologyEnumerationD, uint32_t> {
  DEF_FIELD(31, 0, x2apic_id);
};

template <uint32_t Level>
using CpuidV1TopologyEnumerationA =
    CpuidIoValue<CpuidTopologyEnumerationA, 0xb, Level, CpuidIo::kEax>;

template <uint32_t Level>
using CpuidV1TopologyEnumerationB =
    CpuidIoValue<CpuidTopologyEnumerationB, 0xb, Level, CpuidIo::kEbx>;

template <uint32_t Level>
using CpuidV1TopologyEnumerationC =
    CpuidIoValue<CpuidTopologyEnumerationC, 0xb, Level, CpuidIo::kEcx>;

template <uint32_t Level>
using CpuidV1TopologyEnumerationD =
    CpuidIoValue<CpuidTopologyEnumerationD, 0xb, Level, CpuidIo::kEdx>;

//---------------------------------------------------------------------------//
// Leaf/Function 0x14.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
//---------------------------------------------------------------------------//

struct CpuidProcessorTraceMainB
    : public CpuidIoValueBase<CpuidProcessorTraceMainB, 0x14, 0x0, CpuidIo::kEbx> {
  DEF_RSVDZ_FIELD(31, 6);
  DEF_BIT(5, power_event_trace);
  DEF_BIT(4, ptwrite);
  DEF_BIT(3, mtc);
  DEF_BIT(2, ip_filtering);
  DEF_BIT(1, psb);
  DEF_BIT(0, crc3_filtering);
};

struct CpuidProcessorTraceMainC
    : public CpuidIoValueBase<CpuidProcessorTraceMainC, 0x14, 0x0, CpuidIo::kEcx> {
  DEF_BIT(31, lip);
  DEF_RSVDZ_FIELD(30, 4);
  DEF_BIT(3, trace_transport);
  DEF_BIT(2, single_range_output);
  DEF_BIT(1, topa_multi);
  DEF_BIT(0, topa);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x1f.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
//---------------------------------------------------------------------------//

template <uint32_t Level>
using CpuidV2TopologyEnumerationA =
    CpuidIoValue<CpuidTopologyEnumerationA, 0x1f, Level, CpuidIo::kEax>;

template <uint32_t Level>
using CpuidV2TopologyEnumerationB =
    CpuidIoValue<CpuidTopologyEnumerationB, 0x1f, Level, CpuidIo::kEbx>;

template <uint32_t Level>
using CpuidV2TopologyEnumerationC =
    CpuidIoValue<CpuidTopologyEnumerationC, 0x1f, Level, CpuidIo::kEcx>;

template <uint32_t Level>
using CpuidV2TopologyEnumerationD =
    CpuidIoValue<CpuidTopologyEnumerationD, 0x1f, Level, CpuidIo::kEdx>;

//---------------------------------------------------------------------------//
// Leaves/Functions 0x4000'0000 - 0x4fff'ffff.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
//
// This range is reserved by convention for hypervisors: the original RFC can be
// found at https://lwn.net/Articles/301888.
//
// Intel documents that "No existing or future CPU will return processor
// identification or feature information if the initial EAX value is in the
// range 40000000H to 4FFFFFFFH."
//---------------------------------------------------------------------------//

struct CpuidMaximumHypervisorLeaf
    : public CpuidIoValueBase<CpuidMaximumHypervisorLeaf, 0x4000'0000, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, leaf);
};

struct CpuidHypervisorNameB
    : public CpuidIoValueBase<CpuidHypervisorNameB, 0x4000'0000, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidHypervisorNameC
    : public CpuidIoValueBase<CpuidHypervisorNameC, 0x4000'0000, 0x0, CpuidIo::kEcx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidHypervisorNameD
    : public CpuidIoValueBase<CpuidHypervisorNameD, 0x4000'0000, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 0, value);
};

// HypervisorName is a simple class that serves to hold the content of a
// hypervisor's name (or "vendor string").
class HypervisorName {
 public:
  template <typename CpuidIoProvider>
  explicit HypervisorName(CpuidIoProvider&& io) {
    // Check if we are actually within a hypervisor.
    if (io.template Read<CpuidFeatureFlagsC>().hypervisor()) {
      const uint32_t values[] = {
          io.template Read<CpuidHypervisorNameB>().value(),
          io.template Read<CpuidHypervisorNameC>().value(),
          io.template Read<CpuidHypervisorNameD>().value(),
      };
      static_assert(kSize == sizeof(values));
      memcpy(str_.data(), values, kSize);
    } else {
      str_[0] = '\0';
    }
  }

  // Returns a string representation of name of the hypervisor, valid for as
  // long as the associated HypervisorName is in scope.
  std::string_view name() const {
    std::string_view name{str_.data(), str_.size()};
    return name.substr(0, name.find_first_of('\0'));
  }

 private:
  static constexpr size_t kSize = 12;
  std::array<char, kSize> str_;
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0000
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.4.1  Function 8000_0000h—Maximum Extended Function Number and Vendor String
//---------------------------------------------------------------------------//

// [amd/vol3]: CPUID Fn8000_0000_EAX Largest Extended Function Number
struct CpuidMaximumExtendedLeaf
    : public CpuidIoValueBase<CpuidMaximumExtendedLeaf, 0x8000'0000, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, leaf);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0001
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.4.2  Function 8000_0001h—Extended Processor and Processor Feature Identifiers.
//---------------------------------------------------------------------------//

// Despite Intel implementing (parts of) the 0x8000'0000 feature set, we
// namespace these features under "AMD", as it was pragmatically following
// AMD's lead, and as Intel has already nabbed the more appropriate name of
// "extended features" - this being the extended leaf range - with leaf 0x7.
//
// TODO(fxbug.dev/68404): Pass hwreg::EnableAsmGeneration when safe to do so.

struct CpuidAmdFeatureFlagsC
    : public CpuidIoValueBase<CpuidAmdFeatureFlagsC, 0x8000'0001, 0x0, CpuidIo::kEcx> {
  // Bits [31:28] are reserved.
  DEF_BIT(27, perf_tsc);
  DEF_BIT(26, data_breakpoint_extension);
  // Bit 25 is reserved.
  DEF_BIT(24, perf_ctr_ext_nb);
  DEF_BIT(23, perf_ctr_ext_core);
  DEF_BIT(22, topology_extensions);
  DEF_BIT(21, tbm);
  // Bits [20:17] are reserved.
  DEF_BIT(16, fma4);
  DEF_BIT(15, lwp);
  // Bit 14 is reserved.
  DEF_BIT(13, wdt);
  DEF_BIT(12, skinit);
  DEF_BIT(11, xop);
  DEF_BIT(10, ibs);
  DEF_BIT(9, osvw);
  DEF_BIT(8, prefetchw);
  DEF_BIT(7, misaligned_sse);
  DEF_BIT(6, sse4a);
  DEF_BIT(5, lzcnt);
  DEF_BIT(4, alt_move_cr8);
  DEF_BIT(3, ext_apic_space);
  DEF_BIT(2, svm);
  DEF_BIT(1, cmp_legacy);
  DEF_BIT(0, lahf_sahf);
};

struct CpuidAmdFeatureFlagsD
    : public CpuidIoValueBase<CpuidAmdFeatureFlagsD, 0x8000'0001, 0x0, CpuidIo::kEdx> {
  DEF_BIT(31, has_3dnow);
  DEF_BIT(30, has_3dnow_ext);
  DEF_BIT(29, lm);
  // Bit 28 is reserved.
  DEF_BIT(27, rdtscp);
  DEF_BIT(26, page1gb);
  DEF_BIT(25, ffxsr);
  DEF_BIT(24, fxsr);
  DEF_BIT(23, mmx);
  DEF_BIT(22, mmx_ext);
  // Bit 21 is reserved.
  DEF_BIT(20, nx);
  // Bits [19:18] are reserved.
  DEF_BIT(17, pse36);
  DEF_BIT(16, pat);
  DEF_BIT(15, cmov);
  DEF_BIT(14, mca);
  DEF_BIT(13, pge);
  DEF_BIT(12, mtrr);
  DEF_BIT(11, syscall_sysret);
  // Bit 10 is reserved.
  DEF_BIT(9, apic);
  DEF_BIT(8, cmpxchg8b);
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
// Leaves/Functions 0x8000'0002 - 0x8000'0004
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.4.3  Functions 8000_0002h–8000_0004h—Extended Processor Name String
//---------------------------------------------------------------------------//

// The 2,3,4 below refer to the low digit of the leaf number and not the
// express (zero-based) index into how the combine to form the processor name
// string.
struct CpuidProcessorName2A
    : public CpuidIoValueBase<CpuidProcessorName2A, 0x8000'0002, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName2B
    : public CpuidIoValueBase<CpuidProcessorName2B, 0x8000'0002, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName2C
    : public CpuidIoValueBase<CpuidProcessorName2C, 0x8000'0002, 0x0, CpuidIo::kEcx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName2D
    : public CpuidIoValueBase<CpuidProcessorName2D, 0x8000'0002, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 0, value);
};

struct CpuidProcessorName3A
    : public CpuidIoValueBase<CpuidProcessorName3A, 0x8000'0003, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName3B
    : public CpuidIoValueBase<CpuidProcessorName3B, 0x8000'0003, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName3C
    : public CpuidIoValueBase<CpuidProcessorName3C, 0x8000'0003, 0x0, CpuidIo::kEcx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName3D
    : public CpuidIoValueBase<CpuidProcessorName3D, 0x8000'0003, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 0, value);
};

struct CpuidProcessorName4A
    : public CpuidIoValueBase<CpuidProcessorName4A, 0x8000'0004, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName4B
    : public CpuidIoValueBase<CpuidProcessorName4B, 0x8000'0004, 0x0, CpuidIo::kEbx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName4C
    : public CpuidIoValueBase<CpuidProcessorName4C, 0x8000'0004, 0x0, CpuidIo::kEcx> {
  DEF_FIELD(31, 0, value);
};
struct CpuidProcessorName4D
    : public CpuidIoValueBase<CpuidProcessorName4D, 0x8000'0004, 0x0, CpuidIo::kEdx> {
  DEF_FIELD(31, 0, value);
};

// ProcessorName is a simple class that serves to hold the content of a
// processor name (or "brand string" in Intel-speak), a general identifier.
class ProcessorName {
 public:
  template <typename CpuidIoProvider>
  explicit ProcessorName(CpuidIoProvider&& io) {
    // The name string needs leaves 0x8000'0002-0x8000'0004.
    if (io.template Read<CpuidMaximumExtendedLeaf>().leaf() >= CpuidProcessorName4D::kLeaf) {
      const uint32_t values[] = {
          io.template Read<CpuidProcessorName2A>().value(),
          io.template Read<CpuidProcessorName2B>().value(),
          io.template Read<CpuidProcessorName2C>().value(),
          io.template Read<CpuidProcessorName2D>().value(),
          io.template Read<CpuidProcessorName3A>().value(),
          io.template Read<CpuidProcessorName3B>().value(),
          io.template Read<CpuidProcessorName3C>().value(),
          io.template Read<CpuidProcessorName3D>().value(),
          io.template Read<CpuidProcessorName4A>().value(),
          io.template Read<CpuidProcessorName4B>().value(),
          io.template Read<CpuidProcessorName4C>().value(),
          io.template Read<CpuidProcessorName4D>().value(),
      };
      static_assert(kSize == sizeof(values));
      memcpy(str_.data(), values, kSize);
    } else {
      str_[0] = '\0';
    }
  }

  ProcessorName() = delete;

  // Returns a string representation of name of the processor, valid for as
  // long as the associated ProcessorName is in scope.
  std::string_view name() const {
    std::string_view name{str_.data(), str_.size()};
    return name.substr(0, name.find('\0'));
  }

 private:
  static constexpr size_t kSize = 48;
  std::array<char, kSize> str_;
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0005
//
// [amd/vol3]: E.4.4  Function 8000_0005h — L1 Cache and TLB Information.
//---------------------------------------------------------------------------//

struct CpuidL1CacheInformation : public hwreg::RegisterBase<CpuidL1CacheInformation, uint32_t> {
  // The value of the associativity field representing full associativity.
  static constexpr uint8_t kFullyAssociative = 0xff;

  DEF_FIELD(31, 24, size_kb);
  DEF_FIELD(23, 16, assoc);
  DEF_FIELD(15, 8, lines_per_tag);
  DEF_FIELD(7, 0, line_size);

  // Indeterminate if zero.
  size_t ways_of_associativity() const;

  // Indeterminate if std::nullopt.
  std::optional<bool> fully_associative() const;
};

using CpuidL1DataCacheInformation =
    CpuidIoValue<CpuidL1CacheInformation, 0x8000'0005, 0, CpuidIo::kEcx>;

using CpuidL1InstructionCacheInformation =
    CpuidIoValue<CpuidL1CacheInformation, 0x8000'0005, 0, CpuidIo::kEdx>;

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0006
//
// [amd/vol3]: E.4.5  Function 8000_0006h—L2 Cache and TLB and L3 Cache Information.
//---------------------------------------------------------------------------//

enum class CpuidL2L3Associativity : uint8_t {
  kDisabled = 0x0,
  kDirectMapped = 0x1,
  k2Way = 0x2,
  k3Way = 0x3,
  k4Way = 0x4,
  k6Way = 0x5,
  k8Way = 0x6,
  // 0x7 is reserved.
  k16Way = 0x8,
  kSeeLeaf0x8000001d = 0x9,
  k32Way = 0xa,
  k48Way = 0xb,
  k64Way = 0xc,
  k96Way = 0xd,
  k128Way = 0xe,
  kFullyAssociative = 0xf,
  // 0x10-0xff are reserved.
};

struct CpuidL2CacheInformation
    : public CpuidIoValueBase<CpuidL2CacheInformation, 0x8000'0006, 0, CpuidIo::kEcx> {
  DEF_FIELD(31, 16, size_kb);
  DEF_ENUM_FIELD(CpuidL2L3Associativity, 15, 12, assoc);
  DEF_FIELD(11, 8, lines_per_tag);
  DEF_FIELD(7, 0, line_size);

  // Indeterminate if zero.
  size_t ways_of_associativity() const;

  // Indeterminate if std::nullopt.
  std::optional<bool> fully_associative() const;
};

struct CpuidL3CacheInformation
    : public CpuidIoValueBase<CpuidL3CacheInformation, 0x8000'0006, 0, CpuidIo::kEdx> {
  DEF_FIELD(31, 18, size);
  // Bits [17:16] are reserved.
  DEF_ENUM_FIELD(CpuidL2L3Associativity, 15, 12, assoc);
  DEF_FIELD(11, 8, lines_per_tag);
  DEF_FIELD(7, 0, line_size);

  // Indeterminate if zero.
  size_t ways_of_associativity() const;

  // Indeterminate if std::nullopt.
  std::optional<bool> fully_associative() const;
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0007
//
// [amd/vol3]: E.4.6  Function 8000_0007h—Processor Power Management and RAS Capabilities.
//---------------------------------------------------------------------------//

struct CpuidAdvancedPowerFeatureFlags
    : public CpuidIoValueBase<CpuidAdvancedPowerFeatureFlags, 0x8000'0007, 0x0, CpuidIo::kEdx> {
  // Bits [31:13] are reserved.
  DEF_BIT(12, proc_power_reporting);
  DEF_BIT(11, proc_feedback_interface);
  DEF_BIT(10, eff_freq);
  DEF_BIT(9, cpb);
  DEF_BIT(8, tsc_invariant);
  DEF_BIT(7, hw_pstate);
  DEF_BIT(6, has_100mhz_steps);
  // Bit 5 is reserved.
  DEF_BIT(4, tm);
  DEF_BIT(3, ttp);
  DEF_BIT(2, vid);
  DEF_BIT(1, fid);
  DEF_BIT(0, ts);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'0008
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.4.7  Function 8000_0008h—Processor Capacity Parameters and
// Extended Feature Identification.
//---------------------------------------------------------------------------//

struct CpuidAddressSizeInfo
    : public CpuidIoValueBase<CpuidAddressSizeInfo, 0x8000'0008, 0x0, CpuidIo::kEax> {
  // Bits [31:24] are reserved.
  DEF_FIELD(23, 16, guest_phys_addr_bits);
  DEF_FIELD(15, 8, linear_addr_bits);
  DEF_FIELD(7, 0, phys_addr_bits);
};

// [amd/ibc] details bits [18:14] and 12.
// [amd/ssbd] details bits [26:24].
struct CpuidExtendedAmdFeatureFlagsB
    : public CpuidIoValueBase<CpuidExtendedAmdFeatureFlagsB, 0x8000'0008, 0x0, CpuidIo::kEbx> {
  // Bits [32:27] are reserved.
  DEF_BIT(26, ssb_no);
  DEF_BIT(25, virt_ssbd);
  DEF_BIT(24, ssbd);
  // Bits [23:19] are reserved.
  DEF_BIT(18, prefers_ibrs);
  DEF_BIT(17, stibp_always_on);
  DEF_BIT(16, ibrs_always_on);
  DEF_BIT(15, stibp);
  DEF_BIT(14, ibrs);
  // Bit 13 is reserved.
  DEF_BIT(12, ibpb);
  // Bits [11:10] are reserved.
  DEF_BIT(9, wbnoinvd);
  DEF_BIT(8, mcommit);
  // Bits [7:5] are reserved.
  DEF_BIT(4, rdpru);
  // Bit 3 is reserved.
  DEF_BIT(2, rstr_fp_err_ptrs);
  DEF_BIT(1, inst_ret_cnt_msr);
  DEF_BIT(0, clzero);
};

struct CpuidExtendedSizeInfo
    : public CpuidIoValueBase<CpuidExtendedSizeInfo, 0x8000'0008, 0x0, CpuidIo::kEcx> {
  enum class PerfTimestampCounterSize : uint8_t {
    k40Bits = 0b00,
    k48Bits = 0b01,
    k56Bits = 0b10,
    k64Bits = 0b11,
  };

  // Bits [31:18] are reserved.
  DEF_ENUM_FIELD(PerfTimestampCounterSize, 17, 16, perf_tsc_size);
  DEF_FIELD(15, 12, apic_id_size);
  // Bits [11:8] are reserved.
  DEF_FIELD(7, 0, nc);
};

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'001d
//
// [amd/vol3]: E.4.15  Function 8000_001Dh—Cache Topology Information.
//---------------------------------------------------------------------------//

template <uint32_t Subleaf>
using CpuidAmdCacheTopologyA =
    CpuidIoValue<CpuidCacheTopologyA, 0x8000'001d, Subleaf, CpuidIo::kEax>;

template <uint32_t Subleaf>
using CpuidAmdCacheTopologyB =
    CpuidIoValue<CpuidCacheTopologyB, 0x8000'001d, Subleaf, CpuidIo::kEbx>;

template <uint32_t Subleaf>
using CpuidAmdCacheTopologyC =
    CpuidIoValue<CpuidCacheTopologyC, 0x8000'001d, Subleaf, CpuidIo::kEcx>;

template <uint32_t Subleaf>
using CpuidAmdCacheTopologyD =
    CpuidIoValue<CpuidCacheTopologyD, 0x8000'001d, Subleaf, CpuidIo::kEdx>;

//---------------------------------------------------------------------------//
// Leaf/Function 0x8000'001e
//
// [amd/vol3]: E.4.16  Function 8000_001Eh—Processor Topology Information.
//---------------------------------------------------------------------------//

struct CpuidExtendedApicId
    : public CpuidIoValueBase<CpuidExtendedApicId, 0x8000'001e, 0x0, CpuidIo::kEax> {
  DEF_FIELD(31, 0, x2apic_id);
};

struct CpuidComputeUnitInfo
    : public CpuidIoValueBase<CpuidComputeUnitInfo, 0x8000'001e, 0x0, CpuidIo::kEbx> {
  // Bits [31:16] are reserved.
  DEF_FIELD(15, 8, threads_per_compute_unit);
  DEF_FIELD(7, 0, compute_unit_id);
};

struct CpuidNodeInfo : public CpuidIoValueBase<CpuidNodeInfo, 0x8000'001e, 0x0, CpuidIo::kEcx> {
  // Bits [31:11] are reserved.
  DEF_FIELD(10, 8, nodes_per_package);
  DEF_FIELD(7, 0, node_id);
};

// Whether the leaf associated with a given CPUID value type is supported.
template <typename CpuidValueType, typename CpuidIoProvider>
inline bool CpuidSupports(CpuidIoProvider&& cpuid) {
  if constexpr (CpuidValueType::kLeaf >= 0x8000'0000) {
    auto max = cpuid.template Read<CpuidMaximumExtendedLeaf>().leaf();

    // [amd/vol3]: E.4.15  Function 8000_001Dh—Cache Topology Information.
    // [amd/vol3]: E.4.16  Function 8000_001Eh—Processor Topology Information.
    //
    // If topology extensions are not advertised, these leaves are reserved.
    if constexpr (CpuidValueType::kLeaf == 0x8000'001d || CpuidValueType::kLeaf == 0x8000'001e) {
      return (CpuidValueType::kLeaf <= max) &&
             cpuid.template Read<CpuidAmdFeatureFlagsC>().topology_extensions();
    } else {
      return CpuidValueType::kLeaf <= max;
    }
  } else if constexpr (CpuidValueType::kLeaf >= 0x4000'0000) {
    auto max = cpuid.template Read<CpuidMaximumHypervisorLeaf>().leaf();
    return CpuidValueType::kLeaf <= max;
  } else {
    auto max = cpuid.template Read<CpuidMaximumLeaf>().leaf();
    return CpuidValueType::kLeaf <= max;
  }
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
