// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/cpuid.h>

namespace cpu_id {

struct TestDataSet {
    Features::Feature features[200];
    Features::Feature missing_features[200];
    Registers leaf0;
    Registers leaf1;
    Registers leaf4;
    Registers leaf7;
    SubLeaves<Topology::kEaxBSubleaves> leafB;
    Registers leaf80000001;
};

// Queried from a Intel Xeon E5-2690v4.
const TestDataSet kTestDataXeon2690v4 = {
    .features = {Features::FPU, Features::VME, Features::DE, Features::PSE, Features::TSC,
                 Features::MSR, Features::PAE, Features::MCE, Features::CX8, Features::APIC, Features::SEP,
                 Features::MTRR, Features::PGE, Features::MCA, Features::CMOV, Features::PAT,
                 Features::PSE36, Features::ACPI, Features::MMX, Features::FSGSBASE,
                 Features::FXSR, Features::SSE, Features::SSE2, Features::SS, Features::HTT, Features::TM,
                 Features::PBE, Features::SYSCALL, Features::XD, Features::PDPE1GB, Features::RDTSCP,
                 Features::PCLMULQDQ, Features::DTES64, Features::MONITOR, Features::DS_CPL, Features::VMX,
                 Features::SMX, Features::EST, Features::TM2, Features::SSSE3, Features::SDBG,
                 Features::FMA, Features::CX16, Features::XTPR, Features::PDCM, Features::PCID,
                 Features::DCA, Features::SSE4_1, Features::SSE4_2, Features::X2APIC, Features::MOVBE,
                 Features::POPCNT, Features::AES, Features::XSAVE, Features::AVX, Features::F16C,
                 Features::RDRAND, Features::LAHF, Features::BMI1, Features::HLE, Features::AVX2,
                 Features::SMEP, Features::BMI2, Features::ERMS, Features::INVPCID, Features::RTM,
                 Features::RDSEED, Features::ADX, Features::SMAP, Features::INTEL_PT},
    .missing_features = {Features::PSN, Features::AVX512VNNI},
    .leaf0 = {.reg = {0x14, 0x756E6547, 0x6C65746E, 0x49656E69}},
    .leaf1 = {.reg = {0x406F1, 0x12200800, 0x7FFEFBFF, 0xBFEBFBFF}},
    .leaf4 = {.reg = {0x3C07C163, 0x4C0003F, 0x6FFF, 0x6}},
    .leaf7 = {.reg = {0x0, 0x21CBFBB, 0x0, 0x9C000000}},
    .leafB = {{{.reg = {0x1, 0x2, 0x100, 0x28}},
              {.reg = {0x5, 0x1C, 0x201, 0x29}},
              {.reg = {0x0, 0x0, 0x2, 0x38}}}},
    .leaf80000001 = {.reg = {0x0, 0x0, 0x121, 0x2C100800}},
};

class FakeCpuId : public CpuId {
public:
    FakeCpuId(const TestDataSet& data)
        : data_(data) {}

    ManufacturerInfo ReadManufacturerInfo() const override {
        return ManufacturerInfo(data_.leaf0);
    }

    ProcessorId ReadProcessorId() const override {
        return ProcessorId(data_.leaf1);
    }

    Features ReadFeatures() const override {
        return Features(data_.leaf1, data_.leaf7, data_.leaf80000001);
    }

    Topology ReadTopology() const override {
        return Topology(ManufacturerInfo(data_.leaf0),
                        Features(data_.leaf1, data_.leaf7, data_.leaf80000001),
                        data_.leaf4, data_.leafB);
    }

private:
    const TestDataSet& data_;
};

const FakeCpuId kCpuIdXeon2690v4(kTestDataXeon2690v4);

} // namespace cpu_id
