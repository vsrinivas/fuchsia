// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/numa.h>
#include <lib/acpi_lite/testing/test_data.h>
#include <lib/acpi_lite/testing/test_util.h>
#include <lib/zx/status.h>
#include <string.h>

#include <initializer_list>
#include <memory>

#include <gtest/gtest.h>

namespace acpi_lite::testing {
namespace {

TEST(Numa, ParseZ840) {
  size_t domain_counts[2] = {0, 0};
  AcpiNumaDomain domains[2];

  EXPECT_EQ(ZX_OK, EnumerateCpuNumaPairs(Z840AcpiParser(),
                                         [&](const AcpiNumaDomain& region, uint32_t apic_id) {
                                           domain_counts[region.domain]++;
                                           domains[region.domain] = region;
                                         }));

  ASSERT_EQ(28u, domain_counts[0]);
  ASSERT_EQ(28u, domain_counts[1]);
  ASSERT_EQ(1u, domains[0].memory_count);
  ASSERT_EQ(1u, domains[1].memory_count);

  EXPECT_EQ(0u, domains[0].memory[0].base_address);
  EXPECT_EQ(0x1030000000u, domains[0].memory[0].length);

  EXPECT_EQ(0x1030000000u, domains[1].memory[0].base_address);
  EXPECT_EQ(0x1000000000u, domains[1].memory[0].length);
}

TEST(Numa, Parse2970wx) {
  size_t domain_counts[4] = {0};
  AcpiNumaDomain domains[4];
  EXPECT_EQ(ZX_OK, EnumerateCpuNumaPairs(Sys2970wxAcpiParser(),
                                         [&](const AcpiNumaDomain& region, uint32_t apic_id) {
                                           domain_counts[region.domain]++;
                                           domains[region.domain] = region;
                                         }));

  ASSERT_EQ(12u, domain_counts[0]);
  ASSERT_EQ(12u, domain_counts[1]);
  ASSERT_EQ(12u, domain_counts[2]);
  ASSERT_EQ(12u, domain_counts[3]);
  ASSERT_EQ(3u, domains[0].memory_count);
  ASSERT_EQ(0u, domains[1].memory_count);
  ASSERT_EQ(1u, domains[2].memory_count);
  ASSERT_EQ(0u, domains[3].memory_count);

  EXPECT_EQ(0x0u, domains[0].memory[0].base_address);
  EXPECT_EQ(0xa0000u, domains[0].memory[0].length);
  EXPECT_EQ(0x100000u, domains[0].memory[1].base_address);
  EXPECT_EQ(0x7ff00000u, domains[0].memory[1].length);
  EXPECT_EQ(0x100000000u, domains[0].memory[2].base_address);
  EXPECT_EQ(0x180000000u, domains[0].memory[2].length);

  EXPECT_EQ(0x280000000u, domains[2].memory[0].base_address);
  EXPECT_EQ(0x200000000u, domains[2].memory[0].length);
}

}  // namespace
}  // namespace acpi_lite::testing
