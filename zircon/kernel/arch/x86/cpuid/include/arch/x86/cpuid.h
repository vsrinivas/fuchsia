// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_H_
#define ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_H_

#include <assert.h>

#include <cstdint>
#include <cstring>

namespace cpu_id {

struct Registers {
  enum {
    EAX = 0,
    EBX = 1,
    ECX = 2,
    EDX = 3,
  };

  inline uint32_t eax() const { return reg[EAX]; }
  inline uint32_t ebx() const { return reg[EBX]; }
  inline uint32_t edx() const { return reg[EDX]; }
  inline uint32_t ecx() const { return reg[ECX]; }

  uint32_t reg[4];
};

template <size_t count>
struct SubLeaves {
  Registers subleaf[count];
  static constexpr size_t size = count;
};

// Extracts the manufacturer id string from call with EAX=0.
class ManufacturerInfo {
 public:
  enum Manufacturer { INTEL, AMD, OTHER };

  // How many chars are in a manufacturer id.
  static constexpr size_t kManufacturerIdLength = 12;

  ManufacturerInfo(Registers leaf0, Registers leaf8_0);

  Manufacturer manufacturer() const;

  // Reads the manufacturer id and writes it into the buffer, buffer should be
  // at least kManufacturerIdLength in length. This will not null-terminate the
  // string.
  void manufacturer_id(char* buffer) const;

  // Highest leaf (EAX parameter to cpuid) that this processor supports.
  size_t highest_cpuid_leaf() const;

  // Highest leaf (EAX parameter to cpuid) that this processor supports in the
  // extended range (> 0x80000000);
  size_t highest_extended_cpuid_leaf() const;

 private:
  const Registers leaf0_;
  const Registers leaf8_0_;
};

// Extracts the processor signature/id from call with EAX=1.
class ProcessorId {
 public:
  ProcessorId(Registers registers);

  // Stepping, or revision, of this model.
  uint8_t stepping() const;

  // Model inside of the given family.
  uint16_t model() const;

  // Family of processors to which this chip belongs.
  uint16_t family() const;

  // Return the full 32-bit identifier of this chip.
  uint32_t signature() const;

  // APIC ID of the processor on which this object was generated. Note this
  // class uses a cached copy of registers so if this object was generated on
  // a differnet processor this value could be misleading.
  uint8_t local_apic_id() const;

 private:
  const Registers registers_;
};

// Extracts feature flags from EAX=1 call and extended feature flags calls.
// See docs for full listing of possible features, this class is not
// comprehensive, things are added as they are required.
//
// The most recent Intel CPUID bit assignments are in the
// "Intel® Architecture Instruction Set Extensions and Future Features Programming Reference",
// https://software.intel.com/sites/default/files/managed/c5/15/architecture-instruction-set-extensions-programming-reference.pdf
class Features {
 public:
  enum LeafIndex {
    LEAF1,  // Feature Information
    LEAF6,  // Thermal and Power Management Leaf
    LEAF7,  // Structured Extended Feature Flags
    LEAF8_01,
    LEAF8_07,
    INVALID_SET = 254,
  };

  struct Feature {
    uint8_t leaf = INVALID_SET;
    uint8_t reg;
    uint8_t bit;
  };

