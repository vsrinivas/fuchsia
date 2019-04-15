// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_INFRA_BSS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_INFRA_BSS_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/service.h>
#include <zircon/types.h>

#include <queue>

namespace wlan {

class ObjectId;
template <typename T>
class MlmeMsg;

// An infrastructure BSS which keeps track of its client and owned by the AP
// MLME.
class InfraBss : public BssInterface, public RemoteClient::Listener {
 public:
  InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
           const common::MacAddr& bssid, fbl::unique_ptr<Timer> timer);
  virtual ~InfraBss();

  // Starts the BSS. Beacons will be sent and incoming frames are processed.
  void Start(const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>&);
  // Stops the BSS. All incoming frames are dropped and Beacons are not sent
  // anymore.
  void Stop();
  bool IsStarted();

  // Entry point for ethernet and WLAN frames.
  void HandleAnyFrame(fbl::unique_ptr<Packet>);
  // Entry point for MLME messages except START-/STOP.request which are handled
  // in the `ApMlme`.
  zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg);

  zx_status_t ScheduleTimeout(wlan_tu_t tus, const common::MacAddr& client_addr,
                              TimeoutId* id) override;
  void CancelTimeout(TimeoutId id) override;
  zx_status_t HandleTimeout();

  // BssInterface implementation
  const common::MacAddr& bssid() const override;
  uint64_t timestamp() override;

  zx_status_t SendMgmtFrame(MgmtFrame<>&& mgmt_frame) override;
  zx_status_t SendDataFrame(DataFrame<>&& data_frame,
                            uint32_t flags = 0) override;
  zx_status_t DeliverEthernet(Span<const uint8_t> frame) override;

  uint32_t NextSns1(const common::MacAddr& addr) override;

  std::optional<DataFrame<LlcHeader>> EthToDataFrame(
      const EthFrame& eth_frame, bool needs_protection) override;
  void OnPreTbtt() override;
  void OnBcnTxComplete() override;

  bool IsRsn() const override;
  HtConfig Ht() const override;
  const Span<const SupportedRate> Rates() const override;

  wlan_channel_t Chan() const override { return chan_; }

 private:
  using ClientMap = std::unordered_map<common::MacAddr,
                                       fbl::unique_ptr<RemoteClientInterface>,
                                       common::MacAddrHasher>;

  void HandleEthFrame(EthFrame&&);
  void HandleAnyWlanFrame(fbl::unique_ptr<Packet>);
  void HandleAnyMgmtFrame(MgmtFrame<>&&);
  void HandleAnyDataFrame(DataFrame<>&&);
  void HandleAnyCtrlFrame(CtrlFrame<>&&);
  void HandleNewClientAuthAttempt(const MgmtFrameView<Authentication>&);
  zx_status_t HandleMlmeSetKeysReq(
      const MlmeMsg<::fuchsia::wlan::mlme::SetKeysRequest>&);

  bool HasClient(const common::MacAddr& client);
  RemoteClientInterface* GetClient(const common::MacAddr& addr);

  // Maximum number of group addressed packets buffered while at least one
  // client is dozing.
  // TODO(NET-687): Find good BU limit.
  static constexpr size_t kMaxGroupAddressedBu = 128;

  // RemoteClient::Listener implementation
  void HandleClientFailedAuth(const common::MacAddr& client_addr) override;
  void HandleClientDeauth(const common::MacAddr& client_addr) override;
  void HandleClientDisassociation(aid_t aid) override;
  void HandleClientBuChange(const common::MacAddr& client_addr, aid_t aid,
                            size_t bu_count) override;

  void StopTrackingClient(const common::MacAddr& client_addr);

  // Returns `true` if a frame with the given destination should get buffered.
  bool ShouldBufferFrame(const common::MacAddr& dest) const;
  zx_status_t BufferFrame(fbl::unique_ptr<Packet> packet);
  zx_status_t SendNextBu();

  const common::MacAddr bssid_;
  DeviceInterface* device_;
  fbl::unique_ptr<BeaconSender> bcn_sender_;
  zx_time_t started_at_;
  ClientMap clients_;
  SequenceManager seq_mgr_;
  // Queue which holds buffered non-GCR-SP frames when at least one client is
  // dozing.
  std::queue<fbl::unique_ptr<Packet>> bu_queue_;
  PsCfg ps_cfg_;
  wlan_channel_t chan_;
  // MLME-START.request holds all information required to correctly configure
  // and start a BSS.
  ::fuchsia::wlan::mlme::StartRequest start_req_;
  TimerManager<common::MacAddr> timer_mgr_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_INFRA_BSS_H_
