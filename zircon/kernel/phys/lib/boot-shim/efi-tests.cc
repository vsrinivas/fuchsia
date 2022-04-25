// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/efi.h>
#include <lib/boot-shim/test-helper.h>
#include <zircon/boot/image.h>

#include <efi/system-table.h>
#include <zxtest/zxtest.h>

namespace {

template <class Item, uint32_t Type, typename T>
void EfiTest(const efi_system_table* systab, const T& expected_payload) {
  using TestShim = boot_shim::BootShim<Item>;

  boot_shim::testing::TestHelper test;
  TestShim shim(__func__, test.log());

  if (systab) {
    shim.template Get<Item>().Init(systab);
  }

  auto [buffer, owner] = test.GetZbiBuffer();
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
  ASSERT_NO_FATAL_FAILURE((EfiTest<boot_shim::EfiSystemTableItem, ZBI_TYPE_EFI_SYSTEM_TABLE>(
      nullptr, std::monostate{})));
}

TEST(BootShimTests, EfiSystemTable) {
  constexpr efi_system_table kTable = {};
  const uint64_t expected_payload = reinterpret_cast<uintptr_t>(&kTable);
  ASSERT_NO_FATAL_FAILURE((EfiTest<boot_shim::EfiSystemTableItem, ZBI_TYPE_EFI_SYSTEM_TABLE>(
      &kTable, expected_payload)));
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
  ASSERT_NO_FATAL_FAILURE(
      (EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(nullptr, std::monostate{})));
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
      (EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(&kSystemTable, expected_payload)));
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
      (EfiTest<boot_shim::EfiSmbiosItem, ZBI_TYPE_SMBIOS>(&kSystemTable, expected_payload)));
}

}  // namespace
