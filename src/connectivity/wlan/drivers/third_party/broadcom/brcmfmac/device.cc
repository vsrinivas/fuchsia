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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {

zx_status_t Device::Init(zx_device_t* phy_device, zx_device_t* parent_device,
                         BusRegisterFn bus_register_fn) {
  zx_status_t status = ZX_OK;

  auto dispatcher = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = dispatcher->StartThread("brcmfmac-worker", nullptr)) != ZX_OK) {
    return status;
  }

  // Initialize our module-level settings
  auto pub = std::make_unique<brcmf_pub>();
  pub->phy_zxdev = phy_device;
  pub->zxdev = parent_device;
  pub->dispatcher = dispatcher->dispatcher();
  for (auto& entry : pub->if2bss) {
    entry = BRCMF_BSSIDX_INVALID;
  }

  // Perform the bus-specific initialization
  if ((status = (bus_register_fn)(pub.get())) != ZX_OK) {
    BRCMF_ERR("BusRegister() failed: %s\n", zx_status_get_string(status));
    return status;
  }

  dispatcher_ = std::move(dispatcher);
  brcmf_pub_ = std::move(pub);
  return ZX_OK;
}

zx_status_t Device::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  return brcmf_phy_query(brcmf_pub_->iflist[0], out_info);
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  return brcmf_phy_create_iface(brcmf_pub_->iflist[0], req, out_iface_id);
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  return brcmf_phy_destroy_iface(brcmf_pub_->iflist[0], iface_id);
}

zx_status_t Device::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  return brcmf_phy_set_country(brcmf_pub_->iflist[0], country);
}

void Device::DisableDispatcher() {
  if (dispatcher_ != nullptr) {
    dispatcher_->Shutdown();
  }
}

}  // namespace brcmfmac
}  // namespace wlan
