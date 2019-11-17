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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_device.h"

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"

namespace wlan {
namespace brcmfmac {

PcieDevice::PcieDevice(zx_device_t* parent) : Device(parent) {}

PcieDevice::~PcieDevice() { DisableDispatcher(); }

// static
zx_status_t PcieDevice::Create(zx_device_t* parent_device, PcieDevice** out_device) {
  zx_status_t status = ZX_OK;

  const auto ddk_remover = [](PcieDevice* device) { device->DdkAsyncRemove(); };
  std::unique_ptr<PcieDevice, decltype(ddk_remover)> device(new PcieDevice(parent_device),
                                                            ddk_remover);
  if ((status = device->DdkAdd("brcmfmac-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    delete device.release();
    return status;
  }

  if ((status = device->brcmfmac::Device::Init()) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieBus> pcie_bus;
  if ((status = PcieBus::Create(device.get(), &pcie_bus)) != ZX_OK) {
    return status;
  }

  device->pcie_bus_ = std::move(pcie_bus);

  // TODO(sheu): make the device visible once higher-level functionality is present.
  // device->DdkMakeVisible();

  *out_device = device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

zx_status_t PcieDevice::DeviceAdd(device_add_args_t* args, zx_device_t** out_device) {
  return device_add(zxdev(), args, out_device);
}

void PcieDevice::DeviceAsyncRemove(zx_device_t* dev) { device_async_remove(dev); }

zx_status_t PcieDevice::LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) {
  return load_firmware(zxdev(), path, fw, size);
}

}  // namespace brcmfmac
}  // namespace wlan
