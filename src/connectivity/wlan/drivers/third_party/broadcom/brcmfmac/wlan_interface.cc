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

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/logging.h>

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

void WlanInterface::BeginShuttingDown() { shutting_down_ = true; }

// static
wlan_info_mac_role_t WlanInterface::GetMacRoles(struct brcmf_pub* drvr) {
  wlan_info_mac_role_t mac_role = 0;
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_STA)) {
    mac_role |= WLAN_INFO_MAC_ROLE_CLIENT;
  }
  if (brcmf_feat_is_enabled(drvr, BRCMF_FEAT_AP)) {
    mac_role |= WLAN_INFO_MAC_ROLE_AP;
  }
  return mac_role;
}

// static
bool WlanInterface::IsPhyTypeSupported(struct brcmf_pub* drvr, wlan_info_phy_type_t phy_type) {
  uint32_t iovar_data;
  zx_status_t iovar_zx_status;

  switch (phy_type) {
    case WLAN_INFO_PHY_TYPE_DSSS:
    case WLAN_INFO_PHY_TYPE_CCK:
    case WLAN_INFO_PHY_TYPE_OFDM:  // and  WLAN_INFO_PHY_TYPE_ERP
      // Broadcom has mandatory support for DSSS, CCK, ERP, and OFDM. See b/158857812.
      return true;
    case WLAN_INFO_PHY_TYPE_HT:
      iovar_zx_status = brcmf_fil_iovar_int_get(drvr->iflist[0], "nmode", &iovar_data, nullptr);
      if (iovar_zx_status != ZX_OK) {
        BRCMF_DBG(INFO, "Failed to get iovar nmode. Assuming HT phy type not supported");
        return false;
      }
      return iovar_data;
    case WLAN_INFO_PHY_TYPE_VHT:
      iovar_zx_status = brcmf_fil_iovar_int_get(drvr->iflist[0], "vhtmode", &iovar_data, nullptr);
      if (iovar_zx_status != ZX_OK) {
        BRCMF_DBG(INFO, "Failed to get iovar vhtmode. Assuming VHT phy type not supported");
        return false;
      }
      return iovar_data;
    default:
      BRCMF_ERR("wlan_info_phy_type_t value %d not recognized", phy_type);
      return false;
  }
}

// static
wlan_info_phy_type_t WlanInterface::GetSupportedPhyTypes(struct brcmf_pub* drvr) {
  wlan_info_phy_type_t supported_phys = 0;
  wlan_info_phy_type_t phy_type_list[] = {WLAN_INFO_PHY_TYPE_DSSS, WLAN_INFO_PHY_TYPE_CCK,
                                          WLAN_INFO_PHY_TYPE_ERP,  WLAN_INFO_PHY_TYPE_OFDM,
                                          WLAN_INFO_PHY_TYPE_HT,   WLAN_INFO_PHY_TYPE_VHT};
  for (auto phy_type : phy_type_list) {
    if (IsPhyTypeSupported(drvr, phy_type)) {
      supported_phys |= phy_type;
    }
  }
  return supported_phys;
}

// static
wlan_info_driver_feature_t WlanInterface::GetSupportedDriverFeatures(struct brcmf_pub* drvr) {
  wlan_info_driver_feature_t driver_features = 0;

  if (brcmf_feat_is_enabled(drvr->iflist[0], BRCMF_FEAT_DFS)) {
    driver_features |= WLAN_INFO_DRIVER_FEATURE_DFS;
  }
  if (brcmf_feat_is_enabled(drvr->iflist[0], BRCMF_FEAT_PNO) ||
      brcmf_feat_is_enabled(drvr->iflist[0], BRCMF_FEAT_EPNO)) {
    driver_features |= WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD;
  }

  // The driver features associated with WLAN_INFO_DRIVER_FEATURE_RATE_SELECTION,
  // WLAN_INFO_DRIVER_FEATURE_SYNTH, WLAN_INFO_DRIVER_FEATURE_TX_STATUS_REPORT, and
  // WLAN_INFO_DRIVER_FEATURE_PROBE_RESP_OFFLOAD are not supported.

  return driver_features;
}

