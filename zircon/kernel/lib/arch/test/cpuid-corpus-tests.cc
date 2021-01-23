// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/testing/x86/fake-cpuid.h>
#include <lib/arch/x86/cache.h>
#include <lib/arch/x86/cpuid.h>

#include <string_view>
#include <vector>

#include <zxtest/zxtest.h>

// This file is meant for testing lib/arch logic that deals in CpuidIo access,
// along with expressing expectations of the accessed values for the suite of
// particular processors included in the CPUID corpus (see
// //zircon/kernel/lib/arch/test/data/cpuid/README.md). Expectations on the
// full cross-product of (CpuidIo logic, corpus entry) should be found below.

namespace {

using namespace std::string_view_literals;

using arch::testing::X86Microprocessor;

void CheckCaches(const arch::testing::FakeCpuidIo& cpuid,
                 const std::vector<arch::CpuCacheLevelInfo>& expected_caches) {
  arch::CpuCacheInfo caches(cpuid);

  ASSERT_EQ(expected_caches.size(), caches.size());

  {
    auto actual = caches.begin();
    auto expected = expected_caches.cbegin();
    while (actual != caches.end() && expected != expected_caches.cend()) {
      EXPECT_EQ(expected->level, actual->level);
      EXPECT_EQ(expected->type, actual->type);
      EXPECT_EQ(expected->size_kb, actual->size_kb);
      EXPECT_EQ(expected->ways_of_associativity, actual->ways_of_associativity);

      ++actual;
      ++expected;
    }
  }

  // And also compare last-level caches.
  {
    const auto& actual_llc = caches.back();
    const auto& expected_llc = expected_caches.back();
    EXPECT_EQ(expected_llc.level, actual_llc.level);
    EXPECT_EQ(expected_llc.type, actual_llc.type);
    EXPECT_EQ(expected_llc.size_kb, actual_llc.size_kb);
    EXPECT_EQ(expected_llc.ways_of_associativity, actual_llc.ways_of_associativity);
  }
}

//
// Tests.
//

TEST(CpuidTests, IntelCore2_6300) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCore2_6300);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelCore2, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x0f, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM)2 CPU          6300  @ 1.86GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 2048,
              .ways_of_associativity = 8,
          },
      }));
}

TEST(CpuidTests, IntelXeonE5520) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5520);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelNehalem, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x1a, info.model());
  EXPECT_EQ(0x05, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Xeon(R) CPU           E5520  @ 2.27GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelCoreI7_2600k) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI7_2600k);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSandyBridge, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x2a, info.model());
  EXPECT_EQ(0x07, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "       Intel(R) Core(TM) i7-2600K CPU @ 3.40GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.x2apic());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelCoreI3_3240) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_3240);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelIvyBridge, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x3a, info.model());
  EXPECT_EQ(0x09, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "        Intel(R) Core(TM) i3-3240 CPU @ 3.40GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 3072,
              .ways_of_associativity = 12,
          },
      }));
}

TEST(CpuidTests, IntelXeonE5_2690_V3) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V3);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelHaswell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x3f, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Xeon(R) CPU E5-2690 v3 @ 2.60GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present.
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 30720,
              .ways_of_associativity = 20,
          },
      }));
}

TEST(CpuidTests, IntelXeonE5_2690_V4) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelXeonE5_2690_V4);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelBroadwell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4f, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present.
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 35840,
              .ways_of_associativity = 20,
          },
      }));
}

TEST(CpuidTests, IntelCoreI3_6100) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI3_6100);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4e, info.model());
  EXPECT_EQ(0x03, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) i3-6100U CPU @ 2.30GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 3072,
              .ways_of_associativity = 12,
          },
      }));
}

TEST(CpuidTests, IntelCoreI5_7300u) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI5_7300u);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x8e, info.model());
  EXPECT_EQ(0x09, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) i5-7300U CPU @ 2.60GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 3072,
              .ways_of_associativity = 12,
          },
      }));
}

