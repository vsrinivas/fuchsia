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

#include <zircon/status.h>

#include <cstdio>
#include <cstring>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr zx_protocol_device_t kWlanInterfaceDeviceOps = {
    .version = DEVICE_OPS_VERSION,
    .release = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->DdkRelease(); },
};

wlanif_impl_protocol_ops_t wlan_interface_proto_ops = {
    .start =
        [](void* ctx, const wlanif_impl_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel) {
          return static_cast<WlanInterface*>(ctx)->Start(ifc, out_sme_channel);
        },
    .stop = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->Stop(); },
    .query =
        [](void* ctx, wlanif_query_info_t* info) {
          return static_cast<WlanInterface*>(ctx)->Query(info);
        },
    .start_scan =
        [](void* ctx, const wlanif_scan_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StartScan(req);
        },
    .join_req =
        [](void* ctx, const wlanif_join_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->JoinReq(req);
        },
    .auth_req =
        [](void* ctx, const wlanif_auth_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->AuthReq(req);
        },
    .auth_resp =
        [](void* ctx, const wlanif_auth_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->AuthResp(resp);
        },
    .deauth_req =
        [](void* ctx, const wlanif_deauth_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->DeauthReq(req);
        },
    .assoc_req =
        [](void* ctx, const wlanif_assoc_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->AssocReq(req);
        },
    .assoc_resp =
        [](void* ctx, const wlanif_assoc_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->AssocResp(resp);
        },
    .disassoc_req =
        [](void* ctx, const wlanif_disassoc_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->DisassocReq(req);
        },
    .reset_req =
        [](void* ctx, const wlanif_reset_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->ResetReq(req);
        },
    .start_req =
        [](void* ctx, const wlanif_start_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StartReq(req);
        },
    .stop_req =
        [](void* ctx, const wlanif_stop_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->StopReq(req);
        },
    .set_keys_req =
        [](void* ctx, const wlanif_set_keys_req* req) {
          return static_cast<WlanInterface*>(ctx)->SetKeysReq(req);
        },
    .del_keys_req =
        [](void* ctx, const wlanif_del_keys_req* req) {
          return static_cast<WlanInterface*>(ctx)->DelKeysReq(req);
        },
    .eapol_req =
        [](void* ctx, const wlanif_eapol_req_t* req) {
          return static_cast<WlanInterface*>(ctx)->EapolReq(req);
        },
    .stats_query_req = [](void* ctx) { return static_cast<WlanInterface*>(ctx)->StatsQueryReq(); },
    .start_capture_frames =
        [](void* ctx, const wlanif_start_capture_frames_req_t* req,
           wlanif_start_capture_frames_resp_t* resp) {
          return static_cast<WlanInterface*>(ctx)->StartCaptureFrames(req, resp);
        },
    .stop_capture_frames =
        [](void* ctx) { return static_cast<WlanInterface*>(ctx)->StopCaptureFrames(); },
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

zx_status_t WlanInterface::Create(Device* device, const char* name, wireless_dev* wdev,
                                  WlanInterface** out_interface) {
  zx_status_t status = ZX_OK;

  const auto ddk_remover = [](WlanInterface* interface) { interface->DdkAsyncRemove(); };
  std::unique_ptr<WlanInterface, decltype(ddk_remover)> interface(new WlanInterface(), ddk_remover);
  device_add_args device_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = interface.get(),
      .ops = &kWlanInterfaceDeviceOps,
      .proto_id = ZX_PROTOCOL_WLANIF_IMPL,
      .proto_ops = &wlan_interface_proto_ops,
      .flags = DEVICE_ADD_INVISIBLE,
  };
  if (device->DeviceAdd(&device_args, &interface->zx_device_) != ZX_OK) {
    delete interface.release();
    return status;
  }
  interface->device_ = device;
  interface->wdev_ = wdev;
  *out_interface = interface.get();

  device_make_visible(interface->zxdev(), nullptr);
  interface.release();  // This now has its lifecycle managed by the devhost.

  return ZX_OK;
}

zx_device_t* WlanInterface::zxdev() { return zx_device_; }

const zx_device_t* WlanInterface::zxdev() const { return zx_device_; }

wireless_dev* WlanInterface::wdev() { return wdev_; }

