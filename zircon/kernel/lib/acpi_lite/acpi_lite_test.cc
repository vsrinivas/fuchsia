// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/unittest/unittest.h>
#include <string.h>

#include <fbl/vector.h>
#include <ktl/algorithm.h>
#include <ktl/unique_ptr.h>

#include "test_data.h"

namespace acpi_lite {
namespace {

// Every address just translates to 0.
class NullPhysMemReader : public PhysMemReader {
 public:
  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
};

bool TestNoTables() {
  BEGIN_TEST;
  NullPhysMemReader reader;
  zx::status<AcpiParser> result = AcpiParser::Init(reader, 0);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_NOT_FOUND);
  END_TEST;
}

// Every address translates to a valid but empty page.
class EmptyPhysMemReader : public PhysMemReader {
 public:
  EmptyPhysMemReader() {
    fbl::AllocChecker ac;
    empty_data_ = ktl::make_unique<uint8_t[]>(&ac, ZX_PAGE_SIZE);
    ASSERT(ac.check());
  }

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    if (length >= ZX_PAGE_SIZE) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::success(empty_data_.get());
  }

 private:
  ktl::unique_ptr<uint8_t[]> empty_data_;
};

bool TestEmptyTables() {
  BEGIN_TEST;
  EmptyPhysMemReader reader;
  zx::status<AcpiParser> result = AcpiParser::Init(reader, 0);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_NOT_FOUND);
  END_TEST;
}

class FakePhysMemReader : public PhysMemReader {
 public:
  explicit FakePhysMemReader(const AcpiTableSet* tables) : tables_(tables) {}

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    for (const auto& table : tables_->tables) {
      if (table.phys_addr == phys && length <= table.data.size_bytes()) {
        return zx::success(table.data.data());
      }
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

 private:
  const AcpiTableSet* tables_;
};

bool TestParseQemuTables() {
  BEGIN_TEST;

  FakePhysMemReader reader(&kQemuTables);
  AcpiParser result = AcpiParser::Init(reader, kQemuTables.rsdp).value();
  ASSERT_EQ(4u, result.num_tables());

  // Ensure we can read the HPET table.
  const AcpiSdtHeader* hpet_table = result.GetTableBySignature(AcpiSignature("HPET"));
  ASSERT_TRUE(hpet_table != nullptr);
  EXPECT_TRUE(memcmp(hpet_table, "HPET", 4) == 0);

  END_TEST;
}

bool TestParseIntelNucTables() {
  BEGIN_TEST;

  // Parse the QEMU tables.
  FakePhysMemReader reader(&kIntelNuc7i5dnTables);
  AcpiParser result = AcpiParser::Init(reader, kIntelNuc7i5dnTables.rsdp).value();
  EXPECT_EQ(28u, result.num_tables());

  END_TEST;
}

bool TestParseFuchsiaHypervisor() {
  BEGIN_TEST;

  FakePhysMemReader reader(&kFuchsiaHypervisor);
  AcpiParser result = AcpiParser::Init(reader, kFuchsiaHypervisor.rsdp).value();
  EXPECT_EQ(result.num_tables(), 3u);

  END_TEST;
}

bool TestReadMissingTable() {
  BEGIN_TEST;

  // Parse the QEMU tables.
  FakePhysMemReader reader(&kQemuTables);
  AcpiParser result = AcpiParser::Init(reader, kQemuTables.rsdp).value();

  // Read a missing table.
  EXPECT_EQ(result.GetTableBySignature(AcpiSignature("AAAA")), nullptr);

  // Read a bad index.
  EXPECT_EQ(result.GetTableAtIndex(result.num_tables()), nullptr);
  EXPECT_EQ(result.GetTableAtIndex(~0), nullptr);

  END_TEST;
}

bool TestDumpTables() {
  BEGIN_TEST;

  // Parse the QEMU tables.
  FakePhysMemReader reader(&kQemuTables);
  zx::status<AcpiParser> result = AcpiParser::Init(reader, kQemuTables.rsdp);
  ASSERT_FALSE(result.is_error());

  // Dump the (relatively short) QEMU tables.
  result->DumpTables();

  END_TEST;
}

#if __x86_64__
// A PhysMemReader that emulates the BIOS read-only area between 0xe'0000 and 0xf'ffff.
class BiosAreaPhysMemReader : public PhysMemReader {
 public:
  explicit BiosAreaPhysMemReader(const AcpiTableSet* tables) : fallback_(tables) {
    // Create a fake BIOS area.
    fbl::AllocChecker ac;
    bios_area_ = ktl::make_unique<uint8_t[]>(&ac, kBiosReadOnlyAreaLength);
    ASSERT(ac.check());

    // Copy any tables into the fake BIOS area.
    for (const auto& table : tables->tables) {
      if (table.phys_addr >= kBiosReadOnlyAreaStart && table.phys_addr < kBiosReadOnlyAreaEnd) {
        memcpy(bios_area_.get() + table.phys_addr - kBiosReadOnlyAreaStart, table.data.data(),
               ktl::min(table.data.size_bytes(), kBiosReadOnlyAreaEnd - table.phys_addr));
      }
    }
  }