  // Feature "enum".
  static constexpr Feature FPU = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 0};
  static constexpr Feature VME = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 1};
  static constexpr Feature DE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 2};
  static constexpr Feature PSE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 3};
  static constexpr Feature TSC = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 4};
  static constexpr Feature MSR = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 5};
  static constexpr Feature PAE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 6};
  static constexpr Feature MCE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 7};
  static constexpr Feature CX8 = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 8};
  static constexpr Feature APIC = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 9};
  static constexpr Feature SEP = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 11};
  static constexpr Feature MTRR = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 12};
  static constexpr Feature PGE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 13};
  static constexpr Feature MCA = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 14};
  static constexpr Feature CMOV = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 15};
  static constexpr Feature PAT = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 16};
  static constexpr Feature PSE36 = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 17};
  static constexpr Feature PSN = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 18};
  static constexpr Feature CLFSH = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 19};
  static constexpr Feature DS = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 21};
  static constexpr Feature ACPI = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 22};
  static constexpr Feature MMX = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 23};
  static constexpr Feature FXSR = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 24};
  static constexpr Feature SSE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 25};
  static constexpr Feature SSE2 = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 26};
  static constexpr Feature SS = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 27};
  static constexpr Feature HTT = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 28};
  static constexpr Feature TM = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 29};
  static constexpr Feature PBE = {.leaf = LEAF1, .reg = Registers::EDX, .bit = 31};
  static constexpr Feature SSE3 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 0};
  static constexpr Feature PCLMULQDQ = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 1};
  static constexpr Feature DTES64 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 2};
  static constexpr Feature MONITOR = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 3};
  static constexpr Feature DS_CPL = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 4};
  static constexpr Feature VMX = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 5};
  static constexpr Feature SMX = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 6};
  static constexpr Feature EST = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 7};
  static constexpr Feature TM2 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 8};
  static constexpr Feature SSSE3 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 9};
  static constexpr Feature CNXT_ID = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 10};
  static constexpr Feature SDBG = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 11};
  static constexpr Feature FMA = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 12};
  static constexpr Feature CX16 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 13};
  static constexpr Feature XTPR = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 14};
  static constexpr Feature PDCM = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 15};
  static constexpr Feature PCID = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 17};
  static constexpr Feature DCA = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 18};
  static constexpr Feature SSE4_1 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 19};
  static constexpr Feature SSE4_2 = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 20};
  static constexpr Feature X2APIC = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 21};
  static constexpr Feature MOVBE = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 22};
  static constexpr Feature POPCNT = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 23};
  static constexpr Feature TSC_DEADLINE = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 24};
  static constexpr Feature AES = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 25};
  static constexpr Feature XSAVE = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 26};
  static constexpr Feature OSXSAVE = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 27};
  static constexpr Feature AVX = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 28};
  static constexpr Feature F16C = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 29};
  static constexpr Feature RDRAND = {.leaf = LEAF1, .reg = Registers::ECX, .bit = 30};

  static constexpr Feature TURBO = {.leaf = LEAF6, .reg = Registers::EAX, .bit = 1};
  static constexpr Feature HWP = {.leaf = LEAF6, .reg = Registers::EAX, .bit = 7};
  static constexpr Feature HWP_PREF = {.leaf = LEAF6, .reg = Registers::EAX, .bit = 10};
  static constexpr Feature HWP_PKG = {.leaf = LEAF6, .reg = Registers::EAX, .bit = 11};
  static constexpr Feature HWP_REQ_FAST = {.leaf = LEAF6, .reg = Registers::EAX, .bit = 18};
  static constexpr Feature MPERFAPERF = {.leaf = LEAF6, .reg = Registers::ECX, .bit = 0};
  static constexpr Feature EPB = {.leaf = LEAF6, .reg = Registers::ECX, .bit = 3};

  static constexpr Feature FSGSBASE = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 0};
  static constexpr Feature SGX = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 2};
  static constexpr Feature BMI1 = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 3};
  static constexpr Feature HLE = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 4};
  static constexpr Feature AVX2 = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 5};
  static constexpr Feature SMEP = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 7};
  static constexpr Feature BMI2 = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 8};
  static constexpr Feature ERMS = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 9};
  static constexpr Feature INVPCID = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 10};
  static constexpr Feature RTM = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 11};
  static constexpr Feature PQM = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 12};
  static constexpr Feature PQE = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 15};
  static constexpr Feature AVX512F = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 16};
  static constexpr Feature AVX512DQ = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 17};
  static constexpr Feature RDSEED = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 18};
  static constexpr Feature ADX = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 19};
  static constexpr Feature SMAP = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 20};
  static constexpr Feature AVX512IFMA = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 21};
  static constexpr Feature CLWB = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 24};
  static constexpr Feature INTEL_PT = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 25};
  static constexpr Feature AVX512PF = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 26};
  static constexpr Feature AVX512ER = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 27};
  static constexpr Feature AVX512CD = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 28};
  static constexpr Feature SHA = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 29};
  static constexpr Feature AVX512BW = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 30};
  static constexpr Feature AVX512VL = {.leaf = LEAF7, .reg = Registers::EBX, .bit = 31};
  static constexpr Feature PREFETCHWT1 = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 0};
  static constexpr Feature AVX512VBMI = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 1};
  static constexpr Feature UMIP = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 2};
  static constexpr Feature PKU = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 3};
  static constexpr Feature AVX512VBMI2 = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 6};
  static constexpr Feature GFNI = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 8};
  static constexpr Feature VAES = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 9};
  static constexpr Feature VPCLMULQDQ = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 10};
  static constexpr Feature AVX512VNNI = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 11};
  static constexpr Feature AVX512BITALG = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 12};
  static constexpr Feature AVX512VPOPCNTDQ = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 14};
  static constexpr Feature RDPID = {.leaf = LEAF7, .reg = Registers::ECX, .bit = 22};
  static constexpr Feature AVX512_4VNNIW = {.leaf = LEAF7, .reg = Registers::EDX, .bit = 2};
  static constexpr Feature AVX512_4FMAPS = {.leaf = LEAF7, .reg = Registers::EDX, .bit = 3};
  static constexpr Feature MD_CLEAR = {.leaf = LEAF7, .reg = Registers::EDX, .bit = 10};
  static constexpr Feature CLFLUSH = {.leaf = LEAF7, .reg = Registers::EDX, .bit = 19};
  static constexpr Feature ARCH_CAPABILITIES = {.leaf = LEAF7, .reg = Registers::EDX, .bit = 29};

  static constexpr Feature LAHF = {.leaf = LEAF8_01, .reg = Registers::ECX, .bit = 0};
  static constexpr Feature RDTSCP = {.leaf = LEAF8_01, .reg = Registers::EDX, .bit = 27};
  static constexpr Feature PDPE1GB = {.leaf = LEAF8_01, .reg = Registers::EDX, .bit = 26};
  static constexpr Feature XD = {.leaf = LEAF8_01, .reg = Registers::EDX, .bit = 20};
  static constexpr Feature SYSCALL = {.leaf = LEAF8_01, .reg = Registers::EDX, .bit = 11};

  static constexpr Feature CPB = {.leaf = LEAF8_07, .reg = Registers::EDX, .bit = 9};

  Features(Registers leaf1, Registers leaf6, Registers leaf7, Registers leaf8_01,
           Registers leaf8_07);

  inline bool HasFeature(Feature feature) const {
    DEBUG_ASSERT_MSG(
        feature.leaf < kLeafCount && feature.reg <= Registers::EDX && feature.bit <= 32,
        "set: %u reg:%u %d bit: %u", feature.leaf, feature.reg, Registers::EDX, feature.bit);
    return leaves_[feature.leaf].reg[feature.reg] & (1 << feature.bit);
  }

  // Returns the maximum supported logical processors in a physical package.
  // This is NOT that same as the number of logical processors present.
  uint8_t max_logical_processors_in_package() const;

 private:
  static constexpr size_t kLeafCount = 6;

  const Registers leaves_[kLeafCount];
};

// Wraps the CPUID instruction on x86, provides helpers to parse the output and
// allows unit testing of libraries reading it.
// CpuId is uncached; every call results in one (or more) invocations of CPUID.
class CpuId {
 public:
  virtual ~CpuId() = default;
  virtual ManufacturerInfo ReadManufacturerInfo() const;
  // Return ProcessorId; provides (Extended) Family/Model/Stepping.
  virtual ProcessorId ReadProcessorId() const;
  virtual Features ReadFeatures() const;

 private:
};

}  // namespace cpu_id

#endif  // ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_H_
