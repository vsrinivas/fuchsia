// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_
#define SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_

#include <fuchsia/hardware/iommu/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <threads.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/macros.h>
#include <soc/aml-a5/a5-hw.h>

namespace av400 {

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
  Av400(zx_device_t* parent, pbus_protocol_t* pbus, iommu_protocol_t* iommu)
      : Av400Type(parent), pbus_(pbus), iommu_(iommu) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease() {}

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Av400);

  zx_status_t GpioInit();

  int Thread();

  ddk::PBusProtocolClient pbus_;
  std::optional<ddk::InitTxn> init_txn_;
  ddk::IommuProtocolClient iommu_;
  thrd_t thread_;
};

}  // namespace av400

#endif  // SRC_DEVICES_BOARD_DRIVERS_AV400_AV400_H_
