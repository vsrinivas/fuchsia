// Copyright (c) 2022 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/wlan_interface.h"

#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/data_plane.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"

namespace wlan::nxpfmac {

WlanInterface::WlanInterface(zx_device_t* parent, uint32_t iface_index, wlan_mac_role_t role,
                             DeviceContext* context, zx::channel&& mlme_channel)
    : WlanInterfaceDeviceType(parent),
      wlan::drivers::components::NetworkPort(context->data_plane_->NetDevIfcProto(), *this,
                                             static_cast<uint8_t>(iface_index)),
      role_(role),
      mlme_channel_(std::move(mlme_channel)),
      client_connection_(context->ioctl_adapter_, iface_index),
      scanner_(&fullmac_ifc_, context, iface_index),
      context_(context) {}

zx_status_t WlanInterface::Create(zx_device_t* parent, const char* name, uint32_t iface_index,
                                  wlan_mac_role_t role, DeviceContext* context,
                                  zx::channel&& mlme_channel, WlanInterface** out_interface) {
  std::unique_ptr<WlanInterface> interface(
      new WlanInterface(parent, iface_index, role, context, std::move(mlme_channel)));

  // Retrieve the MAC address before adding the DDK device. Otherwise the device can bind and
  // receive calls before the MAC address is ready.
  zx_status_t status = interface->RetrieveMacAddress();
  if (status != ZX_OK) {
    NXPF_ERR("Failed to get interface %u MAC address: %s", iface_index,
             zx_status_get_string(status));
    return status;
  }

  status = interface->DdkAdd(name);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to add fullmac device: %s", zx_status_get_string(status));
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
      NXPF_ERR("Unsupported role %u", role);
      return ZX_ERR_INVALID_ARGS;
  }

  interface->NetworkPort::Init(net_port_role);

  *out_interface = interface.release();  // This now has its lifecycle managed by the devhost.
  return ZX_OK;
}

void WlanInterface::DdkRelease() { delete this; }

zx_status_t WlanInterface::WlanFullmacImplStart(const wlan_fullmac_impl_ifc_protocol_t* ifc,
                                                zx::channel* out_mlme_channel) {
  std::lock_guard lock(mutex_);
  if (!mlme_channel_.is_valid()) {
    NXPF_ERR("%s IF already bound", __func__);
    return ZX_ERR_ALREADY_BOUND;
  }

  NXPF_INFO("Starting wlan_fullmac interface");
  fullmac_ifc_ = ::ddk::WlanFullmacImplIfcProtocolClient(ifc);
  is_up_ = true;

  ZX_DEBUG_ASSERT(out_mlme_channel != nullptr);
  *out_mlme_channel = std::move(mlme_channel_);
  return ZX_OK;
}

void WlanInterface::WlanFullmacImplStop() {}

#define WLAN_MAXRATE 108  /* in 500kbps units */
#define WLAN_RATE_1M 2    /* in 500kbps units */
#define WLAN_RATE_2M 4    /* in 500kbps units */
#define WLAN_RATE_5M5 11  /* in 500kbps units */
#define WLAN_RATE_11M 22  /* in 500kbps units */
#define WLAN_RATE_6M 12   /* in 500kbps units */
#define WLAN_RATE_9M 18   /* in 500kbps units */
#define WLAN_RATE_12M 24  /* in 500kbps units */
#define WLAN_RATE_18M 36  /* in 500kbps units */
#define WLAN_RATE_24M 48  /* in 500kbps units */
#define WLAN_RATE_36M 72  /* in 500kbps units */
#define WLAN_RATE_48M 96  /* in 500kbps units */
#define WLAN_RATE_54M 108 /* in 500kbps units */

