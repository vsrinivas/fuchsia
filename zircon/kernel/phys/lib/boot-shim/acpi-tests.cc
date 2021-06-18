// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite/testing/test_data.h>
#include <lib/boot-shim/acpi.h>
#include <lib/boot-shim/boot-shim.h>
#include <lib/boot-shim/test-helper.h>

#include <zxtest/zxtest.h>

namespace {

template <class Item, uint32_t Type, class Parser, typename T>
void AcpiTest(const Parser* parser, const T& expected_payload) {
  using TestShim = boot_shim::BootShim<Item>;

  boot_shim::testing::TestHelper test;
  TestShim shim(__func__, test.log());

  if (parser) {
    shim.template Get<Item>().Init(*parser);
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

template <typename T>
void AcpiUartTest(const acpi_lite::AcpiParserInterface* parser, const T& expected_uart) {
  AcpiTest<boot_shim::AcpiUartItem, ZBI_TYPE_KERNEL_DRIVER>(parser, expected_uart);
}

TEST(BootShimTests, AcpiUartNone) {
  ASSERT_NO_FATAL_FAILURES(AcpiUartTest(nullptr, std::monostate{}));
}

TEST(BootShimTests, AcpiUartAtlas) {
  constexpr dcfg_simple_t kAtlasUart = {.mmio_phys = 0xfe03'4000};
  auto parser = acpi_lite::testing::PixelbookAtlasAcpiParser();
  ASSERT_NO_FATAL_FAILURES(AcpiUartTest(&parser, kAtlasUart));
}

TEST(BootShimTests, AcpiUartNuc) {
  constexpr dcfg_simple_pio_t kNucUart = {.base = 0x3f8};
  auto reader = acpi_lite::testing::IntelNuc7i5dnPhysMemReader();
  auto result = acpi_lite::AcpiParser::Init(reader, reader.rsdp());
  ASSERT_TRUE(result.is_ok());
  auto& parser = result.value();
  ASSERT_NO_FATAL_FAILURES(AcpiUartTest(&parser, kNucUart));
}

void AcpiRsdpTest(acpi_lite::testing::FakePhysMemReader mem_reader, uint64_t expected_rsdp) {
  auto result = acpi_lite::AcpiParser::Init(mem_reader, mem_reader.rsdp());
  ASSERT_TRUE(result.is_ok());
  auto& parser = result.value();
  AcpiTest<boot_shim::AcpiRsdpItem, ZBI_TYPE_ACPI_RSDP>(&parser, expected_rsdp);
}

TEST(BootShimTests, AcpiRsdpNone) {
  acpi_lite::AcpiParser* parser = nullptr;
  AcpiTest<boot_shim::AcpiRsdpItem, ZBI_TYPE_ACPI_RSDP>(parser, std::monostate{});
}

TEST(BootShimTests, AcpiRsdpQemu) {
  ASSERT_NO_FATAL_FAILURES(AcpiRsdpTest(acpi_lite::testing::QemuPhysMemReader(), 0xf'5860));
}

TEST(BootShimTests, AcpiRsdpFuchsiaHypervisor) {
  ASSERT_NO_FATAL_FAILURES(
      AcpiRsdpTest(acpi_lite::testing::FuchsiaHypervisorPhysMemReader(), 0xe'0000));
}

TEST(BootShimTests, AcpiRsdpNuc) {
  ASSERT_NO_FATAL_FAILURES(
      AcpiRsdpTest(acpi_lite::testing::IntelNuc7i5dnPhysMemReader(), 0x7fa2'9000));
}

}  // namespace
