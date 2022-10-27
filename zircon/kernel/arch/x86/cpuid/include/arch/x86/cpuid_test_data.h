// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_TEST_DATA_H_
#define ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_TEST_DATA_H_

#include <arch/x86/cpuid.h>

namespace cpu_id {

struct TestDataSet {
  Features::Feature features[200];
  Features::Feature missing_features[200];
  Registers leaf0;
  Registers leaf1;
  Registers leaf4;
  Registers leaf6;
  Registers leaf7;
  Registers leaf8_0;
  Registers leaf8_1;
  Registers leaf8_7;
  Registers leaf8_1D;
  Registers leaf8_1E;
};

// Queried from an Intel Core i5-6260U (NUC6i5SYH)
const TestDataSet kTestDataCorei5_6260U = {
    .features = {Features::FPU,     Features::VME,     Features::DE,     Features::PSE,
                 Features::TSC,     Features::MSR,     Features::PAE,    Features::MCE,
                 Features::CX8,     Features::APIC,    Features::SEP,    Features::MTRR,
                 Features::FXSR,    Features::SSE,     Features::SSE2,   Features::SS,
                 Features::XD,      Features::PDPE1GB, Features::RDTSCP, Features::PCLMULQDQ,
                 Features::DTES64,  Features::MONITOR, Features::DS_CPL, Features::VMX,
                 Features::RDSEED,  Features::EST,     Features::TM2,    Features::SSSE3,
                 Features::PDCM,    Features::PCID,    Features::SSE4_1, Features::MD_CLEAR,
                 Features::SSE4_2,  Features::X2APIC,  Features::MOVBE,  Features::POPCNT,
                 Features::AES,     Features::XSAVE,   Features::AVX,    Features::F16C,
                 Features::RDRAND,  Features::LAHF,    Features::BMI1,   Features::ERMS,
                 Features::AVX2,    Features::SMEP,    Features::BMI2,   Features::ADX,
                 Features::INVPCID, Features::TURBO,   Features::HWP,    Features::HWP_PREF,
                 Features::EPB},
    .missing_features = {Features::PSN, Features::AVX512VNNI},
    .leaf0 = {.reg = {0x16, 0x756e6547, 0x6c65746e, 0x49656e69}},
    .leaf1 = {.reg = {0x406e3, 0x100800, 0x7ffafbbf, 0xbfebfbff}},
    .leaf4 = {.reg = {0x1c004121, 0x1c0003f, 0x3f, 0x0}},
    .leaf6 = {.reg = {0x4f7, 0x2, 0x9, 0x0}},
    .leaf7 = {.reg = {0x0, 0x29c67af, 0x0, 0x9c002400}},
    .leaf8_0 = {.reg = {0x80000008, 0x0, 0x0, 0x0}},
    .leaf8_1 = {.reg = {0x0, 0x0, 0x121, 0x2c100800}},
    .leaf8_7 = {},
    .leaf8_1D = {.reg = {0x708, 0xb54, 0x64, 0x0}},
    .leaf8_1E = {.reg = {0x708, 0xb54, 0x64, 0x0}},
};

// Queried from a Intel Xeon E5-2690v4.
const TestDataSet kTestDataXeon2690v4 = {
    .features = {Features::FPU,     Features::VME,     Features::DE,     Features::PSE,
                 Features::TSC,     Features::MSR,     Features::PAE,    Features::MCE,
                 Features::CX8,     Features::APIC,    Features::SEP,    Features::MTRR,
                 Features::PGE,     Features::MCA,     Features::CMOV,   Features::PAT,
                 Features::PSE36,   Features::ACPI,    Features::MMX,    Features::FSGSBASE,
                 Features::FXSR,    Features::SSE,     Features::SSE2,   Features::SS,
                 Features::HTT,     Features::TM,      Features::PBE,    Features::SYSCALL,
                 Features::XD,      Features::PDPE1GB, Features::RDTSCP, Features::PCLMULQDQ,
                 Features::DTES64,  Features::MONITOR, Features::DS_CPL, Features::VMX,
                 Features::SMX,     Features::EST,     Features::TM2,    Features::SSSE3,
                 Features::SDBG,    Features::FMA,     Features::CX16,   Features::XTPR,
                 Features::PDCM,    Features::PCID,    Features::DCA,    Features::SSE4_1,
                 Features::SSE4_2,  Features::X2APIC,  Features::MOVBE,  Features::POPCNT,
                 Features::AES,     Features::XSAVE,   Features::AVX,    Features::F16C,
                 Features::RDRAND,  Features::LAHF,    Features::BMI1,   Features::HLE,
                 Features::AVX2,    Features::SMEP,    Features::BMI2,   Features::ERMS,
                 Features::INVPCID, Features::RTM,     Features::RDSEED, Features::ADX,
                 Features::SMAP,    Features::INTEL_PT},
    .missing_features = {Features::PSN, Features::AVX512VNNI},
    .leaf0 = {.reg = {0x14, 0x756E6547, 0x6C65746E, 0x49656E69}},
    .leaf1 = {.reg = {0x406F1, 0x12200800, 0x7FFEFBFF, 0xBFEBFBFF}},
    .leaf4 = {.reg = {0x3C07C163, 0x4C0003F, 0x6FFF, 0x6}},
    .leaf6 = {},
    .leaf7 = {.reg = {0x0, 0x21CBFBB, 0x0, 0x9C000000}},
    .leaf8_0 = {.reg = {0x80000008, 0x0, 0x0, 0x0}},
    .leaf8_1 = {.reg = {0x0, 0x0, 0x121, 0x2C100800}},
    .leaf8_7 = {},
    .leaf8_1D = {.reg = {0x0, 0x1, 0x1, 0x0}},
    .leaf8_1E = {.reg = {0x0, 0x1, 0x1, 0x0}},
};

// Queried from a AMD ThreadRipper 2970wx.
const TestDataSet kTestDataThreadRipper2970wx = {
    .features = {Features::FPU,   Features::VME,  Features::DE,       Features::PSE,
                 Features::TSC,   Features::MSR,  Features::PAE,      Features::MCE,
                 Features::CX8,   Features::APIC, Features::SEP,      Features::MTRR,
                 Features::PGE,   Features::MCA,  Features::CMOV,     Features::PAT,
                 Features::PSE36, Features::MMX,  Features::FSGSBASE, Features::FXSR,
                 Features::SSE,   Features::SSE2, Features::CPB},
    .missing_features =
        {
            Features::PSN,
            Features::AVX512VNNI,
            Features::ACPI,
            Features::SS,
        },
    .leaf0 = {.reg = {0xD, 0x68747541, 0x444D4163, 0x69746E65}},
    .leaf1 = {.reg = {0x800F82, 0x12300800, 0x7ED8320B, 0x178BFBFF}},
    .leaf4 = {.reg = {0x0, 0x0, 0x0, 0x0}},
    .leaf6 = {},
    .leaf7 = {.reg = {0x0, 0x209C01A9, 0x0, 0x0}},
    .leaf8_0 = {.reg = {0x8000001F, 0x68747541, 0x444D4163, 0x69746E65}},
    .leaf8_1 = {.reg = {0x800F82, 0x70000000, 0x35C233FF, 0x2FD3FBFF}},
    .leaf8_7 = {.reg = {0x0, 0x1b, 0x0, 0x6799}},
    .leaf8_1D = {.reg = {0x14163, 0x3C0003F, 0x1FFF, 0x1}},
    .leaf8_1E = {.reg = {0x34, 0x102, 0x303, 0x0}},
};

// Queried from "AMD A4-9120C RADEON R4, 5 COMPUTE CORES 2C+3G" (HP Chromebook 14)
// 'Stoney Ridge' APU, AMD Excavator CPU
const TestDataSet kTestDataAmdA49120C = {
    // CPU features we do expect to find.
    .features = {Features::FPU,   Features::VME,       Features::DE,    Features::PSE,
                 Features::TSC,   Features::MSR,       Features::PAE,   Features::MCE,
                 Features::CX8,   Features::APIC,      Features::SEP,   Features::MTRR,
                 Features::PGE,   Features::MCA,       Features::CMOV,  Features::PAT,
                 Features::PSE36, Features::MMX,       Features::CLFSH, Features::FSGSBASE,
                 Features::MOVBE, Features::MPERFAPERF},
    // Sample of CPU features we do not expect to find.
    .missing_features = {Features::SGX, Features::RTM, Features::PCID, Features::RDPID,
                         Features::HWP},
    .leaf0 = {.reg = {0xd, 0x68747541, 0x444d4163, 0x69746e65}},
    .leaf1 = {.reg = {0x670f00, 0x20800, 0x7ed8320b, 0x178bfbff}},
    .leaf4 = {.reg = {0x0, 0x0, 0x0, 0x0}},
    .leaf6 = {.reg = {0x0, 0x0, 0x1, 0x0}},
    .leaf7 = {.reg = {0x0, 0x1a9, 0x0, 0x0}},
    .leaf8_0 = {.reg = {0x8000001e, 0x68747541, 0x444d4163, 0x69746e65}},
    .leaf8_1 = {.reg = {0x670f00, 0x40000000, 0x2fabbfff, 0x2fd3fbff}},
    .leaf8_7 = {.reg = {0x0, 0x5, 0x400, 0x37d9}},
    .leaf8_1D = {.reg = {0x121, 0x1c0003f, 0x3f, 0x0}},
    .leaf8_1E = {.reg = {0x10, 0x100, 0x0, 0x0}},
};

// Queried from Intel Celeron J3455 (NUC6CAYH); Apollo Lake NUC (Goldmont)
const TestDataSet kTestDataCeleronJ3455 = {
    .features = {},
    .missing_features = {},
    .leaf0 = {.reg = {0x15, 0x756e6547, 0x6c65746e, 0x49656e69}},
    .leaf1 = {.reg = {0x506c9, 0x2200800, 0x4ff8ebbf, 0xbfebfbff}},
    .leaf4 = {.reg = {0x3c000121, 0x140003f, 0x3f, 0x1}},
    .leaf6 = {},
    .leaf7 = {.reg = {0x0, 0x2294e283, 0x0, 0x2c000000}},
    .leaf8_0 = {.reg = {0x80000008, 0x0, 0x0, 0x0}},
    .leaf8_1 = {.reg = {0x0, 0x0, 0x101, 0x2c100800}},
    .leaf8_7 = {},
    .leaf8_1D = {.reg = {0x3, 0xea, 0x124f800, 0x0}},
    .leaf8_1E = {.reg = {0x3, 0xea, 0x124f800, 0x0}},
};

class FakeCpuId : public CpuId {
 public:
  FakeCpuId(const TestDataSet& data) : data_(data) {}

  ManufacturerInfo ReadManufacturerInfo() const override {
    return ManufacturerInfo(data_.leaf0, data_.leaf8_0);
  }

  ProcessorId ReadProcessorId() const override { return ProcessorId(data_.leaf1); }

  Features ReadFeatures() const override {
    return Features(data_.leaf1, data_.leaf6, data_.leaf7, data_.leaf8_1, data_.leaf8_7);
  }

 private:
  const TestDataSet& data_;
};

const FakeCpuId kCpuIdCorei5_6260U(kTestDataCorei5_6260U);
const FakeCpuId kCpuIdXeon2690v4(kTestDataXeon2690v4);
const FakeCpuId kCpuIdThreadRipper2970wx(kTestDataThreadRipper2970wx);
const FakeCpuId kCpuIdAmdA49120C(kTestDataAmdA49120C);
const FakeCpuId kCpuIdCeleronJ3455(kTestDataCeleronJ3455);

}  // namespace cpu_id

#endif  // ZIRCON_KERNEL_ARCH_X86_CPUID_INCLUDE_ARCH_X86_CPUID_TEST_DATA_H_
