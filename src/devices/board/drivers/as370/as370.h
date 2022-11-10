// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_
#define SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>

namespace board_as370 {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB,
  BTI_AUDIO_DHUB,
  BTI_SDIO0,
  BTI_NNA,
};

class As370 : public ddk::Device<As370> {
 public:
  As370(zx_device_t* parent, fdf::ClientEnd<fuchsia_hardware_platform_bus::PlatformBus> pbus,
        fuchsia_hardware_platform_bus::TemporaryBoardInfo board_info)
      : ddk::Device<As370>(parent), pbus_(std::move(pbus)), board_info_(std::move(board_info)) {}

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
  zx_status_t NnaInit();
  zx_status_t PowerInit();
  zx_status_t RegistersInit();
  zx_status_t SdioInit();
  zx_status_t ThermalInit();
  zx_status_t TouchInit();
  zx_status_t ButtonsInit();

  const fdf::WireSyncClient<fuchsia_hardware_platform_bus::PlatformBus> pbus_;
  const fuchsia_hardware_platform_bus::TemporaryBoardInfo board_info_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace board_as370

#endif  // SRC_DEVICES_BOARD_DRIVERS_AS370_AS370_H_
