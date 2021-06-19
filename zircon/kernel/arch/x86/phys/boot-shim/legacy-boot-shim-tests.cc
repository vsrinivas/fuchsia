// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/acpi_lite/testing/test_data.h>
#include <lib/boot-shim/test-helper.h>
#include <stdio.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "legacy-boot-shim.h"

namespace {

TEST(X86LegacyBootShimTests, EmptyInfo) {
  LegacyBoot info;

  boot_shim::testing::TestHelper test;
  LegacyBootShim shim("X86LegacyBootShimTests", info, test.log());

  test.ExpectLogLines({
      "X86LegacyBootShimTests: Legacy boot from unknown legacy boot loader.",
      "X86LegacyBootShimTests: No command line from legacy boot loader!",
      "X86LegacyBootShimTests: Missing or empty RAMDISK: No ZBI!",
      "X86LegacyBootShimTests: Error scanning ZBI: container header doesn't fit."
      " Truncated? at offset 0",
  });
}

TEST(X86LegacyBootShimTests, MissingRamdisk) {
  LegacyBoot info;
  info.bootloader = "xyz";
  info.cmdline = "pdq";

  boot_shim::testing::TestHelper test;
  LegacyBootShim shim("X86LegacyBootShimTests", info, test.log());

  test.ExpectLogLines({
      "X86LegacyBootShimTests: Legacy boot from xyz.",
      ": pdq",  // Matches the tail, since "CMDLINE @ [...,)" has addresses.
      "X86LegacyBootShimTests: Missing or empty RAMDISK: No ZBI!",
      "X86LegacyBootShimTests: Error scanning ZBI: container header doesn't fit."
      " Truncated? at offset 0",
  });
}

TEST(X86LegacyBootShimTests, CmdlineItem) {
  LegacyBoot info;
  info.cmdline = "test command line data";

  boot_shim::testing::TestHelper test;
  LegacyBootShim shim("X86LegacyBootShimTests", info, test.log());

  size_t data_budget = shim.size_bytes();
  EXPECT_GE(data_budget, info.cmdline.size() + sizeof(zbi_header_t));

  auto [buffer, owner] = test.GetZbiBuffer();
  LegacyBootShim::DataZbi zbi(buffer);
  ASSERT_TRUE(zbi.clear().is_ok());

  auto result = shim.AppendItems(zbi);
  ASSERT_TRUE(result.is_ok());

  size_t cmdline_payload_count = 0;
  std::string_view cmdline_payload;
  for (auto [header, payload] : zbi) {
    if (header->type == ZBI_TYPE_CMDLINE) {
      ++cmdline_payload_count;
      cmdline_payload = boot_shim::testing::StringPayload(payload);
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  EXPECT_EQ(cmdline_payload_count, 1);

  // The item payload has a NUL terminator appended.
  ASSERT_FALSE(cmdline_payload.empty());
  EXPECT_EQ('\0', cmdline_payload.back());
  cmdline_payload.remove_suffix(1);

  // The shim prepends other synthetic command-line arguments, but the actual
  // legacy boot loader command line contents should always come last.
  EXPECT_GT(cmdline_payload.size(), info.cmdline.size());
  std::string_view cmdline_tail =
      cmdline_payload.substr(cmdline_payload.size() - info.cmdline.size());
  EXPECT_STR_EQ(cmdline_tail.data(), info.cmdline.data(), "CMDLINE |%.*s|",
                static_cast<int>(cmdline_payload.size()), cmdline_payload.data());
}

TEST(X86LegacyBootShimTests, AcpiItems) {
  LegacyBoot info;
  boot_shim::testing::TestHelper test;
  LegacyBootShim shim("X86LegacyBootShimTests", info, test.log());

  constexpr dcfg_simple_pio_t kUart = {.base = 0x3f8};
  constexpr size_t kUartItemSize = sizeof(zbi_header_t) + sizeof(kUart);
  {
    auto mem_reader = acpi_lite::testing::IntelNuc7i5dnPhysMemReader();
    auto result = acpi_lite::AcpiParser::Init(mem_reader, mem_reader.rsdp());
    ASSERT_TRUE(result.is_ok());
    auto& parser = result.value();
    shim.InitAcpi(parser);
  }

  size_t data_budget = shim.size_bytes();
  EXPECT_GE(data_budget, kUartItemSize);

  auto [buffer, owner] = test.GetZbiBuffer();
  LegacyBootShim::DataZbi zbi(buffer);
  ASSERT_TRUE(zbi.clear().is_ok());

  auto result = shim.AppendItems(zbi);
  ASSERT_TRUE(result.is_ok());

  zbitl::ByteView uart_payload;
  for (auto [header, payload] : zbi) {
    switch (header->type) {
      case ZBI_TYPE_KERNEL_DRIVER:
        EXPECT_TRUE(uart_payload.empty(), "too many uart items");
        EXPECT_FALSE(payload.empty());
        uart_payload = payload;
        break;
    }
  }
  EXPECT_TRUE(zbi.take_error().is_ok());

  ASSERT_EQ(sizeof(kUart), uart_payload.size());
  EXPECT_BYTES_EQ(uart_payload.data(), &kUart, sizeof(kUart));
}

}  // namespace
