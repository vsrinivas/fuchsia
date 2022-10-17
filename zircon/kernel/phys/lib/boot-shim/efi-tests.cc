// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/efi.h>
#include <lib/boot-shim/test-helper.h>
#include <zircon/boot/image.h>

#include <efi/boot-services.h>
#include <efi/system-table.h>
#include <zxtest/zxtest.h>

namespace {

template <class Item, uint32_t Type,
          size_t BufferSize = boot_shim::testing::TestHelper::kDefaultBufferSize, typename T,
          typename... Args>
void EfiTest(const T& expected_payload, Args&&... args) {
  using TestShim = boot_shim::BootShim<Item>;

  boot_shim::testing::TestHelper test;
  TestShim shim(__func__, test.log());

  if constexpr (sizeof...(Args) > 0) {
    auto init = [&]() { return shim.template Get<Item>().Init(std::forward<Args>(args)...); };
    if constexpr (std::is_void_v<decltype(init())>) {
      init();
    } else {
      auto result = init();
      EXPECT_TRUE(result.is_ok(), "EFI error %#zx", result.error_value());
    }
  }

  auto [buffer, owner] = test.GetZbiBuffer(BufferSize);
  typename TestShim::DataZbi zbi(buffer);
  ASSERT_TRUE(zbi.clear().is_ok());

  auto result = shim.AppendItems(zbi);
  ASSERT_TRUE(result.is_ok());

  size_t found_payload_count = 0;
  zbitl::ByteView found_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == Type) {
      ++found_payload_count;
      found_payload = payload;
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  if constexpr (std::is_same_v<T, std::monostate>) {
    EXPECT_EQ(0, found_payload_count);
  } else {
    EXPECT_EQ(1, found_payload_count);
    EXPECT_EQ(found_payload.size(), sizeof(expected_payload));
    EXPECT_BYTES_EQ(found_payload.data(), &expected_payload, sizeof(expected_payload));
  }
}

TEST(BootShimTests, EfiSystemTableNone) {
  ASSERT_NO_FATAL_FAILURE(
      (EfiTest<boot_shim::EfiSystemTableItem, ZBI_TYPE_EFI_SYSTEM_TABLE>(std::monostate{})));
}

TEST(BootShimTests, EfiSystemTable) {
  constexpr efi_system_table kTable = {};
  const uint64_t expected_payload = reinterpret_cast<uintptr_t>(&kTable);
  ASSERT_NO_FATAL_FAILURE((EfiTest<boot_shim::EfiSystemTableItem, ZBI_TYPE_EFI_SYSTEM_TABLE>(
      expected_payload, &kTable)));
}

TEST(BootShimTests, EfiGetVendorTable) {
  constexpr efi_guid kTestGuid = {1, 2, 3, {4, 5, 6, 7, 8}};
  static constexpr std::string_view kFakeTable = "VendorPrefix<data here>";
  static constexpr efi_configuration_table kConfigTable[] = {
      {.VendorGuid = kTestGuid, .VendorTable = kFakeTable.data()}};
  static constexpr efi_system_table kSystemTable = {
      .NumberOfTableEntries = std::size(kConfigTable),
      .ConfigurationTable = kConfigTable,
  };

  EXPECT_NULL(boot_shim::EfiGetVendorTable(&kSystemTable, {}));

  EXPECT_EQ(boot_shim::EfiGetVendorTable(&kSystemTable, kTestGuid, "VendorPrefix"),
            kFakeTable.data());
}

TEST(BootShimTests, EfiSmbiosNone) {
  ASSERT_NO_FATAL_FAILURE((EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(std::monostate{})));
}

TEST(BootShimTests, EfiSmbios) {
  static constexpr std::string_view kFakeSmbios = "_SM_<real data here>";
  static constexpr efi_configuration_table kConfigTable[] = {
      {.VendorGuid = SMBIOS_TABLE_GUID, .VendorTable = kFakeSmbios.data()},
  };
  static constexpr efi_system_table kSystemTable = {
      .NumberOfTableEntries = std::size(kConfigTable),
      .ConfigurationTable = kConfigTable,
  };
  const uint64_t expected_payload = reinterpret_cast<uintptr_t>(kFakeSmbios.data());
  ASSERT_NO_FATAL_FAILURE(
      (EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(expected_payload, &kSystemTable)));
}

TEST(BootShimTests, EfiSmbiosV3) {
  static constexpr std::string_view kFakeSmbios = "_SM3_<real data here>";
  static constexpr efi_configuration_table kConfigTable[] = {
      {.VendorGuid = SMBIOS3_TABLE_GUID, .VendorTable = kFakeSmbios.data()},
  };
  static constexpr efi_system_table kSystemTable = {
      .NumberOfTableEntries = std::size(kConfigTable),
      .ConfigurationTable = kConfigTable,
  };
  const uint64_t expected_payload = reinterpret_cast<uintptr_t>(kFakeSmbios.data());
  ASSERT_NO_FATAL_FAILURE(
      (EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(expected_payload, &kSystemTable)));
}

// Mock up some ACPI tables in memory.  This is sample ACPI data from an Intel
// NUC 7i5DN, borrowed from acpi_lite/test_data.cc; but it has to be fixed up
// to point to itself.

class MockAcpiTables {
 public:
  MockAcpiTables(const MockAcpiTables&) = delete;

