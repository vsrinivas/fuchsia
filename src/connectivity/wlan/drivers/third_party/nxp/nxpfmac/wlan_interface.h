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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WLAN_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WLAN_INTERFACE_H_

#include <fuchsia/hardware/wlan/fullmac/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <mutex>

#include <ddktl/device.h>
#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/network_port.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/client_connection.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/key_ring.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/scanner.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/softap.h"

namespace wlan::nxpfmac {

struct DeviceContext;
class WlanInterface;
using WlanInterfaceDeviceType = ::ddk::Device<WlanInterface>;

class WlanInterface : public WlanInterfaceDeviceType,
                      public ::ddk::WlanFullmacImplProtocol<WlanInterface, ::ddk::base_protocol>,
                      public ClientConnectionIfc,
                      public SoftApIfc,
                      public wlan::drivers::components::NetworkPort,
                      public wlan::drivers::components::NetworkPort::Callbacks {
 public:
  // Static factory function.  The returned instance is unowned, since its lifecycle is managed by
  // the devhost.
  static zx_status_t Create(zx_device_t* parent, const char* name, uint32_t iface_index,
                            wlan_mac_role_t role, DeviceContext* context,
                            const uint8_t mac_address[ETH_ALEN], zx::channel&& mlme_channel,
                            WlanInterface** out_interface);

  // Initiate an async remove call. The provided callback will be called once DdkRelease is called
  // as part of the removal. Note that when `on_remove` is called the WlanInterface object is
  // already destroyed and should not be referenced.
  void Remove(fit::callback<void()>&& on_remove);

  // Device operations.
  void DdkRelease();

  void OnEapolResponse(wlan::drivers::components::Frame&& frame);
  void OnEapolTransmitted(zx_status_t status, const uint8_t* dst_addr);

  // ZX_PROTOCOL_WLAN_FULLMAC_IMPL operations.
  zx_status_t WlanFullmacImplStart(const wlan_fullmac_impl_ifc_protocol_t* ifc,
                                   zx::channel* out_mlme_channel);
  void WlanFullmacImplStop();
  void WlanFullmacImplQuery(wlan_fullmac_query_info_t* info);
  void WlanFullmacImplQueryMacSublayerSupport(mac_sublayer_support_t* resp);
  void WlanFullmacImplQuerySecuritySupport(security_support_t* resp);
  void WlanFullmacImplQuerySpectrumManagementSupport(spectrum_management_support_t* resp);
  void WlanFullmacImplStartScan(const wlan_fullmac_scan_req_t* req);
  void WlanFullmacImplConnectReq(const wlan_fullmac_connect_req_t* req);
  void WlanFullmacImplReconnectReq(const wlan_fullmac_reconnect_req_t* req);
  void WlanFullmacImplAuthResp(const wlan_fullmac_auth_resp_t* resp);
  void WlanFullmacImplDeauthReq(const wlan_fullmac_deauth_req_t* req);
  void WlanFullmacImplAssocResp(const wlan_fullmac_assoc_resp_t* resp);
  void WlanFullmacImplDisassocReq(const wlan_fullmac_disassoc_req_t* req);
  void WlanFullmacImplResetReq(const wlan_fullmac_reset_req_t* req);
  void WlanFullmacImplStartReq(const wlan_fullmac_start_req_t* req);
  void WlanFullmacImplStopReq(const wlan_fullmac_stop_req_t* req);
  void WlanFullmacImplSetKeysReq(const wlan_fullmac_set_keys_req_t* req,
                                 wlan_fullmac_set_keys_resp_t* resp);
  void WlanFullmacImplDelKeysReq(const wlan_fullmac_del_keys_req_t* req);
  void WlanFullmacImplEapolReq(const wlan_fullmac_eapol_req_t* req);
  void WlanFullmacImplStatsQueryReq();
  zx_status_t WlanFullmacImplGetIfaceCounterStats(wlan_fullmac_iface_counter_stats_t* out_stats);
  zx_status_t WlanFullmacImplGetIfaceHistogramStats(
      wlan_fullmac_iface_histogram_stats_t* out_stats);
  void WlanFullmacImplStartCaptureFrames(const wlan_fullmac_start_capture_frames_req_t* req,
                                         wlan_fullmac_start_capture_frames_resp_t* resp);
  void WlanFullmacImplStopCaptureFrames();
  zx_status_t WlanFullmacImplSetMulticastPromisc(bool enable);
  void WlanFullmacImplDataQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                  ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  void WlanFullmacImplSaeHandshakeResp(const wlan_fullmac_sae_handshake_resp_t* resp);
  void WlanFullmacImplSaeFrameTx(const wlan_fullmac_sae_frame_t* frame);
  void WlanFullmacImplWmmStatusReq();
  void WlanFullmacImplOnLinkStateChanged(bool online);

  // ClientConnectionIfc implementation.
  void OnDisconnectEvent(uint16_t reason_code) override;
  void SignalQualityIndication(int8_t rssi, int8_t snr) override;

  // SoftApIfc implementation.
  void OnStaConnectEvent(uint8_t* sta_mac_addr, uint8_t* ies, uint32_t ie_length) override;
  void OnStaDisconnectEvent(uint8_t* sta_mac_addr, uint16_t reason_code) override;

  // NetworkPort::Callbacks implementation
  uint32_t PortGetMtu() override;
  void MacGetAddress(uint8_t out_mac[6]) override;
  void MacGetFeatures(features_t* out_features) override;
  void MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) override;

 private:
  explicit WlanInterface(zx_device_t* parent, uint32_t iface_index, wlan_mac_role_t role,
                         DeviceContext* context, const uint8_t mac_address[ETH_ALEN],
                         zx::channel&& mlme_channel);

  zx_status_t SetMacAddressInFw();
  void ConfirmDeauth() __TA_EXCLUDES(fullmac_ifc_mutex_);
  void ConfirmDisassoc(zx_status_t status) __TA_EXCLUDES(fullmac_ifc_mutex_);
  void ConfirmConnectReq(ClientConnection::StatusCode status, const uint8_t* ies = nullptr,
                         size_t ies_len = 0) __TA_EXCLUDES(fullmac_ifc_mutex_);

  wlan_mac_role_t role_;
  uint32_t iface_index_;
  zx::channel mlme_channel_;

  fit::callback<void()> on_remove_ __TA_GUARDED(mutex_);

  KeyRing key_ring_;
  ClientConnection client_connection_ __TA_GUARDED(mutex_);
  Scanner scanner_ __TA_GUARDED(mutex_);

  SoftAp soft_ap_ __TA_GUARDED(mutex_);
  DeviceContext* context_ = nullptr;

  ::ddk::WlanFullmacImplIfcProtocolClient fullmac_ifc_ __TA_GUARDED(fullmac_ifc_mutex_);
  std::mutex fullmac_ifc_mutex_;

  uint8_t mac_address_[ETH_ALEN] = {};

  bool is_up_ __TA_GUARDED(mutex_) = false;
  std::mutex mutex_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_WLAN_INTERFACE_H_
