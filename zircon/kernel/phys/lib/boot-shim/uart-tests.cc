// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/uart.h>
#include <lib/uart/all.h>
#include <lib/uart/ns8250.h>
#include <lib/uart/null.h>
#include <lib/zbitl/image.h>
#include <zircon/boot/driver-config.h>

#include <array>

#include <zxtest/zxtest.h>

namespace {

using boot_shim::UartItem;

TEST(UartItemTest, SetItemWithNullDriverIsNoOp) {
  std::array<std::byte, 2 * sizeof(zbi_header_t) + ZBI_ALIGN(sizeof(uart::null::Driver().config()))>
      data;
  zbitl::Image<cpp20::span<std::byte>> image(data);
  ASSERT_TRUE(image.clear().is_ok());
  size_t clear_image_size = image.size_bytes();

  UartItem item;
  uart::all::Driver dcfg = uart::null::Driver();
  item.Init(dcfg);

  ASSERT_TRUE(item.AppendItems(image).is_ok());
  EXPECT_EQ(image.size_bytes(), clear_image_size);
  ASSERT_TRUE(image.take_error().is_ok());
}

TEST(UartItemTest, SetItemWithMmioDriver) {
  std::array<std::byte,
             2 * sizeof(zbi_header_t) + ZBI_ALIGN(sizeof(uart::ns8250::MmioDriver().config()))>
      data;
  zbitl::Image<cpp20::span<std::byte>> image(data);
  ASSERT_TRUE(image.clear().is_ok());

  UartItem item;
  auto mmio_dcfg = uart::ns8250::MmioDriver(zbi_dcfg_simple_t{.mmio_phys = 0xBEEF, .irq = 0xDEAD});
  uart::all::Driver dcfg = mmio_dcfg;
  item.Init(dcfg);

  ASSERT_TRUE(item.AppendItems(image).is_ok());

  auto it = image.find(ZBI_TYPE_KERNEL_DRIVER);
  image.ignore_error();
  ASSERT_NE(it, image.end());

  auto& [header, payload] = *it;
  EXPECT_EQ(header->extra, mmio_dcfg.extra());
  ASSERT_EQ(header->length, mmio_dcfg.size());

  auto* actual_mmio_dcfg = reinterpret_cast<zbi_dcfg_simple_t*>(payload.data());
  EXPECT_EQ(actual_mmio_dcfg->mmio_phys, mmio_dcfg.config().mmio_phys);
  EXPECT_EQ(actual_mmio_dcfg->irq, mmio_dcfg.config().irq);
}

#if defined(__x86_64__) || defined(__i386__)
TEST(UartItemTest, SetItemWithPioDriver) {
  std::array<std::byte,
             2 * sizeof(zbi_header_t) + ZBI_ALIGN(sizeof(uart::ns8250::PioDriver().config()))>
      data;
  zbitl::Image<cpp20::span<std::byte>> image(data);
  ASSERT_TRUE(image.clear().is_ok());

  UartItem item;
  auto pio_cfg = uart::ns8250::PioDriver(zbi_dcfg_simple_pio_t{.base = 0xA110, .irq = 0x5A02});
  uart::all::Driver dcfg = pio_cfg;
  item.Init(dcfg);

  ASSERT_TRUE(item.AppendItems(image).is_ok());

  auto it = image.find(ZBI_TYPE_KERNEL_DRIVER);
  image.ignore_error();
  ASSERT_NE(it, image.end());

  auto& [header, payload] = *it;
  EXPECT_EQ(header->extra, pio_cfg.extra());
  ASSERT_EQ(header->length, pio_cfg.size());

  auto* actual_pio_dcfg = reinterpret_cast<zbi_dcfg_simple_pio_t*>(payload.data());
  EXPECT_EQ(actual_pio_dcfg->base, pio_cfg.config().base);
  EXPECT_EQ(actual_pio_dcfg->irq, pio_cfg.config().irq);
}
#endif

}  // namespace
