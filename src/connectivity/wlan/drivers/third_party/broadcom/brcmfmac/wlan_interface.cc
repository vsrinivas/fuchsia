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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/wlan_interface.h"

#include <fuchsia/hardware/network/device/c/banjo.h>
#include <fuchsia/hardware/network/mac/c/banjo.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cstdio>
#include <cstring>

#include "fuchsia/hardware/wlan/fullmac/c/banjo.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/feature.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr uint32_t kEthernetMtu = 1500;

constexpr zx_protocol_device_t kWlanInterfaceDeviceOps = {
    .version = DEVICE_OPS_VERSION,
    .release = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->DdkRelease(); },
};

wlan_fullmac_impl_protocol_ops_t wlan_interface_proto_ops = {
    .start =
        [](void* ctx, const wlan_fullmac_impl_ifc_protocol_t* ifc, zx_handle_t* out_mlme_channel) {
          return static_cast<WlanInterface*>(ctx)->Start(ifc, out_mlme_channel);
        },
    .stop = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->Stop(); },
    .query =
        [](void* ctx, wlan_fullmac_query_info_t* info) {
          return static_cast<WlanInterface*>(ctx)->Query(info);
        },
    .query_mac_sublayer_support =
        [](void* ctx, mac_sublayer_support_t* resp) {
          return static_cast<WlanInterface*>(ctx)->QueryMacSublayerSupport(resp);
        },
    .query_security_support =
        [](void* ctx, security_support_t* resp) {
          return static_cast<WlanInterface*>(ctx)->QuerySecuritySupport(resp);
        },
    .query_spectrum_management_support =
        [](void* ctx, spectrum_management_support_t* resp) {
          return static_cast<WlanInterface*>(ctx)->QuerySpectrumManagementSupport(resp);
        },
    .start_scan =
        [](void* ctx, const wlan_fullmac_scan_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StartScan(req);
        },
    .connect_req =
        [](void* ctx, const wlan_fullmac_connect_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->ConnectReq(req);
        },
    .reconnect_req =
        [](void* ctx, const wlan_fullmac_reconnect_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->ReconnectReq(req);
        },
    .auth_resp =
        [](void* ctx, const wlan_fullmac_auth_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->AuthResp(resp);
        },
    .deauth_req =
        [](void* ctx, const wlan_fullmac_deauth_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->DeauthReq(req);
        },
    .assoc_resp =
        [](void* ctx, const wlan_fullmac_assoc_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->AssocResp(resp);
        },
    .disassoc_req =
        [](void* ctx, const wlan_fullmac_disassoc_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->DisassocReq(req);
        },
    .reset_req =
        [](void* ctx, const wlan_fullmac_reset_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->ResetReq(req);
        },
    .start_req =
        [](void* ctx, const wlan_fullmac_start_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StartReq(req);
        },
    .stop_req =
        [](void* ctx, const wlan_fullmac_stop_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StopReq(req);
        },
    .set_keys_req =
        [](void* ctx, const wlan_fullmac_set_keys_req* req, wlan_fullmac_set_keys_resp* resp) {
          return static_cast<WlanInterface*>(ctx)->SetKeysReq(req, resp);
        },
    .del_keys_req =
        [](void* ctx, const wlan_fullmac_del_keys_req* req) {
          return static_cast<WlanInterface*>(ctx)->DelKeysReq(req);
        },
    .eapol_req =
        [](void* ctx, const wlan_fullmac_eapol_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->EapolReq(req);
        },
    .get_iface_counter_stats =
        [](void* ctx, wlan_fullmac_iface_counter_stats_t* out_stats) {
          return static_cast<WlanInterface*>(ctx)->GetIfaceCounterStats(out_stats);
        },
    .get_iface_histogram_stats =
        [](void* ctx, wlan_fullmac_iface_histogram_stats_t* out_stats) {
          return static_cast<WlanInterface*>(ctx)->GetIfaceHistogramStats(out_stats);
        },
    .start_capture_frames =
        [](void* ctx, const wlan_fullmac_start_capture_frames_req_t* req,
           wlan_fullmac_start_capture_frames_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->StartCaptureFrames(req, resp);
        },
    .stop_capture_frames =
        [](void* ctx) { return static_cast<WlanInterface*>(ctx)->StopCaptureFrames(); },
    .sae_handshake_resp =
        [](void* ctx, const wlan_fullmac_sae_handshake_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->SaeHandshakeResp(resp);
        },
    .sae_frame_tx =
        [](void* ctx, const wlan_fullmac_sae_frame_t* frame) {
          return static_cast<WlanInterface*>(ctx)->SaeFrameTx(frame);
        },
    .wmm_status_req = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->WmmStatusReq(); },
    .set_multicast_promisc =
        [](void* ctx, bool enable) {
          return static_cast<WlanInterface*>(ctx)->SetMulticastPromisc(enable);
        },
    .data_queue_tx =
        [](void* ctx, uint32_t options, ethernet_netbuf_t* netbuf,
           ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
          return static_cast<WlanInterface*>(ctx)->DataQueueTx(options, netbuf, completion_cb,
                                                               cookie);
        },
    .on_link_state_changed =
        [](void* ctx, bool online) {
          static_cast<WlanInterface*>(ctx)->OnLinkStateChanged(online);
        },
};

}  // namespace

