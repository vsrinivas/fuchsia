// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_
#define SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/iommu/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <threads.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <soc/aml-a5/a5-hw.h>

#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

// BTI IDs for our devices
enum {
  BTI_CANVAS,
  BTI_DISPLAY,
  BTI_EMMC,
  BTI_ETHERNET,
  BTI_SD,
  BTI_SDIO,
  BTI_SYSMEM,
  BTI_NNA,
  BTI_USB,
  BTI_MALI,
  BTI_VIDEO,
  BTI_SPI1,
  BTI_AUDIO_OUT,
  BTI_AUDIO_IN,
  BTI_TEE,
};

// Av400 SPI bus arbiters (should match spi_channels[] in av400-spi.cc  ).
enum {
  AV400_SPICC0,
  AV400_SPICC1,
};

// MAC address metadata indices.
// Currently the bootloader only sets up a single MAC zbi entry, we'll use it for both the WiFi and
// BT radio MACs.
enum {
  MACADDR_WIFI = 0,
  MACADDR_BLUETOOTH = 0,
};

class Av400;
using Av400Type = ddk::Device<Av400, ddk::Initializable>;

// This is the main class for the platform bus driver.
class Av400 : public Av400Type {
 public:
  Av400(zx_device_t* parent, fdf::ClientEnd<fuchsia_hardware_platform_bus::PlatformBus> pbus,
        iommu_protocol_t* iommu)
      : Av400Type(parent), pbus_(std::move(pbus)), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() {}

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Av400);

  zx_status_t GpioInit();
  zx_status_t ButtonsInit();
  zx_status_t PwmInit();
  zx_status_t ClkInit();
  zx_status_t I2cInit();
  zx_status_t RegistersInit();
  zx_status_t EmmcInit();
  zx_status_t SpiInit();
  zx_status_t SdioInit();
  zx_status_t EthInit();
  zx_status_t RtcInit();
  zx_status_t AudioInit();
  zx_status_t UsbInit();
  zx_status_t ThermalInit();
  zx_status_t SysmemInit();
  zx_status_t TeeInit();
  zx_status_t PowerInit();
  zx_status_t CpuInit();
  zx_status_t DmcInit();
  zx_status_t MailboxInit();
  zx_status_t DspInit();

  int Thread();

  fdf::WireSyncClient<fuchsia_hardware_platform_bus::PlatformBus> pbus_;
  std::optional<ddk::InitTxn> init_txn_;
  ddk::IommuProtocolClient iommu_;
  thrd_t thread_;
  ddk::GpioImplProtocolClient gpio_impl_;
  ddk::ClockImplProtocolClient clk_impl_;
};

}  // namespace av400

#endif  // SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_
