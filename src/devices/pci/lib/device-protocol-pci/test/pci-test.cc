// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/device-protocol/pci.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace {

class PciTest : public zxtest::Test {
 public:
  PciTest() : loop_(&kAsyncLoopConfigNeverAttachToThread), bar_id_(0u) {}

  void SetUp() override {
    loop_.StartThread("pci-fidl-server-thread");

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_pci::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    fake_pci_ = std::make_unique<pci::FakePciProtocol>();
    fake_pci_->CreateBar(bar_id_, zx_system_get_page_size(), /*is_mmio=*/true);

    binding_ = fidl::BindServer(
        loop_.dispatcher(),
        fidl::ServerEnd<fuchsia_hardware_pci::Device>(endpoints->server.TakeChannel()),
        std::move(fake_pci_));
    EXPECT_TRUE(binding_.has_value());

    client_ = std::move(endpoints->client);
  }

  void TearDown() override { loop_.Shutdown(); }

  async::Loop loop_;
  std::unique_ptr<pci::FakePciProtocol> fake_pci_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_pci::Device>> binding_;
  fidl::ClientEnd<fuchsia_hardware_pci::Device> client_;
  uint32_t bar_id_;
};

TEST_F(PciTest, MapMmio) {
  ddk::Pci pci(std::move(client_));
  std::optional<fdf::MmioBuffer> mmio;
  EXPECT_OK(pci.MapMmio(bar_id_, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
}

TEST_F(PciTest, MapMmioWithRawBuffer) {
  ddk::Pci pci(std::move(client_));
  mmio_buffer_t mmio;
  EXPECT_OK(pci.MapMmio(bar_id_, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));

  // Make sure the VMO is valid.
  EXPECT_OK(zx_object_get_info(mmio.vmo, ZX_INFO_HANDLE_VALID, nullptr, 0, 0u, nullptr));
}

TEST(PciConversionTest, DeviceInfo) {
  fuchsia_hardware_pci::wire::DeviceInfo fidl_info = {
      .vendor_id = 1,
      .device_id = 2,
      .base_class = 3,
      .sub_class = 4,
      .program_interface = 5,
      .revision_id = 6,
      .bus_id = 7,
      .dev_id = 8,
      .func_id = 9,
  };

  pci_device_info_t banjo_info = ddk::convert_device_info_to_banjo(fidl_info);
  EXPECT_EQ(banjo_info.vendor_id, 1);
  EXPECT_EQ(banjo_info.device_id, 2);
  EXPECT_EQ(banjo_info.base_class, 3);
  EXPECT_EQ(banjo_info.sub_class, 4);
  EXPECT_EQ(banjo_info.program_interface, 5);
  EXPECT_EQ(banjo_info.revision_id, 6);
  EXPECT_EQ(banjo_info.bus_id, 7);
  EXPECT_EQ(banjo_info.dev_id, 8);
  EXPECT_EQ(banjo_info.func_id, 9);
}

TEST(PciConversionTest, InterruptModes) {
  fuchsia_hardware_pci::wire::InterruptModes fidl_modes = {
      .has_legacy = true,
      .msi_count = 0,
      .msix_count = 1,
  };

  pci_interrupt_modes_t banjo_modes = ddk::convert_interrupt_modes_to_banjo(fidl_modes);
  EXPECT_EQ(banjo_modes.has_legacy, true);
  EXPECT_EQ(banjo_modes.msi_count, 0);
  EXPECT_EQ(banjo_modes.msix_count, 1);
}

TEST(PciConversionTest, MmioBar) {
  zx::vmo vmo;
  fuchsia_hardware_pci::wire::Bar fidl_bar = {
      .bar_id = 1,
      .size = 2,
      .result = fuchsia_hardware_pci::wire::BarResult::WithVmo(std::move(vmo)),
  };

  pci_bar_t banjo_bar = ddk::convert_bar_to_banjo(std::move(fidl_bar));
  EXPECT_EQ(banjo_bar.bar_id, 1);
  EXPECT_EQ(banjo_bar.size, 2);
  EXPECT_EQ(banjo_bar.type, PCI_BAR_TYPE_MMIO);
  EXPECT_EQ(banjo_bar.result.vmo, vmo.get());
}

TEST(PciConversionTest, IoBar) {
  fidl::Arena arena;
  zx::resource resource;
  fuchsia_hardware_pci::wire::Bar fidl_bar = {
      .bar_id = 1,
      .size = 2,
      .result = fuchsia_hardware_pci::wire::BarResult::WithIo(
          arena, fuchsia_hardware_pci::wire::IoBar{.address = 3, .resource = std::move(resource)}),
  };

  pci_bar_t banjo_bar = ddk::convert_bar_to_banjo(std::move(fidl_bar));
  EXPECT_EQ(banjo_bar.bar_id, 1);
  EXPECT_EQ(banjo_bar.size, 2);
  EXPECT_EQ(banjo_bar.type, PCI_BAR_TYPE_IO);
  EXPECT_EQ(banjo_bar.result.io.address, 3);
  EXPECT_EQ(banjo_bar.result.io.resource, resource.get());
}

}  // namespace
