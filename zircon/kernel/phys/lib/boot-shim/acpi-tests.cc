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

using TestShim = boot_shim::BootShim<boot_shim::AcpiUartItem>;

template <typename T>
void AcpiUartTest(const acpi_lite::AcpiParserInterface* parser, const T& expected_uart) {
  boot_shim::testing::TestHelper test;
  TestShim shim("AcpiUartTest", test.log());

  if (parser) {
    shim.Get<boot_shim::AcpiUartItem>().Init(*parser);
  }

  auto [buffer, owner] = test.GetZbiBuffer();
  TestShim::DataZbi zbi(buffer);
  ASSERT_TRUE(zbi.clear().is_ok());

  auto result = shim.AppendItems(zbi);
  ASSERT_TRUE(result.is_ok());

  size_t uart_payload_count = 0;
  zbitl::ByteView uart_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_KERNEL_DRIVER) {
      ++uart_payload_count;
      uart_payload = payload;
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  if constexpr (std::is_same_v<T, std::monostate>) {
    EXPECT_EQ(0, uart_payload_count);
  } else {
    EXPECT_EQ(1, uart_payload_count);
    EXPECT_EQ(uart_payload.size(), sizeof(expected_uart));
    EXPECT_BYTES_EQ(uart_payload.data(), &expected_uart, sizeof(expected_uart));
  }
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

}  // namespace