// TODO(chcl): Learn *all* capability flags from the firmware itself.
// See fxb/29107 and b/158857812.
// static
wlan_info_hardware_capability_t WlanInterface::GetSupportedHardwareCapabilities(
    struct brcmf_pub* drvr) {
  uint32_t iovar_data;
  zx_status_t iovar_zx_status;
  wlan_info_hardware_capability_t hardware_capability_flags = 0;

  // Short Preamble support is mandatory. See b/158857812.
  hardware_capability_flags |= WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE;

  // Enabled in AP mode by default when 802.11h is in the firmware capability string.
  if (brcmf_feat_is_enabled(drvr->iflist[0], BRCMF_FEAT_DOT11H)) {
    hardware_capability_flags |= WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT;
  }

  // Enabled by default on 11g so it is a capability of the PHY.
  hardware_capability_flags |= WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;

  // Radio Resource Management is enabled when 802.11k is included. This is indicated
  // by the "rrm" iovar.
  iovar_zx_status = brcmf_fil_iovar_int_get(drvr->iflist[0], "rrm", &iovar_data, nullptr);
  if (iovar_zx_status != ZX_OK) {
    BRCMF_DBG(INFO, "Failed to get iovar rrm. Assuming radio measurement not supported");
  } else if (iovar_data) {
    hardware_capability_flags |= WLAN_INFO_HARDWARE_CAPABILITY_RADIO_MSMT;
  }

  hardware_capability_flags |= WLAN_INFO_HARDWARE_CAPABILITY_SIMULTANEOUS_CLIENT_AP;

  return hardware_capability_flags;
}

// static
zx_status_t WlanInterface::Query(brcmf_pub* drvr, wlanphy_impl_info_t* out_info) {
  wlan_info_t info = {};
  std::memset(&info, 0, sizeof(info));

  // The default client iface at bsscfgidx 0 is always assumed to exist by the driver.
  if (!drvr->iflist[0]) {
    BRCMF_ERR("drvr->iflist[0] is NULL. This should never happen.");
    return false;
  }

  // Skip setting the info.mac_addr field since this is a PHY query. See fxb/53991

  info.mac_role = GetMacRoles(drvr);
  info.supported_phys = GetSupportedPhyTypes(drvr);

  info.driver_features = GetSupportedDriverFeatures(drvr);
  DebugDriverFeatureFlags(info.driver_features);

  info.caps = GetSupportedHardwareCapabilities(drvr);
  DebugHardwareCapabilityFlags(info.caps);

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
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  const unsigned char* code = country->alpha2;
  BRCMF_ERR("WlanInterface::SetCountry() %c%c", code[0], code[1]);
  return brcmf_set_country(drvr, country);
}

// static
zx_status_t WlanInterface::GetCountry(brcmf_pub* drvr, wlanphy_country_t* out_country) {
  return brcmf_get_country(drvr, out_country);
}

zx_status_t WlanInterface::ClearCountry(brcmf_pub* drvr) { return brcmf_clear_country(drvr); }

zx_status_t WlanInterface::Start(const wlanif_impl_ifc_protocol_t* ifc,
                                 zx_handle_t* out_sme_channel) {
  return brcmf_if_start(wdev_->netdev, ifc, out_sme_channel);
}

void WlanInterface::Stop() { brcmf_if_stop(wdev_->netdev); }

void WlanInterface::Query(wlanif_query_info_t* info) { brcmf_if_query(wdev_->netdev, info); }

void WlanInterface::StartScan(const wlanif_scan_req_t* req) {
  // TODO(58783): We sometimes see these requests after DdkAsyncRemove is called, which can cause
  // issues in the driver.
  if (!shutting_down_) {
    brcmf_if_start_scan(wdev_->netdev, req);
  }
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

WlanInterface::WlanInterface()
    : zx_device_(nullptr), wdev_(nullptr), device_(nullptr), shutting_down_(false) {}

WlanInterface::~WlanInterface() {}

}  // namespace brcmfmac
}  // namespace wlan