void WlanInterface::WlanFullmacImplQuery(wlan_fullmac_query_info_t* info) {
  constexpr uint8_t kRates2g[] = {WLAN_RATE_1M,  WLAN_RATE_2M,  WLAN_RATE_5M5, WLAN_RATE_11M,
                                  WLAN_RATE_6M,  WLAN_RATE_9M,  WLAN_RATE_12M, WLAN_RATE_18M,
                                  WLAN_RATE_24M, WLAN_RATE_36M, WLAN_RATE_48M, WLAN_RATE_54M};
  constexpr uint8_t kRates5g[] = {WLAN_RATE_6M,  WLAN_RATE_9M,  WLAN_RATE_12M, WLAN_RATE_18M,
                                  WLAN_RATE_24M, WLAN_RATE_36M, WLAN_RATE_48M, WLAN_RATE_54M};
  static_assert(std::size(kRates2g) <= fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES);
  static_assert(std::size(kRates5g) <= fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES);

  constexpr uint8_t kChannels2g[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  constexpr uint8_t kChannels5g[] = {36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112,
                                     116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165};

  std::lock_guard lock(mutex_);
  info->role = role_;
  info->band_cap_count = 2;

  memcpy(info->sta_addr, mac_address_, sizeof(mac_address_));

  // 2.4 GHz
  wlan_fullmac_band_capability* band_cap = &info->band_cap_list[0];
  band_cap->band = WLAN_BAND_TWO_GHZ;
  band_cap->basic_rate_count = std::size(kRates2g);
  std::copy(std::begin(kRates2g), std::end(kRates2g), std::begin(band_cap->basic_rate_list));
  band_cap->operating_channel_count = std::size(kChannels2g);
  std::copy(std::begin(kChannels2g), std::end(kChannels2g),
            std::begin(band_cap->operating_channel_list));

  // 5 GHz
  band_cap = &info->band_cap_list[1];
  band_cap->band = WLAN_BAND_FIVE_GHZ;
  band_cap->basic_rate_count = std::size(kRates5g);
  std::copy(std::begin(kRates5g), std::end(kRates5g), std::begin(band_cap->basic_rate_list));
  band_cap->operating_channel_count = std::size(kChannels5g);
  std::copy(std::begin(kChannels5g), std::end(kChannels5g),
            std::begin(band_cap->operating_channel_list));
}

void WlanInterface::WlanFullmacImplQueryMacSublayerSupport(mac_sublayer_support_t* resp) {
  std::lock_guard lock(mutex_);
  *resp = mac_sublayer_support_t{
      .data_plane{.data_plane_type = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE},
      .device{.mac_implementation_type = MAC_IMPLEMENTATION_TYPE_FULLMAC},
  };
}

void WlanInterface::WlanFullmacImplQuerySecuritySupport(security_support_t* resp) {
  std::lock_guard lock(mutex_);
  resp->sae.sme_handler_supported = false;
  resp->sae.driver_handler_supported = false;
  resp->mfp.supported = false;
}

void WlanInterface::WlanFullmacImplQuerySpectrumManagementSupport(
    spectrum_management_support_t* resp) {
  std::lock_guard lock(mutex_);
  resp->dfs.supported = false;
}

void WlanInterface::WlanFullmacImplStartScan(const wlan_fullmac_scan_req_t* req) {
  // TODO(fxbug.dev/108408): Consider calculating a more accurate scan timeout based on the max
  // scan time per channel in the scan request.
  constexpr zx_duration_t kScanTimeout = ZX_MSEC(6000);
  zx_status_t status = scanner_.Scan(req, kScanTimeout);
  if (status != ZX_OK) {
    NXPF_ERR("Scan failed: %s", zx_status_get_string(status));
    // Error will have been reported through fullmac_ifc in scanner.
  }
}

void WlanInterface::WlanFullmacImplConnectReq(const wlan_fullmac_connect_req_t* req) {
  auto on_connect = [this](ClientConnection::StatusCode status) {
    wlan_fullmac_connect_confirm_t result = {.result_code = static_cast<uint16_t>(status)};
    fullmac_ifc_.ConnectConf(&result);
  };

  zx_status_t status = client_connection_.Connect(req->selected_bss.bssid,
                                                  req->selected_bss.channel.primary, on_connect);

  if (status != ZX_OK) {
    on_connect(ClientConnection::StatusCode::kJoinFailure);
  }
}

void WlanInterface::WlanFullmacImplReconnectReq(const wlan_fullmac_reconnect_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplAuthResp(const wlan_fullmac_auth_resp_t* resp) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplDeauthReq(const wlan_fullmac_deauth_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplAssocResp(const wlan_fullmac_assoc_resp_t* resp) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplDisassocReq(const wlan_fullmac_disassoc_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplResetReq(const wlan_fullmac_reset_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplStartReq(const wlan_fullmac_start_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplStopReq(const wlan_fullmac_stop_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplSetKeysReq(const wlan_fullmac_set_keys_req_t* req,
                                              wlan_fullmac_set_keys_resp_t* resp) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplDelKeysReq(const wlan_fullmac_del_keys_req_t* req) {
  NXPF_ERR("%s", __func__);
}

void WlanInterface::WlanFullmacImplEapolReq(const wlan_fullmac_eapol_req_t* req) {
  NXPF_ERR("%s", __func__);
}

zx_status_t WlanInterface::WlanFullmacImplGetIfaceCounterStats(
    wlan_fullmac_iface_counter_stats_t* out_stats) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t WlanInterface::WlanFullmacImplGetIfaceHistogramStats(
    wlan_fullmac_iface_histogram_stats_t* out_stats) {
  return ZX_ERR_NOT_SUPPORTED;
}

void WlanInterface::WlanFullmacImplStartCaptureFrames(
    const wlan_fullmac_start_capture_frames_req_t* req,
    wlan_fullmac_start_capture_frames_resp_t* resp) {}

void WlanInterface::WlanFullmacImplStopCaptureFrames() {}

zx_status_t WlanInterface::WlanFullmacImplSetMulticastPromisc(bool enable) {
  return ZX_ERR_NOT_SUPPORTED;
}

void WlanInterface::WlanFullmacImplDataQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                               ethernet_impl_queue_tx_callback completion_cb,
                                               void* cookie) {
  ZX_PANIC("DataQueueTx should not ever be called, we're using netdevice");
}

void WlanInterface::WlanFullmacImplSaeHandshakeResp(const wlan_fullmac_sae_handshake_resp_t* resp) {
}

void WlanInterface::WlanFullmacImplSaeFrameTx(const wlan_fullmac_sae_frame_t* frame) {}

void WlanInterface::WlanFullmacImplWmmStatusReq() {}

void WlanInterface::WlanFullmacImplOnLinkStateChanged(bool online) { SetPortOnline(online); }

uint32_t WlanInterface::PortGetMtu() { return 1500u; }

void WlanInterface::MacGetAddress(uint8_t out_mac[MAC_SIZE]) {
  memcpy(out_mac, mac_address_, MAC_SIZE);
}

void WlanInterface::MacGetFeatures(features_t* out_features) {
  *out_features = {
      .multicast_filter_count = MLAN_MAX_MULTICAST_LIST_SIZE,
      .supported_modes = MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS | MODE_PROMISCUOUS,
  };
}

void WlanInterface::MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) {
  IoctlRequest<mlan_ds_bss> request(MLAN_IOCTL_BSS, MLAN_ACT_SET, PortId(),
                                    {.sub_command = MLAN_OID_BSS_MULTICAST_LIST});

  auto& multicast_list = request.UserReq().param.multicast_list;

  switch (mode) {
    case MODE_MULTICAST_FILTER:
      multicast_list.mode = MLAN_MULTICAST_MODE;
      if (multicast_macs.size() > sizeof(multicast_list.mac_list)) {
        NXPF_ERR("Number of multicast macs %zu exceeds maximum value of %zu",
                 multicast_macs.size() / ETH_ALEN, std::size(multicast_list.mac_list));
        return;
      }
      memcpy(multicast_list.mac_list, multicast_macs.data(), multicast_macs.size());
      multicast_list.num_multicast_addr = static_cast<uint32_t>(multicast_macs.size() / ETH_ALEN);
      break;
    case MODE_MULTICAST_PROMISCUOUS:
      multicast_list.mode = MLAN_ALL_MULTI_MODE;
      break;
    case MODE_PROMISCUOUS:
      multicast_list.mode = MLAN_PROMISC_MODE;
      break;
    default:
      NXPF_ERR("Unsupported MAC mode %u", mode);
      return;
  }

  IoctlStatus io_status = context_->ioctl_adapter_->IssueIoctlSync(&request);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Failed to set mac mode: %d", io_status);
    return;
  }
}

zx_status_t WlanInterface::RetrieveMacAddress() {
  IoctlRequest<mlan_ds_bss> request(MLAN_IOCTL_BSS, MLAN_ACT_GET, PortId(),
                                    {.sub_command = MLAN_OID_BSS_MAC_ADDR});

  NXPF_INFO("Retrieving MAC address");
  IoctlStatus io_status = context_->ioctl_adapter_->IssueIoctlSync(&request);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Failed to perform get MAC ioctl: %d", io_status);
    return ZX_ERR_INTERNAL;
  }
  memcpy(mac_address_, request.UserReq().param.mac_addr, ETH_ALEN);
  return ZX_OK;
}

}  // namespace wlan::nxpfmac
