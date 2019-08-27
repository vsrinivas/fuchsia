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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio_device.h"

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio.h"

namespace wlan {
namespace brcmfmac {

// static
zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  const auto ddk_remover = [](SdioDevice* device) { device->DdkRemove(); };
  std::unique_ptr<SdioDevice, decltype(ddk_remover)> device(new SdioDevice(parent_device),
                                                            ddk_remover);
  if ((status = device->DdkAdd("brcmfmac-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    delete device.release();
    return status;
  }

  auto bus_register_fn = std::bind(&SdioDevice::BusRegister, device.get(), std::placeholders::_1);
  status = device->brcmfmac::Device::Init(device->zxdev(), parent_device, bus_register_fn);
  if (status != ZX_OK) {
    return status;
  }

  device->DdkMakeVisible();
  device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

void SdioDevice::DdkUnbind() { DdkRemove(); }

void SdioDevice::DdkRelease() { delete this; }

zx_status_t SdioDevice::BusRegister(brcmf_pub* drvr) {
  zx_status_t status;
  std::unique_ptr<brcmf_bus> bus;

  if ((status = brcmf_sdio_register(drvr, &bus)) != ZX_OK) {
    return status;
  }

  brcmf_bus_ = std::move(bus);
  return ZX_OK;
}

SdioDevice::SdioDevice(zx_device_t* parent)
    : ::ddk::Device<SdioDevice, ::ddk::Unbindable>(parent) {}

SdioDevice::~SdioDevice() {
  DisableDispatcher();
  if (brcmf_bus_) {
    brcmf_sdio_exit(brcmf_bus_.get());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
