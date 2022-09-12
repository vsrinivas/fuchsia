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

}  // namespace
