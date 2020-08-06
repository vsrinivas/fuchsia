// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for the time being it's only a template copy from vim3

#ifndef SRC_DEVICES_BOARD_DRIVERS_RPI4_RPI4_H_
#define SRC_DEVICES_BOARD_DRIVERS_RPI4_RPI4_H_

#include <threads.h>
#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/iommu.h>
#include <ddktl/protocol/platform/bus.h>

namespace rpi4 {

// BTI IDs for our devices
enum {
  BTI_EMMC,
  BTI_ETHERNET,
  BTI_SD,
  BTI_SDIO,
  BTI_SYSMEM,
  BTI_NNA,
};

class Rpi4;
using Rpi4Type = ddk::Device<Rpi4, ddk::Initializable>;

// This is the main class for the platform bus driver.
class Rpi4 : public Rpi4Type {
 public:
  Rpi4(zx_device_t* parent, pbus_protocol_t* pbus, iommu_protocol_t* iommu)
      : Rpi4Type(parent), pbus_(pbus), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() {}

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Rpi4);

  zx_status_t ClkInit();
  zx_status_t EmmcInit();
  zx_status_t EthInit();
  zx_status_t GpioInit();
  zx_status_t I2cInit();
  zx_status_t SdInit();
  zx_status_t SdioInit();
  zx_status_t Start();
  zx_status_t SysmemInit();
  zx_status_t NnaInit();

  int Thread();

  ddk::PBusProtocolClient pbus_;
  std::optional<ddk::InitTxn> init_txn_;
  ddk::IommuProtocolClient iommu_;
  ddk::GpioImplProtocolClient gpio_impl_;
  ddk::ClockImplProtocolClient clk_impl_;
  thrd_t thread_;
};

}  // namespace rpi4

#endif  // SRC_DEVICES_BOARD_DRIVERS_RPI4_RPI4_H_