  MockAcpiTables() {
    static_assert(sizeof(kNucRsdp) == sizeof(rsdp_));
    memcpy(&rsdp_, kNucRsdp, sizeof(rsdp_));
    rsdp_.xsdt_address = reinterpret_cast<uintptr_t>(kNucXsdt);
    rsdp_.extended_checksum -= Checksum({
        reinterpret_cast<const uint8_t*>(&rsdp_),
        sizeof(rsdp_),
    });
  }

 private:
  acpi_lite::AcpiRsdpV2 rsdp_;

  static uint8_t Checksum(cpp20::span<const uint8_t> bytes) {
    uint8_t sum = 0;
    for (uint8_t byte : bytes) {
      sum = static_cast<uint8_t>(sum + byte);
    }
    return sum;
  }

  static constexpr uint8_t kNucRsdp[] = {
      0x52, 0x53, 0x44, 0x20, 0x50, 0x54, 0x52, 0x20, 0x8a, 0x49, 0x4e, 0x54,
      0x45, 0x4c, 0x00, 0x02, 0x28, 0x90, 0xa2, 0x7f, 0x24, 0x00, 0x00, 0x00,
      0xc0, 0x90, 0xa2, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x6b, 0x00, 0x00, 0x00,
  };

  static constexpr uint8_t kNucXsdt[] = {
      0x58, 0x53, 0x44, 0x54, 0x04, 0x01, 0x00, 0x00, 0x01, 0xc8, 0x49, 0x4e, 0x54, 0x45, 0x4c,
      0x20, 0x4e, 0x55, 0x43, 0x37, 0x69, 0x35, 0x44, 0x4e, 0x1f, 0x00, 0x00, 0x00, 0x41, 0x4d,
      0x49, 0x20, 0x13, 0x00, 0x01, 0x00, 0x30, 0x31, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x48,
      0x32, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x32, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00,
      0x18, 0x33, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x33, 0xa5, 0x7f, 0x00, 0x00, 0x00,
      0x00, 0xf8, 0x33, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x58, 0x37, 0xa5, 0x7f, 0x00, 0x00,
      0x00, 0x00, 0xf0, 0x38, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x48, 0x6a, 0xa5, 0x7f, 0x00,
      0x00, 0x00, 0x00, 0x80, 0x6a, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xa8, 0x6a, 0xa5, 0x7f,
      0x00, 0x00, 0x00, 0x00, 0x38, 0xbd, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xce, 0xa5,
      0x7f, 0x00, 0x00, 0x00, 0x00, 0x08, 0xd6, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x50, 0xd6,
      0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x98,
      0xee, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xef, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00,
      0x80, 0xf2, 0xa5, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x88, 0x22, 0xa6, 0x7f, 0x00, 0x00, 0x00,
      0x00, 0xa0, 0x27, 0xa6, 0x7f, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x27, 0xa6, 0x7f, 0x00, 0x00,
      0x00, 0x00, 0x30, 0x28, 0xa6, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3b, 0xa6, 0x7f, 0x00,
      0x00, 0x00, 0x00, 0x08, 0x3c, 0xa6, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x40, 0x3c, 0xa6, 0x7f,
      0x00, 0x00, 0x00, 0x00, 0x78, 0x3c, 0xa6, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3d, 0xa6,
      0x7f, 0x00, 0x00, 0x00, 0x00,
  };
};

TEST(BootShimTests, EfiGetAcpi) {
  constexpr efi_system_table kEmptySystemTable = {};
  zx::result<acpi_lite::AcpiParser> result = boot_shim::EfiGetAcpi(&kEmptySystemTable);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.status_value(), ZX_ERR_NOT_FOUND);

  const MockAcpiTables kAcpiTables;
  const uint64_t kRsdpPa = reinterpret_cast<uintptr_t>(&kAcpiTables);

  const efi_configuration_table kConfigTable[] = {
      {.VendorGuid = ACPI_TABLE_GUID, .VendorTable = &kAcpiTables},
  };
  const efi_system_table kSystemTable = {
      .NumberOfTableEntries = std::size(kConfigTable),
      .ConfigurationTable = kConfigTable,
  };
  result = boot_shim::EfiGetAcpi(&kSystemTable);
  ASSERT_TRUE(result.is_ok(), "status %d", result.status_value());
  EXPECT_EQ(result->rsdp_pa(), kRsdpPa);

  const efi_configuration_table kConfigTable2[] = {
      {.VendorGuid = ACPI_20_TABLE_GUID, .VendorTable = &kAcpiTables},
  };
  const efi_system_table kSystemTable2 = {
      .NumberOfTableEntries = std::size(kConfigTable2),
      .ConfigurationTable = kConfigTable2,
  };
  result = boot_shim::EfiGetAcpi(&kSystemTable2);
  ASSERT_TRUE(result.is_ok(), "status %d", result.status_value());
  EXPECT_EQ(result->rsdp_pa(), kRsdpPa);
}
}  // namespace
