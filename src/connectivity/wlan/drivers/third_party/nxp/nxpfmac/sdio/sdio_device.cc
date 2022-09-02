// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/sdio/sdio_device.h"

#include <lib/async-loop/default.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"

namespace wlan::nxpfmac {

SdioDevice::SdioDevice(zx_device_t* parent) : Device(parent) {}

zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  auto async_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = async_loop->StartThread("nxpfmac_sdio-worker", nullptr)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<SdioDevice> device(new SdioDevice(parent_device));
  device->async_loop_ = std::move(async_loop);

  if ((status = device->DdkAdd(
           ddk::DeviceAddArgs("nxpfmac_sdio-wlanphy").set_proto_id(ZX_PROTOCOL_WLANPHY_IMPL))) !=
      ZX_OK) {
    NXPF_ERR("DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }
  device.release();  // This now has its lifecycle managed by the devhost.

  // Further initialization is performed in the SdioDevice::Init() DDK hook, invoked by the devhost.
  return ZX_OK;
}

async_dispatcher_t* SdioDevice::GetDispatcher() { return async_loop_->dispatcher(); }

zx_status_t SdioDevice::Init(mlan_device* mlan_dev, BusInterface** out_bus) {
  if (bus_) {
    NXPF_ERR("SDIO device already initialized");
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx_status_t status = SdioBus::Create(parent(), mlan_dev, &bus_);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to create SDIO bus: %s", zx_status_get_string(status));
    return status;
  }

  if (!IS_SD(mlan_dev->card_type)) {
    NXPF_ERR("Card type %u is NOT an SD card!", mlan_dev->card_type);
    return ZX_ERR_INTERNAL;
  }

  *out_bus = bus_.get();
  return ZX_OK;
}

zx_status_t SdioDevice::LoadFirmware(const char* path, zx::vmo* out_fw, size_t* out_size) {
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  zx_status_t status = load_firmware(zxdev(), path, &vmo, out_size);
  if (status != ZX_OK) {
    NXPF_ERR("Failed loading firmware file '%s': %s", path, zx_status_get_string(status));
    return status;
  }

  *out_fw = zx::vmo(vmo);

  return ZX_OK;
}

void SdioDevice::Shutdown() {
  if (async_loop_) {
    // Explicitly destroy the async loop before further shutdown to prevent asynchronous tasks
    // from using resources as they are being deallocated.
    async_loop_.reset();
  }
  // Then destroy the bus. This should perform a clean shutdown.
  bus_.reset();
}

}  // namespace wlan::nxpfmac
