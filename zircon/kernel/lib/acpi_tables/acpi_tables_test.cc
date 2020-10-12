// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_tables.h>
#include <lib/acpi_tables_test_data.h>
#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>

bool test_cpus_eve() {
  BEGIN_TEST;
  AcpiTables tables(&acpi_test_data::kEveTableProvider);

  uint32_t cpus = 0;
  EXPECT_EQ(ZX_OK, tables.cpu_count(&cpus));
  EXPECT_EQ(4u, cpus);

  uint32_t ids[4] = {0};
  EXPECT_EQ(ZX_OK, tables.cpu_apic_ids(ids, 4, &cpus));
  ASSERT_EQ(4u, cpus);
  EXPECT_EQ(0u, ids[0]);
  EXPECT_EQ(1u, ids[1]);
  EXPECT_EQ(2u, ids[2]);
  EXPECT_EQ(3u, ids[3]);

  END_TEST;
}

bool test_cpus_z840() {
  BEGIN_TEST;
  AcpiTables tables(&acpi_test_data::kZ840TableProvider);

  // We check the numcpus to ensure basic parsing is working.
  uint32_t cpus = 0;
  EXPECT_EQ(ZX_OK, tables.cpu_count(&cpus));
  EXPECT_EQ(56u, cpus);

  END_TEST;
}

bool test_io() {
  BEGIN_TEST;
  AcpiTables tables(&acpi_test_data::kEveTableProvider);

  uint32_t num_io = 0;
  EXPECT_EQ(ZX_OK, tables.io_apic_count(&num_io));
  EXPECT_EQ(1u, num_io);

  io_apic_descriptor io;
  EXPECT_EQ(ZX_OK, tables.io_apics(&io, 1, &num_io));
  ASSERT_EQ(1u, num_io);
  EXPECT_EQ(2u, io.apic_id);
  EXPECT_EQ(0u, io.global_irq_base);
  EXPECT_EQ(0xFEC00000u, io.paddr);

  END_TEST;
}

bool test_interrupt_source_overrides() {
  BEGIN_TEST;
  AcpiTables tables(&acpi_test_data::kEveTableProvider);

  uint32_t num_overrides = 0;
  EXPECT_EQ(ZX_OK, tables.interrupt_source_overrides_count(&num_overrides));
  EXPECT_EQ(2u, num_overrides);

  io_apic_isa_override overrides[2];
  EXPECT_EQ(ZX_OK, tables.interrupt_source_overrides(overrides, 2, &num_overrides));
  ASSERT_EQ(2u, num_overrides);

  EXPECT_EQ(0u, overrides[0].isa_irq);
  EXPECT_EQ(true, overrides[0].remapped);
  EXPECT_EQ(0, overrides[0].tm);
  EXPECT_EQ(0, overrides[0].pol);
  EXPECT_EQ(2u, overrides[0].global_irq);

  EXPECT_EQ(9u, overrides[1].isa_irq);
  EXPECT_EQ(true, overrides[1].remapped);
  EXPECT_EQ(1, overrides[1].tm);
  EXPECT_EQ(0, overrides[1].pol);
  EXPECT_EQ(9u, overrides[1].global_irq);

  END_TEST;
}

UNITTEST_START_TESTCASE(acpi_tables_tests)
UNITTEST("Enumerate cpus using pixelbook eve data", test_cpus_eve)
UNITTEST("Enumerate cpus using HP z840 data.", test_cpus_z840)
UNITTEST("Enumerate io_apic_ids.", test_io)
UNITTEST("Enumerate interrupt_source_overrides.", test_interrupt_source_overrides)
UNITTEST_END_TESTCASE(acpi_tables_tests, "acpi_tables", "Test parsing of acpi tables.")
