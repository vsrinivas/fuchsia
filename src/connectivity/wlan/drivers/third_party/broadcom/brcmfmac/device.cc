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

// static
zx_status_t Device::Create(zx_device_t* parent_device, Device** device_out) {
  zx_status_t status = ZX_OK;

  auto dispatcher = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  if ((status = dispatcher->StartThread("brcmfmac-worker", nullptr)) != ZX_OK) {
    return status;
  }

  const auto ddk_remover = [](Device* device) { device->DdkRemove(); };
  std::unique_ptr<Device, decltype(ddk_remover)> device(new Device(parent_device), ddk_remover);
  if ((status = device->DdkAdd("brcmfmac-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    delete device.release();
    return status;
  }

  auto pub = std::make_unique<brcmf_pub>();
  pub->zxdev = parent_device;
  pub->phy_zxdev = device->zxdev();
  pub->dispatcher = dispatcher->dispatcher();
  for (auto& entry : pub->if2bss) {
    entry = BRCMF_BSSIDX_INVALID;
  }

  std::unique_ptr<brcmf_bus> bus;
  if ((status = brcmf_bus_register(pub.get(), &bus)) != ZX_OK) {
    BRCMF_ERR("brcmf_bus_register() returned %s\n", zx_status_get_string(status));
    return status;
  }

  device->dispatcher_ = std::move(dispatcher);
  device->brcmf_pub_ = std::move(pub);
  device->brcmf_bus_ = std::move(bus);
  device->DdkMakeVisible();

  if (device_out != nullptr) {
    *device_out = device.get();
  }
  device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

void Device::SimUnbind() {
  // Simulate the devhost unbind/remove process.
  DdkUnbind();

  // device_remove() on this device causes the equivalent of Unbind() to be invoked on its children.
  brcmf_phy_destroy_iface(brcmf_pub_->iflist[0], 0);

  // Now this device is released.
  DdkRelease();
}

void Device::DdkUnbind() { DdkRemove(); }

void Device::DdkRelease() { delete this; }

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

Device::Device(zx_device_t* parent) : ::ddk::Device<Device, ::ddk::Unbindable>(parent) {}

Device::~Device() {
  // Make sure any asynchronous work is stopped first.
  if (dispatcher_ != nullptr) {
    dispatcher_->Shutdown();
  }
  if (brcmf_bus_ != nullptr) {
    brcmf_bus_exit(brcmf_bus_.get());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
