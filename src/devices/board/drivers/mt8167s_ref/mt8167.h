// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_MT8167S_REF_MT8167_H_
#define SRC_DEVICES_BOARD_DRIVERS_MT8167S_REF_MT8167_H_

#include <fuchsia/hardware/gpioimpl/c/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/mmio/mmio.h>
#include <threads.h>

#include <ddk/usb-peripheral-config.h>
#include <ddktl/device.h>
#include <fbl/macros.h>
#include <soc/mt8167/mt8167-power.h>

namespace board_mt8167 {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_DISPLAY,
  BTI_MSDC0,
  BTI_MSDC1,
  BTI_MSDC2,
  BTI_USB,
  BTI_AUDIO_OUT,
  BTI_AUDIO_IN,
  BTI_SYSMEM,
  BTI_GPU,
};

class Mt8167;
using Mt8167Type = ddk::Device<Mt8167>;

// This is the main class for the platform bus driver.
class Mt8167 : public Mt8167Type {
 public:
  explicit Mt8167(zx_device_t* parent, pbus_protocol_t* pbus, pdev_board_info_t* board_info)
      : Mt8167Type(parent), pbus_(pbus), board_info_(*board_info) {}
  virtual ~Mt8167() = default;

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

  // Visible for testing.
  int Thread();
  void UpdateRegisters(ddk::MmioBuffer mmio);

 protected:
  explicit Mt8167(zx_device_t* parent) : Mt8167Type(parent) {}

  virtual zx_status_t Vgp1Enable();

  virtual zx_status_t Msdc0Init();
  virtual zx_status_t Msdc2Init();
  virtual zx_status_t SocInit();
  virtual zx_status_t SysmemInit();
  virtual zx_status_t GpioInit();
  virtual zx_status_t GpuInit();
  virtual zx_status_t DisplayInit();
  virtual zx_status_t I2cInit();
  virtual zx_status_t ButtonsInit();
  virtual zx_status_t ClkInit();
  virtual zx_status_t UsbInit();
  virtual zx_status_t ThermalInit();
  virtual zx_status_t TouchInit();
  virtual zx_status_t BacklightInit();
  virtual zx_status_t AudioInit();

  void InitMmPll(ddk::MmioBuffer* clk_mmio, ddk::MmioBuffer* pll_mmio);

  ddk::PBusProtocolClient pbus_;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Mt8167);

  zx_status_t Start();

  zx_status_t PowerInit();

  gpio_impl_protocol_t gpio_impl_;
  pdev_board_info_t board_info_;
  thrd_t thread_;
  UsbConfig* usb_config_;
};

}  // namespace board_mt8167

#endif  // SRC_DEVICES_BOARD_DRIVERS_MT8167S_REF_MT8167_H_
