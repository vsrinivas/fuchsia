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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_DEVICE_H_

#include <zircon/types.h>

#include <ddk/device.h>
#include <ddktl/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan::brcmfmac {

// This class uses the DDKTL classes to manage the lifetime of a brcmfmac driver instance.
class SdioDevice : public Device, public ::ddk::Device<SdioDevice, ::ddk::Unbindable> {
 public:
  // Static factory function for SdioDevice instances. This factory does not return an owned
  // instance, as on successful invocation the instance will have its lifecycle maanged by the
  // devhost.
  static zx_status_t Create(zx_device_t* parent_device);

  SdioDevice(const SdioDevice& device) = delete;
  SdioDevice& operator=(const SdioDevice& other) = delete;

  // DDK interface implementation.
  void DdkUnbind();
  void DdkRelease();

 protected:
  explicit SdioDevice(zx_device_t* parent);
  ~SdioDevice();

  // Perform the bus-specific initialization
  zx_status_t BusRegister(brcmf_pub* drvr);

 private:
  std::unique_ptr<brcmf_bus> brcmf_bus_;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SDIO_DEVICE_H_