TEST(CpuidTests, IntelCoreI7_6500u) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI7_6500u);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4e, info.model());
  EXPECT_EQ(0x03, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) i7-6500U CPU @ 2.50GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 4096,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelCoreI7_6700k) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreI7_6700k);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x5e, info.model());
  EXPECT_EQ(0x03, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) i7-6700K CPU @ 4.00GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelCoreM3_7y30) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCoreM3_7y30);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x8e, info.model());
  EXPECT_EQ(0x09, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Core(TM) m3-7Y30 CPU @ 1.00GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 4096,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelAtom330) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtom330);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelBonnell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x1c, info.model());
  EXPECT_EQ(0x2, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "         Intel(R) Atom(TM) CPU  330   @ 1.60GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 24,
              .ways_of_associativity = 6,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
      }));
}

TEST(CpuidTests, IntelAtomD510) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomD510);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelBonnell, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x1c, info.model());
  EXPECT_EQ(0x0a, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "         Intel(R) Atom(TM) CPU D510   @ 1.66GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

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
    EXPECT_TRUE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 24,
              .ways_of_associativity = 6,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
      }));
}

TEST(CpuidTests, IntelAtomX5_Z8350) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelAtomX5_Z8350);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelAirmont, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4c, info.model());
  EXPECT_EQ(0x4, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "      Intel(R) Atom(TM) x5-Z8350  CPU @ 1.44GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.x2apic());
    EXPECT_TRUE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
    EXPECT_FALSE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 24,
              .ways_of_associativity = 6,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 1024,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, IntelCeleron3855u) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kIntelCeleron3855u);

  EXPECT_EQ(arch::Vendor::kIntel, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kIntelSkylake, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x6, info.family());
  EXPECT_EQ(0x4e, info.model());
  EXPECT_EQ(0x3, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "Intel(R) Celeron(R) CPU 3855U @ 1.60GHz"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.x2apic());
    EXPECT_TRUE(features.pdcm());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.avx());
    EXPECT_TRUE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.intel_pt());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.fsgsbase());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 256,
              .ways_of_associativity = 4,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 2048,
              .ways_of_associativity = 8,
          },
      }));
}

TEST(CpuidTests, AmdA10_7870k) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdA10_7870k);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x15, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x15, info.family());
  EXPECT_EQ(0x38, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD A10-7870K Radeon R7, 12 Compute Cores 4C+8G"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

    // Not present:
    EXPECT_FALSE(features.hypervisor());
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 16,
              .ways_of_associativity = 4,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 96,
              .ways_of_associativity = 3,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 2048,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzen5_1500x) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen5_1500x);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x01, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 5 1500X Quad-Core Processor          "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 64,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzen7_1700) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen7_1700);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x01, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 7 1700 Eight-Core Processor          "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 64,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 16384,  // Total L3 size.
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzen7_2700x) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen7_2700x);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x08, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 7 2700X Eight-Core Processor         "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 64,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzen9_3950x) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950x);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 16384,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzen9_3950xVirtualBoxHyperv) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xVirtualBoxHyperv);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name() == "VBoxVBoxVBox"sv);

  EXPECT_EQ(0x4000'0006, cpuid.Read<arch::CpuidMaximumHypervisorLeaf>().leaf());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.hypervisor());

    // Not present:
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.cmpxchg16b());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
    EXPECT_FALSE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }

  // Topology leaves are reserved, so we expect to only be able to be surface
  // the total L3 size across the package.
  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 4 * 16384,
          },
      }));
}

TEST(CpuidTests, AmdRyzen9_3950xVirtualBoxKvm) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xVirtualBoxKvm);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name() == "KVMKVMKVM"sv);

  EXPECT_EQ(0x4000'0001, cpuid.Read<arch::CpuidMaximumHypervisorLeaf>().leaf());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.hypervisor());

    // Not present:
    EXPECT_FALSE(features.rdrand());
    EXPECT_FALSE(features.avx());
    EXPECT_FALSE(features.osxsave());
    EXPECT_FALSE(features.xsave());
    EXPECT_FALSE(features.cmpxchg16b());
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
    EXPECT_FALSE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_FALSE(features.smap());
    EXPECT_FALSE(features.rdseed());
  }

  // Topology leaves are reserved, so we expect to only be able to be surface
  // the total L3 size across the package.
  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 4 * 16384,
          },
      }));
}