WlanInterface::WlanInterface(const network_device_ifc_protocol_t& proto, uint8_t port_id)
    : NetworkPort(proto, *this, port_id), zx_device_(nullptr), wdev_(nullptr), device_(nullptr) {}

zx_status_t WlanInterface::Create(Device* device, const char* name, wireless_dev* wdev,
                                  wlan_mac_role_t role, WlanInterface** out_interface) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<WlanInterface> interface(
      new WlanInterface(device->NetDev().NetDevIfcProto(), ndev_to_if(wdev->netdev)->ifidx));
  {
    std::lock_guard<std::shared_mutex> guard(interface->lock_);
    interface->device_ = device;
    interface->wdev_ = wdev;
  }

  device_add_args device_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = interface.get(),
      .ops = &kWlanInterfaceDeviceOps,
      .proto_id = ZX_PROTOCOL_WLAN_FULLMAC_IMPL,
      .proto_ops = &wlan_interface_proto_ops,
  };
  if (device->DeviceAdd(&device_args, &interface->zx_device_) != ZX_OK) {
    return status;
  }

  NetworkPort::Role net_port_role;
  switch (role) {
    case WLAN_MAC_ROLE_CLIENT:
      net_port_role = NetworkPort::Role::Client;
      break;
    case WLAN_MAC_ROLE_AP:
      net_port_role = NetworkPort::Role::Ap;
      break;
    default:
      BRCMF_ERR("Unsupported role %u", role);
      return ZX_ERR_INVALID_ARGS;
  }

  if (device->IsNetworkDeviceBus()) {
    interface->NetworkPort::Init(net_port_role);
  }

  *out_interface = interface.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

zx_device_t* WlanInterface::zxdev() { return zx_device_; }

const zx_device_t* WlanInterface::zxdev() const { return zx_device_; }

void WlanInterface::set_wdev(wireless_dev* wdev) {
  std::lock_guard<std::shared_mutex> guard(lock_);
  wdev_ = wdev;
}

wireless_dev* WlanInterface::take_wdev() {
  std::lock_guard<std::shared_mutex> guard(lock_);
  wireless_dev* wdev = wdev_;
  wdev_ = nullptr;
  return wdev;
}

void WlanInterface::DdkAsyncRemove(fit::callback<void()>&& on_remove) {
  zx_device_t* const device = zx_device_;
  if (device == nullptr) {
    on_remove();
    return;
  }
  zx_device_ = nullptr;
  {
    std::lock_guard lock(lock_);
    on_remove_ = std::move(on_remove);
  }
  device_->DeviceAsyncRemove(device);
}

void WlanInterface::DdkRelease() {
  fit::callback<void()> on_remove;
  {
    std::lock_guard lock(lock_);
    if (on_remove_) {
      on_remove = std::move(on_remove_);
    }
  }
  delete this;
  if (on_remove) {
    on_remove();
  }
}

// static
zx_status_t WlanInterface::GetSupportedMacRoles(
    struct brcmf_pub* drvr,
    fuchsia_wlan_common::wire::WlanMacRole
        out_supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles],
    uint8_t* out_supported_mac_roles_count) {
  // The default client iface at bsscfgidx 0 is always assumed to exist by the driver.
  if (!drvr->iflist[0]) {
    BRCMF_ERR("drvr->iflist[0] is NULL. This should never happen.");
    return ZX_ERR_INTERNAL;
  }

  size_t len = 0;
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_STA)) {
    out_supported_mac_roles_list[len] = fuchsia_wlan_common::wire::WlanMacRole::kClient;
    ++len;
  }
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_AP)) {
    out_supported_mac_roles_list[len] = fuchsia_wlan_common::wire::WlanMacRole::kAp;
    ++len;
  }
  *out_supported_mac_roles_count = len;

  return ZX_OK;
}

// static
zx_status_t WlanInterface::SetCountry(brcmf_pub* drvr, const wlanphy_country_t* country) {
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return brcmf_set_country(drvr, country);
}

// static
zx_status_t WlanInterface::GetCountry(brcmf_pub* drvr, wlanphy_country_t* out_country) {
  return brcmf_get_country(drvr, out_country);
}

zx_status_t WlanInterface::ClearCountry(brcmf_pub* drvr) { return brcmf_clear_country(drvr); }

zx_status_t WlanInterface::Start(const wlan_fullmac_impl_ifc_protocol_t* ifc,
                                 zx_handle_t* out_mlme_channel) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return brcmf_if_start(wdev_->netdev, ifc, out_mlme_channel);
}

void WlanInterface::Stop() {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_stop(wdev_->netdev);
  }
}

void WlanInterface::Query(wlan_fullmac_query_info_t* info) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_query(wdev_->netdev, info);
  }
}

