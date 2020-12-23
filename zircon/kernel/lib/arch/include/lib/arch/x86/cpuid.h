// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_

#include <array>
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
// as well as the associated register type.
//
// Assembly macro generation requires the use of `hwreg::EnablePrinter`; to
// make such use-cases more conveniently accessible - and since the code-gen
// cost here is rather minimal - we generally use the feature  below.
template <typename ValueType, uint32_t Leaf, uint32_t Subleaf, CpuidIo::Register OutputRegister>
struct CpuidIoValueBase : public hwreg::RegisterBase<ValueType, uint32_t, hwreg::EnablePrinter>,
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
  kAmdFamily0x15,
  kAmdFamily0x16,
  kAmdFamily0x17,
  kAmdFamily0x19,
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

// [intel/vol2]: Table 3-10.  Feature Information Returned in the ECX Register.
// [amd/vol3]: E.3.2, CPUID Fn0000_0001_ECX Feature Identifiers.
struct CpuidFeatureFlagsC : public CpuidIoValueBase<CpuidFeatureFlagsC, 0x1, 0x0, CpuidIo::kEcx> {
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
struct CpuidFeatureFlagsD : public CpuidIoValueBase<CpuidFeatureFlagsD, 0x1, 0x0, CpuidIo::kEdx> {
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
// Leaf/Function 0x7.
//
// [intel/vol2]: Table 3-8.  Information Returned by CPUID Instruction.
// [amd/vol3]: E.3.6  Function 7h—Structured Extended Feature Identifier
//---------------------------------------------------------------------------//

// [amd/vol3]: E.3.6, CPUID Fn0000_0007_EBX_x0 Structured Extended Feature Identifiers (ECX=0).
struct CpuidExtendedFeatureFlagsB
    : public CpuidIoValueBase<CpuidExtendedFeatureFlagsB, 0x7, 0x0, CpuidIo::kEbx> {
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

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_CPUID_H_
