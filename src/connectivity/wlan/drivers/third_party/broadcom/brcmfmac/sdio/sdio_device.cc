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

#include <lib/zircon-internal/align.h>

#include <string>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sdio/sdio.h"

namespace wlan {
namespace brcmfmac {

// static
zx_status_t SdioDevice::Create(zx_device_t* parent_device) {
  zx_status_t status = ZX_OK;

  const auto ddk_remover = [](SdioDevice* device) { device->DdkAsyncRemove(); };
  std::unique_ptr<SdioDevice, decltype(ddk_remover)> device(new SdioDevice(parent_device),
                                                            ddk_remover);
  if ((status = device->DdkAdd("brcmfmac-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    delete device.release();
    return status;
  }

  if ((status = device->brcmfmac::Device::Init()) != ZX_OK) {
    return status;
  }

  std::unique_ptr<brcmf_bus> bus;
  if ((status = brcmf_sdio_register(device->brcmf_pub_.get(), &bus)) != ZX_OK) {
    return status;
  }

  std::string firmware_binary;
  if ((status = GetFirmwareBinary(device.get(), brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
                                  static_cast<CommonCoreId>(bus->chip), bus->chiprev,
                                  &firmware_binary)) != ZX_OK) {
    return status;
  }

  const size_t padded_size_firmware = ZX_ROUNDUP(firmware_binary.size(), SDIOD_SIZE_ALIGNMENT);
  firmware_binary.resize(padded_size_firmware, '\0');

  std::string nvram_binary;
  if ((status = GetNvramBinary(device.get(), brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
                               static_cast<CommonCoreId>(bus->chip), bus->chiprev,
                               &nvram_binary)) != ZX_OK) {
    return status;
  }

  const size_t padded_size_nvram = ZX_ROUNDUP(nvram_binary.size(), SDIOD_SIZE_ALIGNMENT);
  nvram_binary.resize(padded_size_nvram, '\0');

  if ((status = brcmf_sdio_firmware_callback(device->brcmf_pub_.get(), firmware_binary.data(),
                                             firmware_binary.size(), nvram_binary.data(),
                                             nvram_binary.size())) != ZX_OK) {
    return status;
  }

  // CLM blob loading is optional, and only performed if the blob binary exists.
  std::string clm_binary;
  if ((status = GetClmBinary(device.get(), brcmf_bus_type::BRCMF_BUS_TYPE_SDIO,
                             static_cast<CommonCoreId>(bus->chip), bus->chiprev, &clm_binary)) ==
      ZX_OK) {
    // The firmware IOVAR accesses to upload the CLM blob are always on ifidx 0, so we stub out an
    // appropriate brcmf_if instance here.
    brcmf_if ifp = {};
    ifp.drvr = device->brcmf_pub_.get();
    ifp.ifidx = 0;
    if ((status = brcmf_c_process_clm_blob(&ifp, clm_binary)) != ZX_OK) {
      return status;
    }
  }

  if ((status = brcmf_bus_started(device->brcmf_pub_.get())) != ZX_OK) {
    return status;
  }

  device->brcmf_bus_ = std::move(bus);

  device->DdkMakeVisible();
  device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

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
  DisableDispatcher();
  if (brcmf_bus_) {
    brcmf_sdio_exit(brcmf_bus_.get());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
