// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/board/cpp/banjo.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <threads.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/macros.h>

namespace eth {

class AmlEthernet;
using DeviceType = ddk::Device<AmlEthernet>;

class AmlEthernet : public DeviceType,
                    public ddk::EthBoardProtocol<AmlEthernet, ddk::base_protocol> {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlEthernet);

  explicit AmlEthernet(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // DDK Hooks.
  void DdkRelease();

  // ETH_BOARD protocol.
  zx_status_t EthBoardResetPhy();

 private:
  // GPIO Indexes.
  enum {
    PHY_RESET,
    PHY_INTR,
    GPIO_COUNT,
  };

  // MMIO Indexes
  enum {
    MMIO_PERIPH,
    MMIO_HHI,
  };

  zx_status_t InitPdev();
  zx_status_t Bind();

  ddk::PDev pdev_;
  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient gpios_[GPIO_COUNT];

  std::optional<fdf::MmioBuffer> periph_mmio_;
  std::optional<fdf::MmioBuffer> hhi_mmio_;
};

}  // namespace eth
