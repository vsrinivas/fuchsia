// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_RPMB_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_RPMB_DEVICE_H_

#include <fidl/fuchsia.hardware.rpmb/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sdmmc/hw.h>
#include <lib/svc/outgoing.h>

#include <array>
#include <cinttypes>
#include <optional>

#include <ddktl/device.h>

namespace sdmmc {

class SdmmcBlockDevice;

class RpmbDevice;
using RpmbDeviceType =
    ddk::Device<RpmbDevice, ddk::Messageable<fuchsia_hardware_rpmb::Rpmb>::Mixin>;

class RpmbDevice : public RpmbDeviceType {
 public:
  static zx_status_t Create(zx_device_t* parent, SdmmcBlockDevice* sdmmc,
                            const std::array<uint8_t, SDMMC_CID_SIZE>& cid,
                            const std::array<uint8_t, MMC_EXT_CSD_SIZE>& ext_csd);
  // sdmmc_parent is owned by the SDMMC root device when the RpmbDevice object is created. Ownership
  // is transferred to devmgr shortly after, meaning it will outlive this object due to the
  // parent/child device relationship.
  RpmbDevice(zx_device_t* parent, SdmmcBlockDevice* sdmmc_parent,
             const std::array<uint8_t, SDMMC_CID_SIZE>& cid,
             const std::array<uint8_t, MMC_EXT_CSD_SIZE>& ext_csd)
      : RpmbDeviceType(parent),
        sdmmc_parent_(sdmmc_parent),
        cid_(cid),
        rpmb_size_(ext_csd[MMC_EXT_CSD_RPMB_SIZE_MULT]),
        reliable_write_sector_count_(ext_csd[MMC_EXT_CSD_REL_WR_SEC_C]),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void DdkRelease() { delete this; }

  void GetDeviceInfo(GetDeviceInfoRequestView request,
                     GetDeviceInfoCompleter::Sync& completer) override;
  void Request(RequestRequestView request, RequestCompleter::Sync& completer) override;

 private:
  SdmmcBlockDevice* const sdmmc_parent_;
  const std::array<uint8_t, SDMMC_CID_SIZE> cid_;
  const uint8_t rpmb_size_;
  const uint8_t reliable_write_sector_count_;
  std::optional<svc::Outgoing> outgoing_;
  async::Loop loop_;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_RPMB_DEVICE_H_
