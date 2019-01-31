// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/acpi_tables.h>

#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/unittest/unittest.h>

namespace {

struct FakeTable {
    char sig[5];
    uint32_t instance = 0xFFFFFF;
    ACPI_TABLE_HEADER* header;
};

class FakeTableProvider : public AcpiTableProvider {
public:
    void AddTable(FakeTable table) {
        tables_[table_count_++] = table;
        DEBUG_ASSERT(table_count_ < kMaxTables);
    }

    ACPI_STATUS GetTable(char* signature, uint32_t instance,
                         ACPI_TABLE_HEADER** header) const override {
        for (uint32_t i = 0; i < table_count_; i++) {
            const FakeTable* table = &tables_[i];
            if (strcmp(signature, table->sig) == 0 && instance == table->instance) {
                *header = table->header;
                return AE_OK;
            }
        }
        return AE_NOT_FOUND;
    }

private:
    static const uint32_t kMaxTables = 3;
    FakeTable tables_[kMaxTables];
    uint32_t table_count_ = 0;
};

// Dumped from pixelbook eve.
const uint8_t kEveMadtTable[]{
    0x41, 0x50, 0x49, 0x43, 0x6C, 0x0, 0x0, 0x0, 0x1, 0x72,
    0x43, 0x4F, 0x52, 0x45, 0x20, 0x20, 0x43, 0x4F, 0x52, 0x45,
    0x42, 0x4F, 0x4F, 0x54, 0x0, 0x0, 0x0, 0x0, 0x43, 0x4F,
    0x52, 0x45, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xE0, 0xFE,
    0x1, 0x0, 0x0, 0x0, 0x0, 0x8, 0x0, 0x0, 0x1, 0x0,
    0x0, 0x0, 0x0, 0x8, 0x1, 0x1, 0x1, 0x0, 0x0, 0x0,
    0x0, 0x8, 0x2, 0x2, 0x1, 0x0, 0x0, 0x0, 0x0, 0x8,
    0x3, 0x3, 0x1, 0x0, 0x0, 0x0, 0x1, 0xC, 0x2, 0x0,
    0x0, 0x0, 0xC0, 0xFE, 0x0, 0x0, 0x0, 0x0, 0x2, 0xA,
    0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0xA,
    0x0, 0x9, 0x9, 0x0, 0x0, 0x0, 0xD, 0x0, 0x71, 0x8A,
};

// Dumped from pixelbook eve.
const uint8_t kEveHpetTable[]{
    0x48, 0x50, 0x45, 0x54, 0x38, 0x0, 0x0, 0x0, 0x1, 0xEB,
    0x43, 0x4F, 0x52, 0x45, 0x20, 0x20, 0x43, 0x4F, 0x52, 0x45,
    0x42, 0x4F, 0x4F, 0x54, 0x0, 0x0, 0x0, 0x0, 0x43, 0x4F,
    0x52, 0x45, 0x0, 0x0, 0x0, 0x0, 0x1, 0xA7, 0x86, 0x80,
    0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0xD0, 0xFE, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x61, 0xCC, 0x68, 0x61,
};

} // namespace

bool test_cpus() {
    BEGIN_TEST;
    FakeTableProvider provider;
    provider.AddTable(FakeTable{ACPI_SIG_MADT, 1, (ACPI_TABLE_HEADER*)kEveMadtTable});
    AcpiTables tables(&provider);

    uint32_t cpus = 0;
    EXPECT_EQ(ZX_OK, tables.cpu_count(&cpus), "");
    EXPECT_EQ(4u, cpus, "");

    uint32_t ids[4] = {0};
    EXPECT_EQ(ZX_OK, tables.cpu_apic_ids(ids, 4, &cpus), "");
    ASSERT_EQ(4u, cpus, "");
    EXPECT_EQ(0u, ids[0], "");
    EXPECT_EQ(1u, ids[1], "");
    EXPECT_EQ(2u, ids[2], "");
    EXPECT_EQ(3u, ids[3], "");

    END_TEST;
}

bool test_io() {
    BEGIN_TEST;
    FakeTableProvider provider;
    provider.AddTable(FakeTable{ACPI_SIG_MADT, 1, (ACPI_TABLE_HEADER*)kEveMadtTable});
    AcpiTables tables(&provider);

    uint32_t num_io = 0;
    EXPECT_EQ(ZX_OK, tables.io_apic_count(&num_io), "");
    EXPECT_EQ(1u, num_io, "");

    io_apic_descriptor io;
    EXPECT_EQ(ZX_OK, tables.io_apics(&io, 1, &num_io), "");
    ASSERT_EQ(1u, num_io, "");
    EXPECT_EQ(2u, io.apic_id, "");
    EXPECT_EQ(0u, io.global_irq_base, "");
    EXPECT_EQ(0xFEC00000u, io.paddr, "");

    END_TEST;
}

bool test_interrupt_source_overrides() {
    BEGIN_TEST;
    FakeTableProvider provider;
    provider.AddTable(FakeTable{ACPI_SIG_MADT, 1, (ACPI_TABLE_HEADER*)kEveMadtTable});
    AcpiTables tables(&provider);

    uint32_t num_overrides = 0;
    EXPECT_EQ(ZX_OK, tables.interrupt_source_overrides_count(&num_overrides), "");
    EXPECT_EQ(2u, num_overrides, "");

    io_apic_isa_override overrides[2];
    EXPECT_EQ(ZX_OK, tables.interrupt_source_overrides(overrides, 2, &num_overrides), "");
    ASSERT_EQ(2u, num_overrides, "");

    EXPECT_EQ(0u, overrides[0].isa_irq, "");
    EXPECT_EQ(true, overrides[0].remapped, "");
    EXPECT_EQ(0, overrides[0].tm, "");
    EXPECT_EQ(0, overrides[0].pol, "");
    EXPECT_EQ(2u, overrides[0].global_irq, "");

    EXPECT_EQ(9u, overrides[1].isa_irq, "");
    EXPECT_EQ(true, overrides[1].remapped, "");
    EXPECT_EQ(1, overrides[1].tm, "");
    EXPECT_EQ(0, overrides[1].pol, "");
    EXPECT_EQ(9u, overrides[1].global_irq, "");

    END_TEST;
}

bool test_hpet() {
    BEGIN_TEST;
    FakeTableProvider provider;
    provider.AddTable(FakeTable{ACPI_SIG_HPET, 1, (ACPI_TABLE_HEADER*)kEveHpetTable});
    AcpiTables tables(&provider);

    acpi_hpet_descriptor hpet;
    ASSERT_EQ(ZX_OK, tables.hpet(&hpet), "");

    EXPECT_EQ(0xFED00000u, hpet.address, "");
    EXPECT_EQ(false, hpet.port_io, "");
    EXPECT_EQ(0, hpet.minimum_tick, "");
    EXPECT_EQ(0, hpet.sequence, "");

    END_TEST;
}

UNITTEST_START_TESTCASE(apic_tables_tests)
UNITTEST("Enumerate cpus.", test_cpus)
UNITTEST("Enumerate io_apic_ids.", test_io)
UNITTEST("Enumerate interrupt_source_overrides.", test_interrupt_source_overrides)
UNITTEST("Lookup HPET.", test_hpet)
UNITTEST_END_TESTCASE(apic_tables_tests, "apic_tables", "Test parsing of apic tables.");
