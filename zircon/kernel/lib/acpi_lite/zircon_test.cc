// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/testing/test_data.h>
#include <lib/acpi_lite/testing/test_util.h>
#include <lib/acpi_lite/zircon.h>
#include <lib/unittest/unittest.h>

#include <fbl/vector.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

namespace acpi_lite::testing {
namespace {

// Parse test QEMU tables.
//
// This duplicates a test in the acpi_lite test suite, but we repeat it here as a
// basic check to ensure acpi_lite functions in a kernel context.
bool TestBasicParse() {
  BEGIN_TEST;

  FakePhysMemReader reader = QemuPhysMemReader();
  AcpiParser parser = AcpiParser::Init(reader, reader.rsdp()).value();
  ASSERT_EQ(4u, parser.num_tables());

  // Ensure we can read the HPET table.
  const AcpiSdtHeader* hpet_table = GetTableBySignature(parser, AcpiSignature("HPET"));
  ASSERT_TRUE(hpet_table != nullptr);
  EXPECT_TRUE(memcmp(hpet_table, "HPET", 4) == 0);

  END_TEST;
}

// Parse test QEMU tables.
//
// This duplicates a test in the acpi_lite test suite, but we repeat it here as a
// basic check to ensure acpi_lite functions in a kernel context.
bool TestZirconPhysmemReader() {
  BEGIN_TEST;

  ZirconPhysmemReader reader;

  // Invalid parameters.
  EXPECT_TRUE(reader.PhysToPtr(0, 0).is_error());
  EXPECT_TRUE(reader.PhysToPtr(0, 1).is_error());
  EXPECT_TRUE(reader.PhysToPtr(1, 0).is_error());

  // Given an physical address, expect to be able to see it again.
  static uint64_t test_token = 0xabcd'1234'dead'beef;
  paddr_t paddr = vaddr_to_paddr(&test_token);
  zx::status<const void*> result = reader.PhysToPtr(paddr, sizeof(uint64_t));
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*static_cast<const uint64_t*>(result.value()), test_token);

  // Test basic overflow.
  EXPECT_TRUE(reader.PhysToPtr(UINT64_MAX, 1).is_error());

  // Both the start and end addresses are valid, but we wrap around the address space.
  EXPECT_TRUE(reader.PhysToPtr(paddr + 2, UINT64_MAX).is_error());

  END_TEST;
}

// Parse the tables of whatever system we happen to be running on.
//
// We don't really know what we will find (auto-detection might legitimately fail,
// for example), but we just try and exercise the code.
bool TestParseSystem() {
  BEGIN_TEST;

  zx::status<AcpiParser> result = AcpiParserInit(0);
  if (result.is_ok()) {
    printf("Successfully parsed the current system's tables.\n");
  } else {
    printf("Could not parse the current system's tables: %d\n", result.error_value());
  }

  END_TEST;
}

}  // namespace
}  // namespace acpi_lite::testing

UNITTEST_START_TESTCASE(acpi_lite_tests)
UNITTEST("Basic Parse", acpi_lite::testing::TestBasicParse)
UNITTEST("ZirconPhysmemReader", acpi_lite::testing::TestZirconPhysmemReader)
UNITTEST("ParseSystem", acpi_lite::testing::TestParseSystem)
UNITTEST_END_TESTCASE(acpi_lite_tests, "acpi_lite", "Test ACPI parsing.")
