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

#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
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
    .join_req =
        [](void* ctx, const wlan_fullmac_join_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->JoinReq(req);
        },
    .auth_req =
        [](void* ctx, const wlan_fullmac_auth_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->AuthReq(req);
        },
    .auth_resp =
        [](void* ctx, const wlan_fullmac_auth_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->AuthResp(resp);
        },
    .deauth_req =
        [](void* ctx, const wlan_fullmac_deauth_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->DeauthReq(req);
        },
    .assoc_req =
        [](void* ctx, const wlan_fullmac_assoc_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->AssocReq(req);
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
    .stats_query_req = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->StatsQueryReq(); },
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
};

}  // namespace

WlanInterface::WlanInterface() : zx_device_(nullptr), wdev_(nullptr), device_(nullptr) {}

WlanInterface::~WlanInterface() {}

zx_status_t WlanInterface::Create(Device* device, const char* name, wireless_dev* wdev,
                                  WlanInterface** out_interface) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<WlanInterface> interface(new WlanInterface());
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

void WlanInterface::DdkAsyncRemove() {
  zx_device_t* const device = zx_device_;
  if (device == nullptr) {
    return;
  }
  zx_device_ = nullptr;
  device_->DeviceAsyncRemove(device);
}

void WlanInterface::DdkRelease() { delete this; }

// static
zx_status_t WlanInterface::GetSupportedMacRoles(
    struct brcmf_pub* drvr,
    wlan_mac_role_t out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* out_supported_mac_roles_count) {
  // The default client iface at bsscfgidx 0 is always assumed to exist by the driver.
  if (!drvr->iflist[0]) {
    BRCMF_ERR("drvr->iflist[0] is NULL. This should never happen.");
    return ZX_ERR_INTERNAL;
  }

  size_t len = 0;
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_STA)) {
    out_supported_mac_roles_list[len] = WLAN_MAC_ROLE_CLIENT;
    ++len;
  }
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_AP)) {
    out_supported_mac_roles_list[len] = WLAN_MAC_ROLE_AP;
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

void WlanInterface::JoinReq(const wlan_fullmac_join_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_join_req(wdev_->netdev, req);
  }
}

void WlanInterface::AuthReq(const wlan_fullmac_auth_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_auth_req(wdev_->netdev, req);
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

void WlanInterface::AssocReq(const wlan_fullmac_assoc_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_assoc_req(wdev_->netdev, req);
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

void WlanInterface::StatsQueryReq() {
  std::shared_lock<std::shared_mutex> guard(lock_);
  if (wdev_ != nullptr) {
    brcmf_if_stats_query_req(wdev_->netdev);
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

}  // namespace brcmfmac
}  // namespace wlan
