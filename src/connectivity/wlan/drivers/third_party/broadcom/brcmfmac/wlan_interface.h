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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WLAN_INTERFACE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WLAN_INTERFACE_H_

#include <fidl/fuchsia.factory.wlan/cpp/wire.h>
#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include <memory>
#include <shared_mutex>

#include <wlan/drivers/components/network_port.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"

struct wireless_dev;

namespace wlan {
namespace brcmfmac {

class Device;

class WlanInterface : public wlan::drivers::components::NetworkPort,
                      public wlan::drivers::components::NetworkPort::Callbacks {
 public:
  // Static factory function.  The returned instance is unowned, since its lifecycle is managed by
  // the devhost.
  static zx_status_t Create(Device* device, const char* name, wireless_dev* wdev,
                            wlan_mac_role_t role, WlanInterface** out_interface);

  // Accessors.
  void set_wdev(wireless_dev* wdev);
  wireless_dev* take_wdev();

  // Device operations.
  void DdkAsyncRemove(fit::callback<void()>&& on_remove);
  void DdkRelease();

  static zx_status_t GetSupportedMacRoles(
      struct brcmf_pub* drvr,
      fuchsia_wlan_common::wire::WlanMacRole
          out_supported_mac_roles_list[fuchsia_wlan_common::wire::kMaxSupportedMacRoles],
      uint8_t* out_supported_mac_roles_count);
  static zx_status_t SetCountry(brcmf_pub* drvr, const wlanphy_country_t* country);
  // Reads the currently configured `country` from the firmware.
  static zx_status_t GetCountry(brcmf_pub* drvr, wlanphy_country_t* out_country);
  static zx_status_t ClearCountry(brcmf_pub* drvr);

  // ZX_PROTOCOL_WLAN_FULLMAC_IMPL operations.
  zx_status_t Start(const wlan_fullmac_impl_ifc_protocol_t* ifc, zx_handle_t* out_mlme_channel);
  void Stop();
  void Query(wlan_fullmac_query_info_t* info);
  void QueryMacSublayerSupport(mac_sublayer_support_t* resp);
  void QuerySecuritySupport(security_support_t* resp);
  void QuerySpectrumManagementSupport(spectrum_management_support_t* resp);
  void StartScan(const wlan_fullmac_scan_req_t* req);
  void ConnectReq(const wlan_fullmac_connect_req_t* req);
  void ReconnectReq(const wlan_fullmac_reconnect_req_t* req);
  void AuthResp(const wlan_fullmac_auth_resp_t* resp);
  void DeauthReq(const wlan_fullmac_deauth_req_t* req);
  void AssocResp(const wlan_fullmac_assoc_resp_t* resp);
  void DisassocReq(const wlan_fullmac_disassoc_req_t* req);
  void ResetReq(const wlan_fullmac_reset_req_t* req);
  void StartReq(const wlan_fullmac_start_req_t* req);
  void StopReq(const wlan_fullmac_stop_req_t* req);
  void SetKeysReq(const wlan_fullmac_set_keys_req_t* req, wlan_fullmac_set_keys_resp_t* resp);
  void DelKeysReq(const wlan_fullmac_del_keys_req_t* req);
  void EapolReq(const wlan_fullmac_eapol_req_t* req);
  void StatsQueryReq();
  zx_status_t GetIfaceCounterStats(wlan_fullmac_iface_counter_stats_t* out_stats);
  zx_status_t GetIfaceHistogramStats(wlan_fullmac_iface_histogram_stats_t* out_stats);
  void StartCaptureFrames(const wlan_fullmac_start_capture_frames_req_t* req,
                          wlan_fullmac_start_capture_frames_resp_t* resp);
  void StopCaptureFrames();
  zx_status_t SetMulticastPromisc(bool enable);
  void DataQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                   ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  void SaeHandshakeResp(const wlan_fullmac_sae_handshake_resp_t* resp);
  void SaeFrameTx(const wlan_fullmac_sae_frame_t* frame);
  void WmmStatusReq();
  void OnLinkStateChanged(bool online);

 protected:
  // NetworkPort::Callbacks implementation
  uint32_t PortGetMtu() override;
  void MacGetAddress(uint8_t out_mac[6]) override;
  void MacGetFeatures(features_t* out_features) override;
  void MacSetMode(mode_t mode, cpp20::span<const uint8_t> multicast_macs) override;

 private:
  WlanInterface(const network_device_ifc_protocol_t& proto, uint8_t port_id);

  zx_device_t* zxdev();
  const zx_device_t* zxdev() const;

  zx_device_t* zx_device_;
  std::shared_mutex lock_;
  wireless_dev* wdev_;               // lock_ is used as a RW lock on wdev_
  fit::callback<void()> on_remove_;  // lock_ is also used as a RW lock on on_remove_
  Device* device_;
};
}  // namespace brcmfmac
}  // namespace wlan
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_WLAN_INTERFACE_H_
