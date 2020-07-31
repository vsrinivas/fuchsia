// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_SHERLOCK_SHERLOCK_H_
#define SRC_DEVICES_BOARD_DRIVERS_SHERLOCK_SHERLOCK_H_

#include <threads.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/iommu.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/macros.h>
#include <soc/aml-t931/t931-hw.h>

namespace sherlock {

// BTI IDs for our devices
enum {
  BTI_BOARD,
  BTI_USB,
  BTI_EMMC,
  BTI_SDIO,
  BTI_MALI,
  BTI_CANVAS,
  BTI_VIDEO,
  BTI_ISP,
  BTI_MIPI,
  BTI_GDC,
  BTI_DISPLAY,
  BTI_AUDIO_OUT,
  BTI_AUDIO_IN,
  BTI_SYSMEM,
  BTI_TEE,
  BTI_GE2D,
  BTI_NNA,
  BTI_AML_SECURE_MEM,
  BTI_VIDEO_ENC,
  BTI_HEVC_ENC,
};

// MAC address metadata indices
enum {
  MACADDR_WIFI = 0,
  MACADDR_BLUETOOTH = 1,
};

// These should match the mmio table defined in sherlock-i2c.c
enum {
  SHERLOCK_I2C_A0_0,
  SHERLOCK_I2C_2,
  SHERLOCK_I2C_3,
};

// These should match the mmio table defined in sherlock-spi.c
enum { SHERLOCK_SPICC0, SHERLOCK_SPICC1 };

// From the schematic.
constexpr uint8_t BOARD_REV_P2 = 0x0B;
constexpr uint8_t BOARD_REV_REWORK = 0x0C;
constexpr uint8_t BOARD_REV_P21 = 0x0D;
constexpr uint8_t BOARD_REV_EVT1 = 0x0E;
constexpr uint8_t BOARD_REV_EVT2 = 0x0F;

class Sherlock;
using SherlockType = ddk::Device<Sherlock>;

// This is the main class for the platform bus driver.
class Sherlock : public SherlockType {
 public:
  explicit Sherlock(zx_device_t* parent, pbus_protocol_t* pbus, iommu_protocol_t* iommu)
      : SherlockType(parent), pbus_(pbus), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Sherlock);

  zx_status_t Start();
  zx_status_t SysmemInit();
  zx_status_t GpioInit();
  zx_status_t BoardInit();
  zx_status_t CanvasInit();
  zx_status_t I2cInit();
  zx_status_t SpiInit();
  zx_status_t UsbInit();
  zx_status_t EmmcInit();
  zx_status_t BCM43458LpoClockInit();  // required for BCM43458 wifi/bluetooth chip.
  zx_status_t SdioInit();
  zx_status_t BluetoothInit();
  zx_status_t ClkInit();
  zx_status_t CameraInit();
  zx_status_t MaliInit();
  zx_status_t TeeInit();
  zx_status_t VideoInit();
  zx_status_t VideoEncInit();
  zx_status_t HevcEncInit();
  zx_status_t ButtonsInit();
  zx_status_t DisplayInit();
  zx_status_t AudioInit();
  zx_status_t ThermalInit();
  zx_status_t TouchInit();
  zx_status_t LightInit();
  zx_status_t OtRadioInit();
  zx_status_t BacklightInit();
  zx_status_t NnaInit();
  zx_status_t SecureMemInit();
  zx_status_t PwmInit();
  zx_status_t RamCtlInit();
  zx_status_t CpuInit();
  int Thread();

  zx_status_t EnableWifi32K(void);

  ddk::PBusProtocolClient pbus_;
  ddk::IommuProtocolClient iommu_;
  ddk::GpioImplProtocolClient gpio_impl_;
  ddk::ClockImplProtocolClient clk_impl_;
  thrd_t thread_;
};

}  // namespace sherlock

#endif  // SRC_DEVICES_BOARD_DRIVERS_SHERLOCK_SHERLOCK_H_
