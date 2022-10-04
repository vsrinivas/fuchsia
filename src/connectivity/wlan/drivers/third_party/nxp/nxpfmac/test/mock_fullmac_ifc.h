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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_FULLMAC_IFC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_FULLMAC_IFC_H_

#include <fuchsia/hardware/wlan/fullmac/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace wlan::nxpfmac {

class MockFullmacIfc : public ::ddk::WlanFullmacImplIfcProtocol<MockFullmacIfc> {
 public:
  MockFullmacIfc() : proto_{&wlan_fullmac_impl_ifc_protocol_ops_, this} {}
  const wlan_fullmac_impl_ifc_protocol_t* proto() { return &proto_; }

  void WlanFullmacImplIfcOnScanResult(const wlan_fullmac_scan_result_t* result) {
    on_scan_result.Call(result);
  }
  void WlanFullmacImplIfcOnScanEnd(const wlan_fullmac_scan_end_t* end) { on_scan_end.Call(end); }
  void WlanFullmacImplIfcConnectConf(const wlan_fullmac_connect_confirm_t* resp) {
    on_connect_conf.Call(resp);
  }
  void WlanFullmacImplIfcAuthInd(const wlan_fullmac_auth_ind_t* ind) { on_auth_ind_conf.Call(ind); }
  void WlanFullmacImplIfcDeauthConf(const wlan_fullmac_deauth_confirm_t* resp) {}
  void WlanFullmacImplIfcDeauthInd(const wlan_fullmac_deauth_indication_t* ind) {
    on_deauth_ind_conf.Call(ind);
  }
  void WlanFullmacImplIfcAssocInd(const wlan_fullmac_assoc_ind_t* ind) {
    on_assoc_ind_conf.Call(ind);
  }
  void WlanFullmacImplIfcDisassocConf(const wlan_fullmac_disassoc_confirm_t* resp) {}
  void WlanFullmacImplIfcDisassocInd(const wlan_fullmac_disassoc_indication_t* ind) {
    on_disassoc_ind_conf.Call(ind);
  }
  void WlanFullmacImplIfcStartConf(const wlan_fullmac_start_confirm_t* resp) {
    on_start_conf.Call(resp);
  }
  void WlanFullmacImplIfcStopConf(const wlan_fullmac_stop_confirm_t* resp) {
    on_stop_conf.Call(resp);
  }
  void WlanFullmacImplIfcEapolConf(const wlan_fullmac_eapol_confirm_t* resp) {}
  void WlanFullmacImplIfcOnChannelSwitch(const wlan_fullmac_channel_switch_info_t* ind) {}
  void WlanFullmacImplIfcSignalReport(const wlan_fullmac_signal_report_indication_t* ind) {}
  void WlanFullmacImplIfcEapolInd(const wlan_fullmac_eapol_indication_t* ind) {}
  void WlanFullmacImplIfcRelayCapturedFrame(const wlan_fullmac_captured_frame_result_t* result) {}
  void WlanFullmacImplIfcOnPmkAvailable(const wlan_fullmac_pmk_info_t* info) {}
  void WlanFullmacImplIfcSaeHandshakeInd(const wlan_fullmac_sae_handshake_ind_t* ind) {}
  void WlanFullmacImplIfcSaeFrameRx(const wlan_fullmac_sae_frame_t* frame) {}
  void WlanFullmacImplIfcOnWmmStatusResp(zx_status_t status, const wlan_wmm_params_t* wmm_params) {}
  void WlanFullmacImplIfcDataRecv(const uint8_t* data_buffer, size_t data_size, uint32_t flags) {}

  mock_function::MockFunction<void, const wlan_fullmac_scan_result_t*> on_scan_result;
  mock_function::MockFunction<void, const wlan_fullmac_scan_end_t*> on_scan_end;
  mock_function::MockFunction<void, const wlan_fullmac_connect_confirm_t*> on_connect_conf;
  mock_function::MockFunction<void, const wlan_fullmac_start_confirm_t*> on_start_conf;
  mock_function::MockFunction<void, const wlan_fullmac_stop_confirm_t*> on_stop_conf;
  mock_function::MockFunction<void, const wlan_fullmac_auth_ind_t*> on_auth_ind_conf;
  mock_function::MockFunction<void, const wlan_fullmac_deauth_indication_t*> on_deauth_ind_conf;
  mock_function::MockFunction<void, const wlan_fullmac_assoc_ind_t*> on_assoc_ind_conf;
  mock_function::MockFunction<void, const wlan_fullmac_disassoc_indication_t*> on_disassoc_ind_conf;

 private:
  wlan_fullmac_impl_ifc_protocol_t proto_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_FULLMAC_IFC_H_
