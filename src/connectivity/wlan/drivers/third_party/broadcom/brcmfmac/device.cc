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

#include <ddktl/fidl.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/feature.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/macros.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/wlan_interface.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr char kClientInterfaceName[] = "brcmfmac-wlanif-client";
constexpr uint16_t kClientInterfaceId = 0;
constexpr char kApInterfaceName[] = "brcmfmac-wlanif-ap";
constexpr uint16_t kApInterfaceId = 1;

}  // namespace

namespace wlan_llcpp = ::llcpp::fuchsia::factory::wlan;

Device::Device(zx_device_t* parent)
    : ::ddk::Device<Device, ddk::Messageable>(parent),
      client_interface_(nullptr),
      ap_interface_(nullptr) {}

Device::~Device() = default;

void Device::DdkRelease() { delete this; }

zx_status_t Device::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  wlan_llcpp::Iovar::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void Device::Get(int32_t iface_idx, int32_t cmd, ::fidl::VectorView<uint8_t> request,
                 GetCompleter::Sync& _completer) {
  BRCMF_DBG(INFO, "brcmfmac: Device::Get cmd: %d len: %lu\n", cmd, request.count());
  zx_status_t status = brcmf_send_cmd_to_firmware(brcmf_pub_.get(), iface_idx, cmd,
                                                  (void*)request.data(), request.count(), false);
  if (status == ZX_OK) {
    _completer.ReplySuccess(std::move(request));
  } else {
    _completer.ReplyError(status);
  }
}

void Device::Set(int32_t iface_idx, int32_t cmd, ::fidl::VectorView<uint8_t> request,
                 SetCompleter::Sync& _completer) {
  BRCMF_DBG(INFO, "brcmfmac: Device::Set cmd: %d len: %lu\n", cmd, request.count());
  zx_status_t status = brcmf_send_cmd_to_firmware(brcmf_pub_.get(), iface_idx, cmd,
                                                  (void*)request.data(), request.count(), true);
  if (status == ZX_OK) {
    _completer.ReplySuccess();
  } else {
    _completer.ReplyError(status);
  }
}

zx_status_t Device::Init() {
  zx_status_t status = ZX_OK;

  auto dispatcher = std::make_unique<::async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  if ((status = dispatcher->StartThread("brcmfmac-worker", nullptr)) != ZX_OK) {
    return status;
  }

  // Initialize our module-level settings
  auto pub = std::make_unique<brcmf_pub>();
  pub->zxdev = parent();
  pub->dispatcher = dispatcher->dispatcher();
  for (auto& entry : pub->if2bss) {
    entry = BRCMF_BSSIDX_INVALID;
  }

  brcmf_pub_ = std::move(pub);
  dispatcher_ = std::move(dispatcher);
  return ZX_OK;
}

brcmf_pub* Device::drvr() { return brcmf_pub_.get(); }

const brcmf_pub* Device::drvr() const { return brcmf_pub_.get(); }

