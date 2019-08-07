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

#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlanphyimpl.h>
#include <hw/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcm_hw_ids.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"

namespace wlan {
namespace brcmfmac {

// This class uses the DDKTL classes to manage the lifetime of a brcmfmac driver instance.
class WlanphyImplDevice : public ::ddk::Device<WlanphyImplDevice, ::ddk::Unbindable>,
                          public ::ddk::WlanphyImplProtocol<WlanphyImplDevice, ddk::base_protocol> {
 public:
  // Static factory function for WlanphyImplDevice instances. This factory does not return the
  // instance itself, as on successful invocation the instance will have its lifecycle managed by
  // the devhost.
  static zx_status_t Create(zx_device_t* device);

  // DDK interface implementation.
  void DdkUnbind();
  void DdkRelease();

  // WlanphyImpl protocol implementation.
  zx_status_t WlanphyImplQuery(wlanphy_impl_info_t* out_info);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);

 protected:
  using DeviceType = ::ddk::Device<WlanphyImplDevice, ::ddk::Unbindable>;

  explicit WlanphyImplDevice(zx_device_t* parent);
  ~WlanphyImplDevice() = default;

 private:
  brcmf_device device_;
};

// static
zx_status_t WlanphyImplDevice::Create(zx_device_t* device) {
  zx_status_t status = ZX_OK;

  const auto ddk_remover = [](WlanphyImplDevice* device) { device->DdkRemove(); };
  std::unique_ptr<WlanphyImplDevice, decltype(ddk_remover)> wlanphyimpl_device(
      new WlanphyImplDevice(device), ddk_remover);
  if ((status = wlanphyimpl_device->DdkAdd("brcmfmac-wlanphy", DEVICE_ADD_INVISIBLE)) != ZX_OK) {
    delete wlanphyimpl_device.release();
    return status;
  }
  wlanphyimpl_device->device_.zxdev = device;
  wlanphyimpl_device->device_.phy_zxdev = wlanphyimpl_device->zxdev();

  if ((status = brcmf_core_init(&wlanphyimpl_device->device_)) != ZX_OK) {
    return status;
  }

  wlanphyimpl_device.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

void WlanphyImplDevice::DdkUnbind() {
  brcmf_core_exit(&device_);
  DdkRemove();
}

void WlanphyImplDevice::DdkRelease() { delete this; }

zx_status_t WlanphyImplDevice::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  if (!device_.bus) {
    return ZX_ERR_BAD_STATE;
  }
  brcmf_if* const ifp = device_.bus->drvr->iflist[0];
  return brcmf_phy_query(ifp, out_info);
}

zx_status_t WlanphyImplDevice::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                                      uint16_t* out_iface_id) {
  if (!device_.bus) {
    return ZX_ERR_BAD_STATE;
  }
  brcmf_if* const ifp = device_.bus->drvr->iflist[0];
  return brcmf_phy_create_iface(ifp, req, out_iface_id);
}

zx_status_t WlanphyImplDevice::WlanphyImplDestroyIface(uint16_t iface_id) {
  if (!device_.bus) {
    return ZX_ERR_BAD_STATE;
  }
  brcmf_if* const ifp = device_.bus->drvr->iflist[0];
  return brcmf_phy_destroy_iface(ifp, iface_id);
}

zx_status_t WlanphyImplDevice::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  if (!device_.bus) {
    return ZX_ERR_BAD_STATE;
  }
  brcmf_if* const ifp = device_.bus->drvr->iflist[0];
  return brcmf_phy_set_country(ifp, country);
}

WlanphyImplDevice::WlanphyImplDevice(zx_device_t* parent) : DeviceType(parent), device_() {}

}  // namespace brcmfmac
}  // namespace wlan

static constexpr zx_driver_ops_t brcmfmac_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = [](void** out_ctx) { return brcmfmac_module_init(); },
    .bind = [](void* ctx,
               zx_device_t* device) { return ::wlan::brcmfmac::WlanphyImplDevice::Create(device); },
    .release = [](void* ctx) { return brcmfmac_module_exit(); },
};

// clang-format off

ZIRCON_DRIVER_BEGIN(brcmfmac, brcmfmac_driver_ops, "zircon", "0.1", 33)
BI_GOTO_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE, 5910),
    BI_GOTO_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_USB, 758),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, BRCM_PCIE_VENDOR_ID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, PCI_CLASS_NETWORK),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, PCI_SUBCLASS_NETWORK_OTHER),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4350_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4356_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43567_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43570_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4358_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4359_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_43602_RAW_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4365_5G_DEVICE_ID),
    // TODO(cphoenix): support this chipset.
    // BRCMF_PCIE_DEVICE_SUB(0x4365, BRCM_PCIE_VENDOR_ID_BROADCOM, 0x4365),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_2G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4366_5G_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, BRCM_PCIE_4371_DEVICE_ID), BI_ABORT(), BI_LABEL(758),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x043e), BI_MATCH_IF(EQ, BIND_USB_PID, 0x3101), BI_ABORT(),
    // Composite binding used for SDIO.
    BI_LABEL(5910), BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_BCM_WIFI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM4356),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM43458), ZIRCON_DRIVER_END(brcmfmac)
