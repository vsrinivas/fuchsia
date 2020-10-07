// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/zx/status.h>
#include <string.h>

#include <memory>

#include <gtest/gtest.h>

#include "test_data.h"
#include "test_util.h"

namespace acpi_lite {
namespace {

TEST(AcpiParser, NoRsdp) {
  NullPhysMemReader reader;
  zx::status<AcpiParser> result = AcpiParser::Init(reader, 0);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_NOT_FOUND);
}

TEST(AcpiParser, EmptyTables) {
  EmptyPhysMemReader reader;
  zx::status<AcpiParser> result = AcpiParser::Init(reader, 0);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_NOT_FOUND);
}

TEST(AcpiParser, ParseQemuTables) {
  FakePhysMemReader reader(&kQemuTables);
  AcpiParser result = AcpiParser::Init(reader, kQemuTables.rsdp).value();
  ASSERT_EQ(4u, result.num_tables());

  // Ensure we can read the HPET table.
  const AcpiSdtHeader* hpet_table = result.GetTableBySignature(AcpiSignature("HPET"));
  ASSERT_TRUE(hpet_table != nullptr);
  EXPECT_TRUE(memcmp(hpet_table, "HPET", 4) == 0);
}

TEST(AcpiParser, ParseIntelNucTables) {
  // Parse the QEMU tables.
  FakePhysMemReader reader(&kIntelNuc7i5dnTables);
  AcpiParser result = AcpiParser::Init(reader, kIntelNuc7i5dnTables.rsdp).value();
  EXPECT_EQ(28u, result.num_tables());
}

TEST(AcpiParser, ParseFuchsiaHypervisor) {
  FakePhysMemReader reader(&kFuchsiaHypervisor);
  AcpiParser result = AcpiParser::Init(reader, kFuchsiaHypervisor.rsdp).value();
  EXPECT_EQ(result.num_tables(), 3u);
}

TEST(AcpiParser, ReadMissingTable) {
  // Parse the QEMU tables.
  FakePhysMemReader reader(&kQemuTables);
  AcpiParser result = AcpiParser::Init(reader, kQemuTables.rsdp).value();

  // Read a missing table.
  EXPECT_EQ(result.GetTableBySignature(AcpiSignature("AAAA")), nullptr);

  // Read a bad index.
  EXPECT_EQ(result.GetTableAtIndex(result.num_tables()), nullptr);
  EXPECT_EQ(result.GetTableAtIndex(~0), nullptr);
}

TEST(AcpiParser, DumpTables) {
  // Parse the QEMU tables.
  FakePhysMemReader reader(&kQemuTables);
  zx::status<AcpiParser> result = AcpiParser::Init(reader, kQemuTables.rsdp);
  ASSERT_FALSE(result.is_error());

  // Dump the (relatively short) QEMU tables.
  result->DumpTables();
}

// A PhysMemReader that emulates the BIOS read-only area between 0xe'0000 and 0xf'ffff.
class BiosAreaPhysMemReader : public PhysMemReader {
 public:
  explicit BiosAreaPhysMemReader(const AcpiTableSet* tables) : fallback_(tables) {
    // Create a fake BIOS area.
    bios_area_ = std::make_unique<uint8_t[]>(kBiosReadOnlyAreaLength);

    // Copy any tables into the fake BIOS area.
    for (const auto& table : tables->tables) {
      if (table.phys_addr >= kBiosReadOnlyAreaStart && table.phys_addr < kBiosReadOnlyAreaEnd) {
        memcpy(bios_area_.get() + table.phys_addr - kBiosReadOnlyAreaStart, table.data.data(),
               std::min(table.data.size_bytes(), kBiosReadOnlyAreaEnd - table.phys_addr));
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
  std::unique_ptr<uint8_t[]> bios_area_;
  FakePhysMemReader fallback_;
};

TEST(AcpiParser, AcpiSignatureConstruct) {
  AcpiSignature sig("ABCD");

  // Ensure the in-memory representation is correct.
  EXPECT_TRUE(memcmp(&sig, "ABCD", 4) == 0);
}

TEST(AcpiParser, AcpiSignatureWriteToBuffer) {
  // Write out the signature.
  AcpiSignature sig("ABCD");
  char buff[5];
  sig.WriteToBuffer(buff);
  EXPECT_TRUE(strcmp("ABCD", buff) == 0);
}

// Test auto-detection of the location of the RSD PTR by searching the read-only BIOS
// aread.
#if defined(__x86_64__)
TEST(AcpiParser, RsdPtrAutodetect) {
  BiosAreaPhysMemReader reader(&kQemuTables);
  zx::status<AcpiParser> result = AcpiParser::Init(reader, /*rsdp_pa=*/0);
  ASSERT_TRUE(!result.is_error());
  EXPECT_EQ(4u, result->num_tables());
}
#endif

}  // namespace
}  // namespace acpi_lite