zx_status_t Device::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  BRCMF_DBG(WLANPHY, "Received query request from SME");
  return WlanInterface::Query(brcmf_pub_.get(), out_info);
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  const char* role = req->role == WLAN_INFO_MAC_ROLE_CLIENT ? "client"
                     : req->role == WLAN_INFO_MAC_ROLE_AP   ? "ap"
                     : req->role == WLAN_INFO_MAC_ROLE_MESH ? "mesh"
                                                            : "unknown type";

  if (req->has_init_mac_addr) {
    BRCMF_DBG(WLANPHY, "Creating %s interface (mac: " MAC_FMT_STR ")", role,
              MAC_FMT_ARGS(req->init_mac_addr));
  } else {
    BRCMF_DBG(WLANPHY, "Creating %s interface", role);
  }
  zx_status_t status = ZX_OK;
  wireless_dev* wdev = nullptr;
  uint16_t iface_id = 0;

  switch (req->role) {
    case WLAN_INFO_MAC_ROLE_CLIENT: {
      if (client_interface_ != nullptr) {
        BRCMF_ERR("Device::WlanphyImplCreateIface() client interface already exists");
        return ZX_ERR_NO_RESOURCES;
      }

      // If we are operating with manufacturing firmware ensure SoftAP IF is also not present
      if (brcmf_feat_is_enabled(brcmf_pub_.get(), BRCMF_FEAT_MFG)) {
        if (ap_interface_ != nullptr) {
          BRCMF_ERR("Simultaneous mode not supported in mfg FW - Ap IF already exists");
          return ZX_ERR_NO_RESOURCES;
        }
      }

      if ((status = brcmf_cfg80211_add_iface(brcmf_pub_.get(), kClientInterfaceName, nullptr, req,
                                             &wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplCreateIface() failed to create Client interface, %s",
                  zx_status_get_string(status));
        return status;
      }

      WlanInterface* interface = nullptr;
      if ((status = WlanInterface::Create(this, kClientInterfaceName, wdev, &interface)) != ZX_OK) {
        return status;
      }

      client_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kClientInterfaceId;
      break;
    }
    case WLAN_INFO_MAC_ROLE_AP: {
      if (ap_interface_ != nullptr) {
        BRCMF_ERR("Device::WlanphyImplCreateIface() AP interface already exists");
        return ZX_ERR_NO_RESOURCES;
      }

      // If we are operating with manufacturing firmware ensure client IF is also not present
      if (brcmf_feat_is_enabled(brcmf_pub_.get(), BRCMF_FEAT_MFG)) {
        if (client_interface_ != nullptr) {
          BRCMF_ERR("Simultaneous mode not supported in mfg FW - Client IF already exists");
          return ZX_ERR_NO_RESOURCES;
        }
      }

      if ((status = brcmf_cfg80211_add_iface(brcmf_pub_.get(), kApInterfaceName, nullptr, req,
                                             &wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplCreateIface() failed to create AP interface, %s",
                  zx_status_get_string(status));
        return status;
      }

      WlanInterface* interface = nullptr;
      if ((status = WlanInterface::Create(this, kApInterfaceName, wdev, &interface)) != ZX_OK) {
        return status;
      }

      ap_interface_ = interface;  // The lifecycle of `interface` is owned by the devhost.
      iface_id = kApInterfaceId;
      break;
    };
    default: {
      BRCMF_ERR("Device::WlanphyImplCreateIface() MAC role %d not supported", req->role);
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  *out_iface_id = iface_id;

  // Log the new iface's role, name, and MAC address
  net_device* ndev = wdev->netdev;
  const uint8_t* mac_addr = ndev_to_if(ndev)->mac_addr;
  BRCMF_DBG(WLANPHY, "Created %s iface with netdev:%s id:%d MAC address " MAC_FMT_STR, role,
            ndev->name, iface_id, MAC_FMT_ARGS(mac_addr));
  return ZX_OK;
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  zx_status_t status = ZX_OK;

  BRCMF_DBG(WLANPHY, "Destroying interface %d", iface_id);
  switch (iface_id) {
    case kClientInterfaceId: {
      if (client_interface_ == nullptr) {
        return ZX_ERR_NOT_FOUND;
      }
      wireless_dev* wdev = client_interface_->take_wdev();
      if ((status = brcmf_cfg80211_del_iface(brcmf_pub_->config, wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplDestroyIface() failed to cleanup STA interface, %s",
                  zx_status_get_string(status));
        return status;
      }
      client_interface_->DdkAsyncRemove();
      client_interface_ = nullptr;
      break;
    }
    case kApInterfaceId: {
      if (ap_interface_ == nullptr) {
        return ZX_ERR_NOT_FOUND;
      }
      wireless_dev* wdev = ap_interface_->take_wdev();
      if ((status = brcmf_cfg80211_del_iface(brcmf_pub_->config, wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplDestroyIface() failed to destroy AP interface, %s",
                  zx_status_get_string(status));
        return status;
      }
      ap_interface_->DdkAsyncRemove();
      ap_interface_ = nullptr;
      break;
    }
    default: {
      return ZX_ERR_NOT_FOUND;
    }
  }
  BRCMF_DBG(WLANPHY, "Interface %d destroyed successfully", iface_id);
  return ZX_OK;
}

zx_status_t Device::WlanphyImplSetCountry(const wlanphy_country_t* country) {
  BRCMF_DBG(WLANPHY, "Setting country code");
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return WlanInterface::SetCountry(brcmf_pub_.get(), country);
}

zx_status_t Device::WlanphyImplClearCountry() {
  BRCMF_DBG(WLANPHY, "Clearing country");
  return WlanInterface::ClearCountry(brcmf_pub_.get());
}

zx_status_t Device::WlanphyImplGetCountry(wlanphy_country_t* out_country) {
  BRCMF_DBG(WLANPHY, "Received request for country from SME");
  if (out_country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return WlanInterface::GetCountry(brcmf_pub_.get(), out_country);
}

void Device::DisableDispatcher() {
  if (dispatcher_ != nullptr) {
    dispatcher_->Shutdown();
  }
}

}  // namespace brcmfmac
}  // namespace wlan
