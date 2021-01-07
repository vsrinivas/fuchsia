// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_VIM2_VIM_H_
#define SRC_DEVICES_BOARD_DRIVERS_VIM2_VIM_H_

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/iommu/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <threads.h>

#include <ddktl/device.h>
#include <fbl/macros.h>

namespace vim {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB_XHCI,
  BTI_MALI,
  BTI_DISPLAY,
  BTI_VIDEO,
  BTI_AUDIO,
  BTI_EMMC,
  BTI_SDIO,
  BTI_CANVAS,
  BTI_SYSMEM,
  BTI_SD,
  BTI_ETHERNET,
};

class Vim;
using VimType = ddk::Device<Vim>;

// This is the main class for the VIM2 board driver.
class Vim : public VimType {
 public:
  explicit Vim(zx_device_t* parent, pbus_protocol_t* pbus, iommu_protocol_t* iommu)
      : VimType(parent), pbus_(pbus), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Vim);

  zx_status_t Start();
  zx_status_t GpioInit();
  zx_status_t I2cInit();
  zx_status_t MaliInit();
  zx_status_t UartInit();
  zx_status_t UsbInit();
  zx_status_t EmmcInit();
  zx_status_t SdioInit();
  zx_status_t EthInit();
  zx_status_t ThermalInit();
  zx_status_t DisplayInit();
  zx_status_t VideoInit();
  zx_status_t CanvasInit();
  zx_status_t ClkInit();
  zx_status_t EnableWifi32K();
  zx_status_t SysmemInit();
  zx_status_t SdInit();
  zx_status_t RegistersInit();

  int Thread();

  ddk::PBusProtocolClient pbus_;
  ddk::IommuProtocolClient iommu_;
  ddk::GpioImplProtocolClient gpio_impl_;
  thrd_t thread_;
};

}  // namespace vim

#endif  // SRC_DEVICES_BOARD_DRIVERS_VIM2_VIM_H_