void WlanInterface::QueryMacSublayerSupport(mac_sublayer_support_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_query_mac_sublayer_support(wdev_->netdev, resp);
  }
}

void WlanInterface::QuerySecuritySupport(security_support_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_query_security_support(wdev_->netdev, resp);
  }
}

void WlanInterface::QuerySpectrumManagementSupport(spectrum_management_support_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_query_spectrum_management_support(wdev_->netdev, resp);
  }
}

void WlanInterface::StartScan(const wlan_fullmac_scan_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_start_scan(wdev_->netdev, req);
  }
}

void WlanInterface::ConnectReq(const wlan_fullmac_connect_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_connect_req(wdev_->netdev, req);
  }
}

void WlanInterface::ReconnectReq(const wlan_fullmac_reconnect_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_reconnect_req(wdev_->netdev, req);
  }
}

void WlanInterface::AuthResp(const wlan_fullmac_auth_resp_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_auth_resp(wdev_->netdev, resp);
  }
}

void WlanInterface::DeauthReq(const wlan_fullmac_deauth_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_deauth_req(wdev_->netdev, req);
  }
}

void WlanInterface::AssocResp(const wlan_fullmac_assoc_resp_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_assoc_resp(wdev_->netdev, resp);
  }
}

void WlanInterface::DisassocReq(const wlan_fullmac_disassoc_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_disassoc_req(wdev_->netdev, req);
  }
}

void WlanInterface::ResetReq(const wlan_fullmac_reset_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_reset_req(wdev_->netdev, req);
  }
}

void WlanInterface::StartReq(const wlan_fullmac_start_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_start_req(wdev_->netdev, req);
  }
}

void WlanInterface::StopReq(const wlan_fullmac_stop_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_stop_req(wdev_->netdev, req);
  }
}

void WlanInterface::SetKeysReq(const wlan_fullmac_set_keys_req_t* req,
                               wlan_fullmac_set_keys_resp_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_set_keys_req(wdev_->netdev, req, resp);
  }
}

void WlanInterface::DelKeysReq(const wlan_fullmac_del_keys_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_del_keys_req(wdev_->netdev, req);
  }
}

void WlanInterface::EapolReq(const wlan_fullmac_eapol_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_eapol_req(wdev_->netdev, req);
  }
}

zx_status_t WlanInterface::GetIfaceCounterStats(wlan_fullmac_iface_counter_stats_t* out_stats) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return brcmf_if_get_iface_counter_stats(wdev_->netdev, out_stats);
}

zx_status_t WlanInterface::GetIfaceHistogramStats(wlan_fullmac_iface_histogram_stats_t* out_stats) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return brcmf_if_get_iface_histogram_stats(wdev_->netdev, out_stats);
}

void WlanInterface::StartCaptureFrames(const wlan_fullmac_start_capture_frames_req_t* req,
                                       wlan_fullmac_start_capture_frames_resp_t* resp) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_start_capture_frames(wdev_->netdev, req, resp);
  }
}

void WlanInterface::StopCaptureFrames() {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_stop_capture_frames(wdev_->netdev);
  }
}

zx_status_t WlanInterface::SetMulticastPromisc(bool enable) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return brcmf_if_set_multicast_promisc(wdev_->netdev, enable);
}

void WlanInterface::DataQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_data_queue_tx(wdev_->netdev, options, netbuf, completion_cb, cookie);
  } else {
    // We must fire the completion callback even if the device is being torn down.
    completion_cb(cookie, ZX_ERR_BAD_STATE, netbuf);
  }
}

void WlanInterface::SaeHandshakeResp(const wlan_fullmac_sae_handshake_resp_t* resp) {
  brcmf_if_sae_handshake_resp(wdev_->netdev, resp);
}

void WlanInterface::SaeFrameTx(const wlan_fullmac_sae_frame_t* frame) {
  brcmf_if_sae_frame_tx(wdev_->netdev, frame);
}

void WlanInterface::WmmStatusReq() {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_wmm_status_req(wdev_->netdev);
  }
}

void WlanInterface::OnLinkStateChanged(bool online) {
  if (device_->IsNetworkDeviceBus()) {
    std::shared_lock<std::shared_mutex> guard(lock_);
    SetPortOnline(online);
  }
}

uint32_t WlanInterface::PortGetMtu() { return kEthernetMtu; }

void WlanInterface::MacGetAddress(uint8_t out_mac[MAC_SIZE]) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  memcpy(out_mac, ndev_to_if(wdev_->netdev)->mac_addr, MAC_SIZE);
}

void WlanInterface::MacGetFeatures(features_t* out_features) {
  *out_features = {
      .multicast_filter_count = 0,
      .supported_modes = MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS,
  };
}

void WlanInterface::MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) {
  switch (mode) {
    case MODE_MULTICAST_FILTER:
      SetMulticastPromisc(false);
      break;
    case MODE_MULTICAST_PROMISCUOUS:
      SetMulticastPromisc(true);
      break;
    default:
      BRCMF_ERR("Unsupported MAC mode: %u", mode);
      break;
  }
}

}  // namespace brcmfmac
}  // namespace wlan