const wireless_dev* WlanInterface::wdev() const { return wdev_; }

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
zx_status_t WlanInterface::Query(brcmf_pub* drvr, wlanphy_impl_info_t* out_info) {
  wlan_info_t info = {};

  // TODO(sheu): find a good MAC address to return.
  std::memcpy(&info.mac_addr, drvr->iflist[0]->mac_addr, sizeof(info.mac_addr));
  info.mac_role = WLAN_INFO_MAC_ROLE_CLIENT | WLAN_INFO_MAC_ROLE_AP;
  info.supported_phys = 0x1f;  // WLAN_INFO_PHY_TYPE_;
  info.driver_features = WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD | WLAN_INFO_DRIVER_FEATURE_DFS |
                         WLAN_INFO_DRIVER_FEATURE_TEMP_DIRECT_SME_CHANNEL;
  info.caps = 0xf;  // WLAN_INFO_HARDWARE_CAPABILITY_;
  info.bands_count = 1;
  info.bands[0].band = WLAN_INFO_BAND_2GHZ;
  // TODO(cphoenix): Once this isn't temp/stub code anymore, remove unnecessary "= 0" lines.
  info.bands[0].ht_supported = false;
  info.bands[0].ht_caps.ht_capability_info = 0;
  info.bands[0].ht_caps.ampdu_params = 0;
  // info.bands[0].ht_caps.supported_mcs_set[ 16 entries ] = 0;
  info.bands[0].ht_caps.ht_ext_capabilities = 0;
  info.bands[0].ht_caps.tx_beamforming_capabilities = 0;
  info.bands[0].ht_caps.asel_capabilities = 0;
  info.bands[0].vht_supported = false;
  info.bands[0].vht_caps.vht_capability_info = 0;
  info.bands[0].vht_caps.supported_vht_mcs_and_nss_set = 0;
  // info.bands[0].basic_rates[ 12 entries ] = 0;
  info.bands[0].supported_channels.base_freq = 0;
  // info.bands[0].supported_channels.channels[ 64 entries ] = 0;

  out_info->wlan_info = info;
  return ZX_OK;
}

// static
zx_status_t WlanInterface::SetCountry(brcmf_pub* drvr, const wlanphy_country_t* country) {
  const unsigned char* code = country->alpha2;
  BRCMF_ERR("WlanInterface::SetCountry() %c%c\n", code[0], code[1]);
  return brcmf_set_country(drvr, country);
}

zx_status_t WlanInterface::Start(const wlanif_impl_ifc_protocol_t* ifc,
                                 zx_handle_t* out_sme_channel) {
  return brcmf_if_start(wdev_->netdev, ifc, out_sme_channel);
}

void WlanInterface::Stop() { brcmf_if_stop(wdev_->netdev); }

void WlanInterface::Query(wlanif_query_info_t* info) { brcmf_if_query(wdev_->netdev, info); }

void WlanInterface::StartScan(const wlanif_scan_req_t* req) {
  brcmf_if_start_scan(wdev_->netdev, req);
}

void WlanInterface::JoinReq(const wlanif_join_req_t* req) { brcmf_if_join_req(wdev_->netdev, req); }

void WlanInterface::AuthReq(const wlanif_auth_req_t* req) { brcmf_if_auth_req(wdev_->netdev, req); }

void WlanInterface::AuthResp(const wlanif_auth_resp_t* resp) {
  brcmf_if_auth_resp(wdev_->netdev, resp);
}

void WlanInterface::DeauthReq(const wlanif_deauth_req_t* req) {
  brcmf_if_deauth_req(wdev_->netdev, req);
}

void WlanInterface::AssocReq(const wlanif_assoc_req_t* req) {
  brcmf_if_assoc_req(wdev_->netdev, req);
}

void WlanInterface::AssocResp(const wlanif_assoc_resp_t* resp) {
  brcmf_if_assoc_resp(wdev_->netdev, resp);
}

void WlanInterface::DisassocReq(const wlanif_disassoc_req_t* req) {
  brcmf_if_disassoc_req(wdev_->netdev, req);
}

void WlanInterface::ResetReq(const wlanif_reset_req_t* req) {
  brcmf_if_reset_req(wdev_->netdev, req);
}

void WlanInterface::StartReq(const wlanif_start_req_t* req) {
  brcmf_if_start_req(wdev_->netdev, req);
}

void WlanInterface::StopReq(const wlanif_stop_req_t* req) { brcmf_if_stop_req(wdev_->netdev, req); }

void WlanInterface::SetKeysReq(const wlanif_set_keys_req_t* req) {
  brcmf_if_set_keys_req(wdev_->netdev, req);
}

void WlanInterface::DelKeysReq(const wlanif_del_keys_req_t* req) {
  brcmf_if_del_keys_req(wdev_->netdev, req);
}

void WlanInterface::EapolReq(const wlanif_eapol_req_t* req) {
  brcmf_if_eapol_req(wdev_->netdev, req);
}

void WlanInterface::StatsQueryReq() { brcmf_if_stats_query_req(wdev_->netdev); }

void WlanInterface::StartCaptureFrames(const wlanif_start_capture_frames_req_t* req,
                                       wlanif_start_capture_frames_resp_t* resp) {
  brcmf_if_start_capture_frames(wdev_->netdev, req, resp);
}

void WlanInterface::StopCaptureFrames() { brcmf_if_stop_capture_frames(wdev_->netdev); }

zx_status_t WlanInterface::SetMulticastPromisc(bool enable) {
  return brcmf_if_set_multicast_promisc(wdev_->netdev, enable);
}

void WlanInterface::DataQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  brcmf_if_data_queue_tx(wdev_->netdev, options, netbuf, completion_cb, cookie);
}

WlanInterface::WlanInterface() : zx_device_(nullptr), wdev_(nullptr), device_(nullptr) {}

WlanInterface::~WlanInterface() {}

}  // namespace brcmfmac
}  // namespace wlan
