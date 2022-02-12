// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace {

TEST(PciTest, MapMmio) {
  pci::FakePciProtocol fake_pci;
  uint32_t bar_id = 0u;
  fake_pci.CreateBar(bar_id, zx_system_get_page_size(), /*is_mmio=*/true);

  ddk::Pci pci(fake_pci.get_protocol());
  std::optional<ddk::MmioBuffer> mmio;
  EXPECT_OK(pci.MapMmio(bar_id, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio));
}

}  // namespace
