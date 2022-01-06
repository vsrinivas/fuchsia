// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/cpp/binding.h>

#include <memory>
#include <mutex>

namespace wlanif {

class EthDevice {
 public:
  EthDevice();
  ~EthDevice();

  // wlan_fullmac_protocol_t (ethernet_impl_protocol -> wlan_fullmac_impl_protocol)
  zx_status_t EthStart(const ethernet_ifc_protocol_t* ifc);
  void EthStop();
  void EthQueueTx(wlan_fullmac_impl_protocol_t* wlan_fullmac_impl_proto, uint32_t options,
                  ethernet_netbuf_t* netbuf, ethernet_impl_queue_tx_callback completion_cb,
                  void* cookie);
  zx_status_t EthSetParam(wlan_fullmac_impl_protocol_t* wlan_fullmac_impl_proto, uint32_t param,
                          int32_t value, const void* data, size_t data_size);

  // wlan_fullmac_impl_ifc (wlanif-impl -> ethernet_ifc_t)
  void EthRecv(const uint8_t* data, size_t length, uint32_t flags);

  void SetEthernetStatus(wlan_fullmac_impl_protocol_t* wlan_fullmac_impl_proto, bool online);
  bool IsEthernetOnline();

 private:
  std::mutex lock_;

  bool eth_started_ __TA_GUARDED(lock_) = false;
  bool eth_online_ __TA_GUARDED(lock_) = false;
  ethernet_ifc_protocol_t ethernet_ifc_ __TA_GUARDED(lock_) = {};
};

class Device : public ::fuchsia::wlan::mlme::MLME {
 public:
  Device(zx_device_t* device, wlan_fullmac_impl_protocol_t wlan_fullmac_impl_proto);
  ~Device();

  zx_status_t Bind();

  // zx_protocol_device_t
  void Unbind();
  void Release();

  // MLME implementation (::fuchsia::wlan::mlme -> wlan_fullmac_impl)
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
  void GetIfaceCounterStats(GetIfaceCounterStatsCallback cb) override;
  void GetIfaceHistogramStats(GetIfaceHistogramStatsCallback cb) override;
  void ListMinstrelPeers(ListMinstrelPeersCallback cb) override;
  void GetMinstrelStats(::fuchsia::wlan::mlme::MinstrelStatsRequest req,
                        GetMinstrelStatsCallback cb) override;
  void SendMpOpenAction(::fuchsia::wlan::mlme::MeshPeeringOpenAction req) override;
  void SendMpConfirmAction(::fuchsia::wlan::mlme::MeshPeeringConfirmAction req) override;
  void MeshPeeringEstablished(::fuchsia::wlan::mlme::MeshPeeringParams params) override;
  void GetMeshPathTableReq(::fuchsia::wlan::mlme::GetMeshPathTableRequest req,
                           GetMeshPathTableReqCallback cb) override;
  void StartCaptureFrames(::fuchsia::wlan::mlme::StartCaptureFramesRequest req,
                          StartCaptureFramesCallback cb) override;
  void StopCaptureFrames() override;
  void SaeHandshakeResp(::fuchsia::wlan::mlme::SaeHandshakeResponse resp) override;
  void SaeFrameTx(::fuchsia::wlan::mlme::SaeFrame frame) override;
  void WmmStatusReq() override;
  // This fn calls into ethernet_ifc_t rather than wlan_fullmac_impl
  void SetControlledPort(::fuchsia::wlan::mlme::SetControlledPortRequest req) override;

  // FinalizeAssociationReq is ignored because it is for SoftMAC drivers ONLY.
  void FinalizeAssociationReq(::fuchsia::wlan::mlme::NegotiatedCapabilities cap) override {}

  // wlan_fullmac_impl_ifc (wlan_fullmac_impl -> ::fuchsia::wlan::mlme)
  void OnScanResult(const wlan_fullmac_scan_result_t* result);
  void OnScanEnd(const wlan_fullmac_scan_end_t* result);
  void JoinConf(const wlan_fullmac_join_confirm_t* resp);
  void AuthenticateConf(const wlan_fullmac_auth_confirm_t* resp);
  void AuthenticateInd(const wlan_fullmac_auth_ind_t* ind);
  void DeauthenticateConf(const wlan_fullmac_deauth_confirm_t* resp);
  void DeauthenticateInd(const wlan_fullmac_deauth_indication_t* ind);
  void AssociateConf(const wlan_fullmac_assoc_confirm_t* resp);
  void AssociateInd(const wlan_fullmac_assoc_ind_t* ind);
  void DisassociateConf(const wlan_fullmac_disassoc_confirm_t* resp);
  void DisassociateInd(const wlan_fullmac_disassoc_indication_t* ind);
  void StartConf(const wlan_fullmac_start_confirm_t* resp);
  void StopConf(const wlan_fullmac_stop_confirm_t* resp);
  void EapolConf(const wlan_fullmac_eapol_confirm_t* resp);
  void SignalReport(const wlan_fullmac_signal_report_indication_t* ind);
  void EapolInd(const wlan_fullmac_eapol_indication_t* ind);
  void StatsQueryResp(const wlan_fullmac_stats_query_response_t* resp);
  void RelayCapturedFrame(const wlan_fullmac_captured_frame_result* result);
  void OnChannelSwitched(const wlan_fullmac_channel_switch_info_t* ind);
  void OnPmkAvailable(const wlan_fullmac_pmk_info_t* info);
  void SaeHandshakeInd(const wlan_fullmac_sae_handshake_ind_t* ind);
  void SaeFrameRx(const wlan_fullmac_sae_frame_t* ind);
  void OnWmmStatusResp(zx_status_t status, const wlan_wmm_params_t* params);

  // ethernet_impl_protocol_t (ethernet_impl_protocol -> wlan_fullmac_impl_protocol)
  zx_status_t EthStart(const ethernet_ifc_protocol_t* ifc);
  void EthStop();
  zx_status_t EthQuery(uint32_t options, ethernet_info_t* info);
  void EthQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                  ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthSetParam(uint32_t param, int32_t value, const void* data, size_t data_size);

  // wlan_fullmac_impl_ifc (wlanif-impl -> ethernet_ifc_t)
  void EthRecv(const uint8_t* data, size_t length, uint32_t flags);

  zx_status_t Connect(zx::channel request);

 private:
  zx_status_t AddDevice();
  // Suffixes in these function names have the following meanings:
  //   Locked: Assumes lock_ already acquired prior to this function being called.
  //   Unlocked: Acquires lock_ at the beginning at the beginning of the function.
  //   BindingChecked: Assumes binding_ is not equal to nullptr.
  void SendScanEndLockedBindingChecked(::fuchsia::wlan::mlme::ScanEnd scan_end)
      __TA_REQUIRES(lock_);
  void SendScanEndUnlocked(::fuchsia::wlan::mlme::ScanEnd scan_end) __TA_EXCLUDES(lock_);
  void SendStartConfLocked(wlan_start_result_t result_code) __TA_REQUIRES(lock_);

  std::mutex lock_;
  std::mutex get_iface_histogram_stats_lock_;

  zx_device_t* parent_ = nullptr;
  zx_device_t* device_ = nullptr;

  wlan_fullmac_impl_protocol_t wlan_fullmac_impl_;
  EthDevice eth_device_;

  bool protected_bss_ __TA_GUARDED(lock_) = false;

  wlan_fullmac_query_info query_info_;

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fidl::Binding<::fuchsia::wlan::mlme::MLME>> binding_ __TA_GUARDED(lock_);
};

}  // namespace wlanif

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEVICE_H_
