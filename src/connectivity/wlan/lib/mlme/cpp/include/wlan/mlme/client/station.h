// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_STATION_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_STATION_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <vector>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/moving_average.h>
#include <wlan/common/stats.h>
#include <wlan/mlme/assoc_context.h>
#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/client/client_interface.h>
#include <wlan/mlme/client/join_context.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/mac.h>

namespace wlan {

class Packet;

class Station : public ClientInterface {
 public:
  Station(DeviceInterface* device, wlan_client_mlme_config_t* mlme_config,
          TimerManager<TimeoutTarget>* timer_mgr, ChannelScheduler* chan_sched,
          JoinContext* join_ctx, wlan_client_mlme_t* rust_mlme);
  ~Station() = default;

  enum class WlanState {
    kIdle,
    kAuthenticating,
    kAuthenticated,
    kAssociated,
    // 802.1X's controlled port is not handled here.
  };

  zx_status_t HandleEthFrame(EthFrame&&) override;
  zx_status_t HandleWlanFrame(std::unique_ptr<Packet>) override;
  void HandleTimeout(zx::time now, TimeoutTarget target, TimeoutId timeout_id) override;
  zx_status_t Authenticate(::fuchsia::wlan::mlme::AuthenticationTypes auth_type,
                           uint32_t timeout) override;
  zx_status_t Deauthenticate(::fuchsia::wlan::mlme::ReasonCode reason_code) override;
  zx_status_t Associate(const ::fuchsia::wlan::mlme::AssociateRequest& req) override;
  zx_status_t SendEapolFrame(fbl::Span<const uint8_t> eapol_frame, const common::MacAddr& src,
                             const common::MacAddr& dst) override;
  zx_status_t SetKeys(fbl::Span<const ::fuchsia::wlan::mlme::SetKeyDescriptor> keys) override;
  void UpdateControlledPort(::fuchsia::wlan::mlme::ControlledPortState state) override;

  void PreSwitchOffChannel() override;
  void BackToMainChannel() override;

  ::fuchsia::wlan::stats::ClientMlmeStats stats() const override;
  void ResetStats() override;

  wlan_client_sta_t* GetRustClientSta() override;

 private:
  static constexpr size_t kAssocBcnCountTimeout = 20;
  static constexpr size_t kAutoDeauthBcnCountTimeout = 100;
  static constexpr zx::duration kOnChannelTimeAfterSend = zx::msec(500);

  void Reset();

  zx_status_t HandleMgmtFrame(MgmtFrame<>&&);
  zx_status_t HandleDataFrame(DataFrame<>&&);
  bool ShouldDropMgmtFrame(const MgmtFrameView<>&);
  void HandleBeacon(MgmtFrame<Beacon>&&);
  zx_status_t HandleAuthentication(MgmtFrame<Authentication>&&);
  zx_status_t HandleDeauthentication(MgmtFrame<Deauthentication>&&);
  zx_status_t HandleAssociationResponse(MgmtFrame<AssociationResponse>&&);
  zx_status_t HandleDisassociation(MgmtFrame<Disassociation>&&);
  zx_status_t HandleActionFrame(MgmtFrame<ActionFrame>&&);
  bool ShouldDropDataFrame(const DataFrameView<>&);
  zx_status_t HandleDataFrame(DataFrame<LlcHeader>&& frame);
  zx_status_t HandleAddBaRequest(const AddBaRequestFrame&);

  zx_status_t SendAddBaRequestFrame();

  zx_status_t SendCtrlFrame(std::unique_ptr<Packet> packet);
  zx_status_t SendMgmtFrame(std::unique_ptr<Packet> packet);
  zx_status_t SendDataFrame(std::unique_ptr<Packet> packet, uint32_t flags = 0);
  zx_status_t SendPsPoll();
  zx_status_t SendWlan(std::unique_ptr<Packet> packet, uint32_t flags = 0);
  void DumpDataFrame(const DataFrameView<>&);

  zx::time deadline_after_bcn_period(size_t bcn_count);
  zx::duration FullAutoDeauthDuration();

  // Returns the STA's own MAC address.
  const common::MacAddr& self_addr() const { return device_->GetState()->address(); }

  bool IsQosReady() const;

  uint8_t GetTid();
  uint8_t GetTid(const EthFrame& frame);
  zx_status_t SetAssocContext(const MgmtFrameView<AssociationResponse>& resp);
  std::optional<AssocContext> BuildAssocCtx(const MgmtFrameView<AssociationResponse>& frame,
                                            const wlan_channel_t& join_chan,
                                            wlan_info_phy_type_t join_phy,
                                            uint16_t listen_interval);

  zx_status_t NotifyAssocContext();

  DeviceInterface* device_;
  wlan_client_mlme_config_t* mlme_config_;
  ClientStation rust_client_;
  TimerManager<TimeoutTarget>* timer_mgr_;
  ChannelScheduler* chan_sched_;
  JoinContext* join_ctx_;
  wlan_client_mlme_t* rust_mlme_;

  WlanState state_ = WlanState::kIdle;
  TimeoutId auth_timeout_;
  TimeoutId assoc_timeout_;
  TimeoutId signal_report_timeout_;
  TimeoutId auto_deauth_timeout_;
  // The remaining time we'll wait for a beacon before deauthenticating (while
  // we are on channel) Note: Off-channel time does not count against
  // `remaining_auto_deauth_timeout_`
  zx::duration remaining_auto_deauth_timeout_ = zx::duration::infinite();
  // The last time we re-calculated the `remaining_auto_deauth_timeout_`
  // Note: During channel switching, `auto_deauth_last_accounted_` is set to the
  // timestamp
  //       we go back on channel (to make computation easier).
  zx::time auto_deauth_last_accounted_;

  common::MovingAverageDbm<20> avg_rssi_dbm_;
  eapol::PortState controlled_port_ = eapol::PortState::kBlocked;

  common::WlanStats<common::ClientMlmeStats, ::fuchsia::wlan::stats::ClientMlmeStats> stats_;
  AssocContext assoc_ctx_{};
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_STATION_H_
