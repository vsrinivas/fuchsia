// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/cpuid.h>
#include <libgen.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <filesystem>
#include <string>
#include <string_view>

#include <rapidjson/document.h>
#include <zxtest/zxtest.h>

#include "src/lib/files/file.h"

// This file is meant for testing lib/arch logic that deals in CpuidIo access,
// along with expressing expectations of the accessed values for the suite of
// particular processors included in the CPUID corpus (see
// //zircon/kernel/lib/arch/test/data/cpuid/README.md). Expectations on the
// full cross-product of (CpuidIo logic, corpus entry) should be found below.

namespace {

using namespace std::string_view_literals;

constexpr std::string_view kTestDataDir = "testdata/cpuid";

//
// Helpers.
//

// TODO(joshuaseaton): make this a shared utility available to all host-side
// tests.
std::string GetTestDataPath(std::string_view filename) {
  std::filesystem::path path;
#if defined(__linux__)
  char self_path[PATH_MAX];
  const char* bin_dir = dirname(realpath("/proc/self/exe", self_path));
  path.append(bin_dir).append(kTestDataDir);
#elif defined(__APPLE__)
  uint32_t length = PATH_MAX;
  char self_path[length];
  char self_path_symlink[length];
  _NSGetExecutablePath(self_path_symlink, &length);
  const char* bin_dir = dirname(realpath(self_path_symlink, self_path));
  path.append(bin_dir).append(kTestDataDir);
#else
#error unknown platform.
#endif
  path.append(filename);
  return path.string();
}

// Populates a FakeCpuidIo from the raw format of an entry in the CPUID corpus.
void PopulateFromFile(std::string_view filename, arch::testing::FakeCpuidIo* cpuid) {
  std::string path = GetTestDataPath(filename);
  std::string contents;
  ASSERT_TRUE(files::ReadFileToString(path, &contents));
  rapidjson::Document document;
  document.Parse(contents);
  ASSERT_FALSE(document.HasParseError());
  ASSERT_TRUE(document.IsArray());

  bool hypervisor_leaves = false;  // Whether such leaves are present.
  for (rapidjson::SizeType i = 0; i < document.Size(); ++i) {
    const auto& entry = document[i];
    ASSERT_TRUE(entry.IsObject());
    ASSERT_TRUE(entry["leaf"].IsUint());
    ASSERT_TRUE(entry["subleaf"].IsUint());
    ASSERT_TRUE(entry["eax"].IsUint());
    ASSERT_TRUE(entry["ebx"].IsUint());
    ASSERT_TRUE(entry["ecx"].IsUint());
    ASSERT_TRUE(entry["edx"].IsUint());
    uint32_t leaf = entry["leaf"].GetUint();
    uint32_t subleaf = entry["subleaf"].GetUint();
    cpuid->Populate(leaf, subleaf, arch::CpuidIo::kEax, entry["eax"].GetUint())
        .Populate(leaf, subleaf, arch::CpuidIo::kEbx, entry["ebx"].GetUint())
        .Populate(leaf, subleaf, arch::CpuidIo::kEcx, entry["ecx"].GetUint())
        .Populate(leaf, subleaf, arch::CpuidIo::kEdx, entry["edx"].GetUint());

    hypervisor_leaves = hypervisor_leaves || (0x4000'0000 <= leaf && leaf < 0x8000'0000);
  }

  // Generally, the zeroth hypervisor leaf is safe to compute, even if no
  // hypervisor is actually present. If no hypervisor leaves are present,
  // we stub in one with garbage values, which ensures that we are doing
  // proper feature testing, e.g., in creating an arch::HypervisorName.
  if (!hypervisor_leaves) {
    cpuid->Populate(0x4000'0000, 0x0, arch::CpuidIo::kEax, 0xaaaa'aaaa)
        .Populate(0x4000'0000, 0x0, arch::CpuidIo::kEbx, 0xbbbb'bbbb)
        .Populate(0x4000'0000, 0x0, arch::CpuidIo::kEcx, 0xcccc'cccc)
        .Populate(0x4000'0000, 0x0, arch::CpuidIo::kEdx, 0xdddd'dddd);
  }
}

//
// Tests.
//

TEST(CpuidTests, Core2_6300) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("core2-6300.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelCore2, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x0f, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM)2 CPU          6300  @ 1.86GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }
}

TEST(CpuidTests, Nehalem_Xeon_E5520) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("nehalem-xeon-e5520.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelNehalem, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x1a, info.model());
  EXPECT_EQ(0x05, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Xeon(R) CPU           E5520  @ 2.27GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }
}

TEST(CpuidTests, SandyBridge_i7_2600K) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("sandy-bridge-i7-2600k.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSandyBridge, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x2a, info.model());
  EXPECT_EQ(0x07, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "       Intel(R) Core(TM) i7-2600K CPU @ 3.40GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }
}

TEST(CpuidTests, IvyBridge_i3_3240) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("ivy-bridge-i3-3240.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelIvyBridge, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x3a, info.model());
  EXPECT_EQ(0x09, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "        Intel(R) Core(TM) i3-3240 CPU @ 3.40GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }
}

TEST(CpuidTests, Haswell_Xeon_E5_2690v3) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("haswell-xeon-e5-2690v3.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelHaswell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x3f, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Xeon(R) CPU E5-2690 v3 @ 2.60GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.x2apic());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present.
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }
}

TEST(CpuidTests, Skylake_i3_6100) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("skylake-i3-6100.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4e, info.model());
  EXPECT_EQ(0x03, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) i3-6100U CPU @ 2.30GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.x2apic());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }
}

TEST(CpuidTests, AtomD510) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("atom-d510.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelBonnell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x1c, info.model());
  EXPECT_EQ(0x0a, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "         Intel(R) Atom(TM) CPU D510   @ 1.66GHz"sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }
}

TEST(CpuidTests, Ryzen2700X) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("ryzen-2700x.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x08, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 7 2700X Eight-Core Processor         "sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }
}

TEST(CpuidTests, Ryzen3950X) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("ryzen-3950x.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }
}

TEST(CpuidTests, Threadripper1950X) {
  arch::testing::FakeCpuidIo cpuid;
  ASSERT_NO_FATAL_FAILURES(PopulateFromFile("threadripper-1950x.json", &cpuid));

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x01, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen Threadripper 1950X 16-Core Processor "sv);

  EXPECT_TRUE(arch::HypervisorName(cpuid).name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }
}

}  // namespace
