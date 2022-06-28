// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_
#define SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>

namespace board_as370 {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB,
  BTI_AUDIO_DHUB,
  BTI_SDIO0,
};

class As370 : public ddk::Device<As370> {
 public:
  As370(zx_device_t* parent, const ddk::PBusProtocolClient& pbus,
        const pdev_board_info_t& board_info)
      : ddk::Device<As370>(parent), pbus_(pbus), board_info_(board_info) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

 private:
  zx_status_t Start();
  int Thread();

  zx_status_t GpioInit();
  zx_status_t I2cInit();
  zx_status_t UsbInit();
  zx_status_t AudioInit();
  zx_status_t ClockInit();
  zx_status_t LightInit();
  zx_status_t NandInit();
  zx_status_t PowerInit();
  zx_status_t SdioInit();
  zx_status_t ThermalInit();
  zx_status_t TouchInit();

  const ddk::PBusProtocolClient pbus_;
  const pdev_board_info_t board_info_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_as370

#endif  // SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_