TEST(CpuidTests, AmdRyzen9_3950xVmware) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xVmware);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name() == "VMwareVMware"sv);

  EXPECT_EQ(0x4000'0010, cpuid.Read<arch::CpuidMaximumHypervisorLeaf>().leaf());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.hypervisor());
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.x2apic());

    // Not present:
    EXPECT_FALSE(features.pdcm());
    EXPECT_FALSE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_FALSE(features.intel_pt());
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.fsgsbase());
  }

  // Topology leaves are reserved, so we expect to only be able to be surface
  // the total L3 size across the package.
  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 4 * 16384,
          },
      }));
}

TEST(CpuidTests, AmdRyzen9_3950xWsl2) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzen9_3950xWsl2);

  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x71, info.model());
  EXPECT_EQ(0x00, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen 9 3950X 16-Core Processor            "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name() == "Microsoft Hv"sv);

  EXPECT_EQ(0x4000'000b, cpuid.Read<arch::CpuidMaximumHypervisorLeaf>().leaf());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.hypervisor());
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());

    // Not present:
    EXPECT_FALSE(features.x2apic());
    EXPECT_FALSE(features.pdcm());
    EXPECT_FALSE(features.monitor());
  }

  {
    auto features = cpuid.Read<arch::CpuidExtendedFeatureFlagsB>();

    // Present:
    EXPECT_TRUE(features.rdseed());
    EXPECT_TRUE(features.smap());
    EXPECT_TRUE(features.fsgsbase());

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 16384,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzenThreadripper1950x) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzenThreadripper1950x);
  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x01, info.model());
  EXPECT_EQ(0x01, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen Threadripper 1950X 16-Core Processor "sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 64,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

TEST(CpuidTests, AmdRyzenThreadripper2970wx) {
  arch::testing::FakeCpuidIo cpuid(X86Microprocessor::kAmdRyzenThreadripper2970wx);
  EXPECT_EQ(arch::Vendor::kAmd, arch::GetVendor(cpuid));
  EXPECT_EQ(arch::Microarchitecture::kAmdFamily0x17, arch::GetMicroarchitecture(cpuid));

  auto info = cpuid.Read<arch::CpuidVersionInfo>();
  EXPECT_EQ(0x17, info.family());
  EXPECT_EQ(0x08, info.model());
  EXPECT_EQ(0x02, info.stepping());

  auto processor = arch::ProcessorName(cpuid);
  EXPECT_TRUE(processor.name() == "AMD Ryzen Threadripper 2970WX 24-Core Processor"sv);

  auto hypervisor = arch::HypervisorName(cpuid);
  EXPECT_TRUE(hypervisor.name().empty());

  {
    auto features = cpuid.Read<arch::CpuidFeatureFlagsC>();

    // Present:
    EXPECT_TRUE(features.rdrand());
    EXPECT_TRUE(features.avx());
    EXPECT_TRUE(features.osxsave());
    EXPECT_TRUE(features.xsave());
    EXPECT_TRUE(features.cmpxchg16b());
    EXPECT_TRUE(features.monitor());

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

    // Not present:
    EXPECT_FALSE(features.intel_pt());
  }

  ASSERT_NO_FATAL_FAILURES(CheckCaches(  //
      cpuid,                             //
      {
          {
              .level = 1,
              .type = arch::X86CacheType::kData,
              .size_kb = 32,
              .ways_of_associativity = 8,
          },
          {
              .level = 1,
              .type = arch::X86CacheType::kInstruction,
              .size_kb = 64,
              .ways_of_associativity = 4,
          },
          {
              .level = 2,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 512,
              .ways_of_associativity = 8,
          },
          {
              .level = 3,
              .type = arch::X86CacheType::kUnified,
              .size_kb = 8192,
              .ways_of_associativity = 16,
          },
      }));
}

}  // namespace
