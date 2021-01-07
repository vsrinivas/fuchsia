// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_VS680_EVK_VS680_EVK_H_
#define SRC_DEVICES_BOARD_DRIVERS_VS680_EVK_VS680_EVK_H_

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>

namespace board_vs680_evk {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_EMMC,
  BTI_SDIO,
  BTI_USB,
};

class Vs680Evk : public ddk::Device<Vs680Evk> {
 public:
  Vs680Evk(zx_device_t* parent, const ddk::PBusProtocolClient& pbus,
           const pdev_board_info_t& board_info)
      : ddk::Device<Vs680Evk>(parent), pbus_(pbus), board_info_(board_info) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

 private:
  zx_status_t Start();
  int Thread();

  zx_status_t ClockInit();
  zx_status_t EmmcInit();
  zx_status_t GpioInit();
  zx_status_t ThermalInit();
  zx_status_t UsbInit();
  zx_status_t I2cInit();
  zx_status_t SdioInit();
  zx_status_t PowerInit();
  zx_status_t SpiInit();

  const ddk::PBusProtocolClient pbus_;
  const pdev_board_info_t board_info_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_vs680_evk

#endif  // SRC_DEVICES_BOARD_DRIVERS_VS680_EVK_VS680_EVK_H_
