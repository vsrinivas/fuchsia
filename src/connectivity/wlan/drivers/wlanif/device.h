// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <mutex>

#include <ddk/driver.h>
#include <ddk/protocol/wlanif.h>

namespace wlanif {

class Device : public ::fuchsia::wlan::mlme::MLME {
 public:
  Device(zx_device_t* device, wlanif_impl_protocol_t wlanif_impl_proto);
  ~Device();

  zx_status_t Bind();

  // ETHERNET_IMPL zx_protocol_device_t
  void EthUnbind();
  void EthRelease();

  // wlanif_protocol_t (::fuchsia::wlan::mlme -> wlanif-impl)
  void StartScan(::fuchsia::wlan::mlme::ScanRequest req) override;
  void JoinReq(::fuchsia::wlan::mlme::JoinRequest req) override;
  void AuthenticateReq(::fuchsia::wlan::mlme::AuthenticateRequest req) override;
  void AuthenticateResp(::fuchsia::wlan::mlme::AuthenticateResponse resp) override;
  void DeauthenticateReq(::fuchsia::wlan::mlme::DeauthenticateRequest req) override;
  void AssociateReq(::fuchsia::wlan::mlme::AssociateRequest req) override;
  void AssociateResp(::fuchsia::wlan::mlme::AssociateResponse resp) override;
  void DisassociateReq(::fuchsia::wlan::mlme::DisassociateRequest req) override;
  void ResetReq(::fuchsia::wlan::mlme::ResetRequest req) override;
  void StartReq(::fuchsia::wlan::mlme::StartRequest req) override;
  void StopReq(::fuchsia::wlan::mlme::StopRequest req) override;
  void SetKeysReq(::fuchsia::wlan::mlme::SetKeysRequest req) override;
  void DeleteKeysReq(::fuchsia::wlan::mlme::DeleteKeysRequest req) override;
  void EapolReq(::fuchsia::wlan::mlme::EapolRequest req) override;
  void QueryDeviceInfo(QueryDeviceInfoCallback cb) override;
  void StatsQueryReq() override;
  void ListMinstrelPeers(ListMinstrelPeersCallback cb) override;
  void GetMinstrelStats(::fuchsia::wlan::mlme::MinstrelStatsRequest req,
                        GetMinstrelStatsCallback cb) override;
  void SendMpOpenAction(::fuchsia::wlan::mlme::MeshPeeringOpenAction req) override;
  void SetControlledPort(::fuchsia::wlan::mlme::SetControlledPortRequest req) override;
  void SendMpConfirmAction(::fuchsia::wlan::mlme::MeshPeeringConfirmAction req) override;
  void MeshPeeringEstablished(::fuchsia::wlan::mlme::MeshPeeringParams params) override;
  void GetMeshPathTableReq(::fuchsia::wlan::mlme::GetMeshPathTableRequest req,
                           GetMeshPathTableReqCallback cb) override;
  void StartCaptureFrames(::fuchsia::wlan::mlme::StartCaptureFramesRequest req,
                          StartCaptureFramesCallback cb) override;
  void StopCaptureFrames() override;
  void SaeHandshakeResp(::fuchsia::wlan::mlme::SaeHandshakeResponse resp) override;
  void SaeFrameTx(::fuchsia::wlan::mlme::SaeFrame frame) override;

  // FinalizeAssociationReq is ignored because it is for SoftMAC drivers ONLY.
  void FinalizeAssociationReq(::fuchsia::wlan::mlme::NegotiatedCapabilities cap) override {}

  // wlanif_impl_ifc (wlanif-impl -> ::fuchsia::wlan::mlme)
  void OnScanResult(const wlanif_scan_result_t* result);
  void OnScanEnd(const wlanif_scan_end_t* result);
  void JoinConf(const wlanif_join_confirm_t* resp);
  void AuthenticateConf(const wlanif_auth_confirm_t* resp);
  void AuthenticateInd(const wlanif_auth_ind_t* ind);
  void DeauthenticateConf(const wlanif_deauth_confirm_t* resp);
  void DeauthenticateInd(const wlanif_deauth_indication_t* ind);
  void AssociateConf(const wlanif_assoc_confirm_t* resp);
  void AssociateInd(const wlanif_assoc_ind_t* ind);
  void DisassociateConf(const wlanif_disassoc_confirm_t* resp);
  void DisassociateInd(const wlanif_disassoc_indication_t* ind);
  void StartConf(const wlanif_start_confirm_t* resp);
  void StopConf(const wlanif_stop_confirm_t* resp);
  void EapolConf(const wlanif_eapol_confirm_t* resp);
  void SignalReport(const wlanif_signal_report_indication_t* ind);
  void EapolInd(const wlanif_eapol_indication_t* ind);
  void StatsQueryResp(const wlanif_stats_query_response_t* resp);
  void RelayCapturedFrame(const wlanif_captured_frame_result* result);
  void OnChannelSwitched(const wlanif_channel_switch_info_t* ind);
  void OnPmkAvailable(const wlanif_pmk_info_t* info);

  // wlanif_protocol_t (ethernet_impl_protocol -> wlanif_impl_protocol)
  zx_status_t EthStart(const ethernet_ifc_protocol_t* ifc);
  void EthStop();
  zx_status_t EthQuery(uint32_t options, ethernet_info_t* info);
  void EthQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                  ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthSetParam(uint32_t param, int32_t value, const void* data, size_t data_size);

  // wlanif_impl_ifc (wlanif-impl -> ethernet_ifc_t)
  void EthRecv(const void* data, size_t length, uint32_t flags);

  zx_status_t Connect(zx::channel request);

 private:
  zx_status_t AddEthDevice();
  void SetEthernetStatusLocked(bool online) __TA_REQUIRES(lock_);
  void SetEthernetStatusUnlocked(bool online);

  std::mutex lock_;

  zx_device_t* parent_ = nullptr;
  zx_device_t* ethdev_ = nullptr;  // ETHERNET_IMPL

  wlanif_impl_protocol_t wlanif_impl_;

  bool protected_bss_ __TA_GUARDED(lock_) = false;

  bool eth_started_ __TA_GUARDED(lock_) = false;
  bool eth_online_ __TA_GUARDED(lock_) = false;
  ethernet_ifc_protocol_t ethernet_ifc_ __TA_GUARDED(lock_);

  wlanif_query_info query_info_;

  async::Loop loop_;
  fidl::Binding<::fuchsia::wlan::mlme::MLME> binding_ __TA_GUARDED(lock_);
};

}  // namespace wlanif

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_