  zx::status<const void*> PhysToPtr(uintptr_t phys, size_t length) override {
    if (phys >= kBiosReadOnlyAreaStart && phys < kBiosReadOnlyAreaEnd &&
        phys + length <= kBiosReadOnlyAreaEnd) {
      return zx::success(&bios_area_[phys - kBiosReadOnlyAreaStart]);
    }

    return fallback_.PhysToPtr(phys, length);
  }

 private:
  static constexpr zx_paddr_t kBiosReadOnlyAreaEnd =
      kBiosReadOnlyAreaStart + kBiosReadOnlyAreaLength;
  ktl::unique_ptr<uint8_t[]> bios_area_;
  FakePhysMemReader fallback_;
};

// Test auto-detection of the location of the RSD PTR by searching the read-only BIOS
// aread.
bool TestRsdPtrAutodetect() {
  BEGIN_TEST;

  BiosAreaPhysMemReader reader(&kQemuTables);
  zx::status<AcpiParser> result = AcpiParser::Init(reader, /*rsdp_pa=*/0);
  ASSERT_TRUE(!result.is_error());
  EXPECT_EQ(4u, result->num_tables());

  END_TEST;
}
#endif

bool TestAcpiSignatureConstruct() {
  BEGIN_TEST;

  AcpiSignature sig("ABCD");

  // Ensure the in-memory representation is correct.
  EXPECT_TRUE(memcmp(&sig, "ABCD", 4) == 0);

  END_TEST;
}

bool TestAcpiSignatureWriteToBuffer() {
  BEGIN_TEST;

  // Write out the signature.
  AcpiSignature sig("ABCD");
  char buff[5];
  sig.WriteToBuffer(buff);
  EXPECT_TRUE(strcmp("ABCD", buff) == 0);

  END_TEST;
}

}  // namespace
}  // namespace acpi_lite

UNITTEST_START_TESTCASE(acpi_lite_tests)
UNITTEST("No tables", acpi_lite::TestNoTables)
UNITTEST("Empty tables", acpi_lite::TestEmptyTables)
UNITTEST("Parse QEMU ACPI", acpi_lite::TestParseQemuTables)
UNITTEST("Parse Intel NUC ACPI", acpi_lite::TestParseIntelNucTables)
UNITTEST("Parse Fuchsia Hypervisor ACPI", acpi_lite::TestParseFuchsiaHypervisor)
UNITTEST("Read missing tables", acpi_lite::TestReadMissingTable)
UNITTEST("Dump tables", acpi_lite::TestDumpTables)
#if __x86_64__
UNITTEST("RSDP searching", acpi_lite::TestRsdPtrAutodetect)
#endif
UNITTEST("AcpiSignature representation", acpi_lite::TestAcpiSignatureConstruct)
UNITTEST("AcpiSignature WriteToBuffer", acpi_lite::TestAcpiSignatureWriteToBuffer)
UNITTEST_END_TESTCASE(acpi_lite_tests, "acpi_lite", "Test ACPI parsing.")
