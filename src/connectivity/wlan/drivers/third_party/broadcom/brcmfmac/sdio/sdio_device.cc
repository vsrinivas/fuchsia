// Copyright (c) 2019 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio_device.h"

#include <lib/async-loop/default.h>
#include <lib/zircon-internal/align.h>

#include <limits>
#include <string>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio.h"

namespace wlan {
namespace brcmfmac {

// static
zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  auto async_loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = async_loop->StartThread("brcmfmac-worker", nullptr)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<DeviceInspect> inspect;
  if ((status = DeviceInspect::Create(async_loop->dispatcher(), &inspect)) != ZX_OK) {
    return status;
  }

  const auto ddk_remover = [](SdioDevice* device) { device->DdkAsyncRemove(); };
  std::unique_ptr<SdioDevice, decltype(ddk_remover)> device(new SdioDevice(parent_device),
                                                            ddk_remover);
  if ((status = device->DdkAdd(ddk::DeviceAddArgs("brcmfmac-wlanphy")
                                   .set_flags(DEVICE_ADD_INVISIBLE)
                                   .set_inspect_vmo(inspect->inspector().DuplicateVmo()))) !=
      ZX_OK) {
    delete device.release();
    return status;
  }

  device->async_loop_ = std::move(async_loop);
  device->inspect_ = std::move(inspect);

  std::unique_ptr<brcmf_bus> bus;
  if ((status = brcmf_sdio_register(device->drvr(), &bus)) != ZX_OK) {
    return status;
  }

  if ((status = brcmf_sdio_load_files(device->drvr(), false)) != ZX_OK) {
    return status;
  }

  if ((status = brcmf_bus_started(device->drvr(), false)) != ZX_OK) {
    return status;
  }

  device->brcmf_bus_ = std::move(bus);

  device->DdkMakeVisible();
  device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

async_dispatcher_t* SdioDevice::GetDispatcher() { return async_loop_->dispatcher(); }

DeviceInspect* SdioDevice::GetInspect() { return inspect_.get(); }

zx_status_t SdioDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return device_add(zxdev(), args, out_device);
}

void SdioDevice::DeviceAsyncRemove(zx_device_t* dev) { device_async_remove(dev); }

zx_status_t SdioDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return load_firmware(zxdev(), path, fw, size);
}

zx_status_t SdioDevice::DeviceGetMetadata(uint32_t type, void* buf, size_t buflen, size_t* actual) {
  return device_get_metadata(zxdev(), type, buf, buflen, actual);
}

SdioDevice::SdioDevice(zx_device_t* parent) : Device(parent) {}

SdioDevice::~SdioDevice() {
  if (brcmf_bus_) {
    brcmf_sdio_exit(brcmf_bus_.get());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
