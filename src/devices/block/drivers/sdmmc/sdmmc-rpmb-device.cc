// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-rpmb-device.h"

#include <lib/fidl-async/cpp/bind.h>

#include "sdmmc-block-device.h"
#include "sdmmc-types.h"

namespace sdmmc {

zx_status_t RpmbDevice::Create(zx_device_t* parent, SdmmcBlockDevice* sdmmc,
                               const std::array<uint8_t, SDMMC_CID_SIZE>& cid,
                               const std::array<uint8_t, MMC_EXT_CSD_SIZE>& ext_csd) {
  auto device = std::make_unique<RpmbDevice>(parent, sdmmc, cid, ext_csd);

  if (auto status = device->loop_.StartThread("sdmmc-rpmb-thread"); status != ZX_OK) {
    zxlogf(ERROR, "failed to start RPMB thread: %d", status);
    return status;
  }
  device->outgoing_.emplace(device->loop_.dispatcher());
  device->outgoing_->svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_hardware_rpmb::Rpmb>,
      fbl::MakeRefCounted<fs::Service>(
          [device = device.get()](fidl::ServerEnd<fuchsia_hardware_rpmb::Rpmb> request) mutable {
            auto status = fidl::BindSingleInFlightOnly(device->loop_.dispatcher(),
                                                       std::move(request), device);
            if (status != ZX_OK) {
              zxlogf(ERROR, "failed to bind channel: %d", status);
            }
            return status;
          }));

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto status = device->outgoing_->Serve(std::move(endpoints->server));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to service the outoing directory");
    return status;
  }

  std::array offers = {
      fidl::DiscoverableProtocolName<fuchsia_hardware_rpmb::Rpmb>,
  };

  status = device->DdkAdd(ddk::DeviceAddArgs("rpmb")
                              .set_flags(DEVICE_ADD_MUST_ISOLATE)
                              .set_fidl_protocol_offers(offers)
                              .set_outgoing_dir(endpoints->client.TakeChannel()));

  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to add RPMB partition device: %d", status);
    return status;
  }

  __UNUSED auto* dummy1 = device.release();
  return ZX_OK;
}

void RpmbDevice::GetDeviceInfo(GetDeviceInfoRequestView request,
                               GetDeviceInfoCompleter::Sync& completer) {
  using DeviceInfo = fuchsia_hardware_rpmb::wire::DeviceInfo;
  using EmmcDeviceInfo = fuchsia_hardware_rpmb::wire::EmmcDeviceInfo;

  EmmcDeviceInfo emmc_info = {};
  memcpy(emmc_info.cid.data(), cid_.data(), cid_.size() * sizeof(cid_[0]));
  emmc_info.rpmb_size = rpmb_size_;
  emmc_info.reliable_write_sector_count = reliable_write_sector_count_;

  auto emmc_info_ptr = fidl::ObjectView<EmmcDeviceInfo>::FromExternal(&emmc_info);

  completer.ToAsync().Reply(DeviceInfo::WithEmmcInfo(emmc_info_ptr));
}

void RpmbDevice::Request(RequestRequestView request, RequestCompleter::Sync& completer) {
  RpmbRequestInfo info = {
      .tx_frames = std::move(request->request.tx_frames),
      .completer = completer.ToAsync(),
  };

  if (request->request.rx_frames) {
    info.rx_frames = {
        .vmo = std::move(request->request.rx_frames->vmo),
        .offset = request->request.rx_frames->offset,
        .size = request->request.rx_frames->size,
    };
  }

  sdmmc_parent_->RpmbQueue(std::move(info));
}

}  // namespace sdmmc
