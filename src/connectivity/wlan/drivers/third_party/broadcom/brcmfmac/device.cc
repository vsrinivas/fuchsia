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
#include <ddktl/init-txn.h>

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

namespace wlan_llcpp = fuchsia_factory_wlan;

Device::Device(zx_device_t* parent)
    : DeviceType(parent),
      brcmf_pub_(std::make_unique<brcmf_pub>()),
      client_interface_(nullptr),
      ap_interface_(nullptr) {
  brcmf_pub_->device = this;
  for (auto& entry : brcmf_pub_->if2bss) {
    entry = BRCMF_BSSIDX_INVALID;
  }

  // Initialize the recovery trigger for driver, shared by all buses' devices.
  auto recovery_start_callback = std::make_shared<std::function<zx_status_t()>>();
  *recovery_start_callback = std::bind(&brcmf_schedule_recovery_worker, brcmf_pub_.get());
  brcmf_pub_->recovery_trigger =
      std::make_unique<wlan::brcmfmac::RecoveryTrigger>(recovery_start_callback);
}

Device::~Device() = default;

void Device::DdkInit(ddk::InitTxn txn) { Init(std::move(txn)); }

void Device::DdkRelease() { delete this; }

void Device::Get(GetRequestView request, GetCompleter::Sync& _completer) {
  BRCMF_DBG(TRACE, "Enter. cmd %d, len %lu", request->cmd, request->request.count());
  zx_status_t status =
      brcmf_send_cmd_to_firmware(brcmf_pub_.get(), request->iface_idx, request->cmd,
                                 (void*)request->request.data(), request->request.count(), false);
  if (status == ZX_OK) {
    _completer.ReplySuccess(request->request);
  } else {
    _completer.ReplyError(status);
  }
  BRCMF_DBG(TRACE, "Exit");
}

void Device::Set(SetRequestView request, SetCompleter::Sync& _completer) {
  BRCMF_DBG(TRACE, "Enter. cmd %d, len %lu", request->cmd, request->request.count());
  zx_status_t status =
      brcmf_send_cmd_to_firmware(brcmf_pub_.get(), request->iface_idx, request->cmd,
                                 (void*)request->request.data(), request->request.count(), true);
  if (status == ZX_OK) {
    _completer.ReplySuccess();
  } else {
    _completer.ReplyError(status);
  }
  BRCMF_DBG(TRACE, "Exit");
}

brcmf_pub* Device::drvr() { return brcmf_pub_.get(); }

const brcmf_pub* Device::drvr() const { return brcmf_pub_.get(); }

zx_status_t Device::WlanphyImplQuery(wlanphy_impl_info_t* out_info) {
  BRCMF_DBG(WLANPHY, "Received query request from SME");
  return WlanInterface::Query(brcmf_pub_.get(), out_info);
}

zx_status_t Device::WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                           uint16_t* out_iface_id) {
  std::lock_guard<std::mutex> lock(lock_);
  const char* role = req->role == WLAN_INFO_MAC_ROLE_CLIENT ? "client"
                     : req->role == WLAN_INFO_MAC_ROLE_AP   ? "ap"
                     : req->role == WLAN_INFO_MAC_ROLE_MESH ? "mesh"
                                                            : "unknown type";

  if (req->has_init_sta_addr) {
    BRCMF_DBG(WLANPHY, "Creating %s interface", role);
#if !defined(NDEBUG)
    BRCMF_DBG(WLANPHY, "  address: " MAC_FMT_STR, MAC_FMT_ARGS(req->init_sta_addr));
#endif /* !defined(NDEBUG) */
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

  BRCMF_DBG(WLANPHY, "Created %s iface with netdev:%s id:%d", role, ndev->name, iface_id);
#if !defined(NDEBUG)
  const uint8_t* mac_addr = ndev_to_if(ndev)->mac_addr;
  BRCMF_DBG(WLANPHY, "  address: " MAC_FMT_STR, MAC_FMT_ARGS(mac_addr));
#endif /* !defined(NDEBUG) */
  return ZX_OK;
}

zx_status_t Device::WlanphyImplDestroyIface(uint16_t iface_id) {
  std::lock_guard<std::mutex> lock(lock_);
  zx_status_t status = ZX_OK;

  BRCMF_DBG(WLANPHY, "Destroying interface %d", iface_id);
  switch (iface_id) {
    case kClientInterfaceId: {
      if (client_interface_ == nullptr) {
        BRCMF_INFO("Client interface %d unavailable, skipping destroy.", iface_id);
        return ZX_ERR_NOT_FOUND;
      }
      wireless_dev* wdev = client_interface_->take_wdev();
      if ((status = brcmf_cfg80211_del_iface(brcmf_pub_->config, wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplDestroyIface() failed to cleanup STA interface, %s",
                  zx_status_get_string(status));
        // Set the wdev back to WlanInterface if the deletion failed, because it means that wdev is
        // not actually released.
        client_interface_->set_wdev(wdev);
        return status;
      }
      client_interface_->DdkAsyncRemove();
      client_interface_ = nullptr;
      break;
    }
    case kApInterfaceId: {
      if (ap_interface_ == nullptr) {
        BRCMF_INFO("AP interface %d unavailable, skipping destroy.", iface_id);
        return ZX_ERR_NOT_FOUND;
      }
      wireless_dev* wdev = ap_interface_->take_wdev();
      if ((status = brcmf_cfg80211_del_iface(brcmf_pub_->config, wdev)) != ZX_OK) {
        BRCMF_ERR("Device::WlanphyImplDestroyIface() failed to destroy AP interface, %s",
                  zx_status_get_string(status));
        // Set the wdev back to WlanInterface if the deletion failed, because it means that wdev is
        // not actually released.
        ap_interface_->set_wdev(wdev);
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

void Device::DestroyAllIfaces(void) {
  zx_status_t status = WlanphyImplDestroyIface(kClientInterfaceId);
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    BRCMF_ERR("Failed to destroy client iface %d status %s", kClientInterfaceId,
              zx_status_get_string(status));
  }

  status = WlanphyImplDestroyIface(kApInterfaceId);
  if (status != ZX_OK && status != ZX_ERR_NOT_FOUND) {
    BRCMF_ERR("Failed to destroy ap iface %d status %s", kApInterfaceId,
              zx_status_get_string(status));
  }
}

}  // namespace brcmfmac
}  // namespace wlan
