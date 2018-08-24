// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/client/channel_scheduler.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer_manager.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/moving_average.h>
#include <wlan/common/stats.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#include <vector>

namespace wlan {

namespace wlan_mlme = wlan_mlme;

class Packet;

// Information defined only within a context of association
// Beware the subtle interpretation of each field: they are designed to
// reflect the parameters safe to use within an association
// Many parameters do not distinguish Rx capability from Tx capability.
// In those cases, a capability is commonly applied to both Rx and Tx.
// Some parameters are distinctively for Rx only, and some are Tx only.
struct AssocContext {
    // TODO(porce): Move association-related variables of class Station to here
    zx::time ts_start;  // timestamp of the beginning of the association

    common::MacAddr bssid;
    CapabilityInfo cap;
    uint16_t aid;

    // Negotiated configurations
    // This is an outcome of intersection of capabilities and configurations.
    std::vector<SupportedRate> supported_rates;
    std::vector<SupportedRate> ext_supported_rates;

    // Rx MCS Bitmask in Supported MCS Set field represents the set of MCS
    // the peer can receive at from this device, considering this device's Tx capability.
    bool has_ht_cap;
    HtCapabilities ht_cap;

    bool has_ht_op;
    HtOperation ht_op;

    bool has_vht_cap;
    VhtCapabilities vht_cap;

    bool has_vht_op;
    VhtOperation vht_op;

    bool is_ht = false;
    bool is_cbw40_rx = false;
    bool is_cbw40_tx = false;
};

// TODO(NET-1322): Replace by a FIDL struct
// AssocConfig stores the per-association configuration on top of
// what associations peers are mutually capable of.
// The configuration is an instruction from SME to MLME.
struct AssocConfig {
    wlan_mlme::CBW cbw;
};

class Station {
   public:
    Station(DeviceInterface* device, TimerManager&& timer_mgr, ChannelScheduler* chan_sched);

    enum class WlanState {
        kUnjoined,
        kJoined,
        kAuthenticating,
        kAuthenticated,
        kAssociated,
        // 802.1X's controlled port is not handled here.
    };

    void Reset();

    const common::MacAddr* bssid() const {
        // TODO(porce): Distinguish cases
        // (1) if no Bss Descriptor came down from SME.
        // (2) if bssid_ is uninitlized.
        // (3) if bssid_ is kZeroMac.
        if (!bss_) { return nullptr; }
        return &bssid_;
    }

    uint16_t aid() const { return aid_; }

    wlan_channel_t GetBssChan() const {
        ZX_DEBUG_ASSERT(bss_ != nullptr);

        return wlan_channel_t{
            .primary = bss_->chan.primary,
            .cbw = static_cast<uint8_t>(bss_->chan.cbw),
        };
    }

    wlan_channel_t GetJoinChan() const { return join_chan_; }

    wlan_channel_t GetDeviceChan() const {
        ZX_DEBUG_ASSERT(device_ != nullptr);
        ZX_DEBUG_ASSERT(device_->GetState());
        return device_->GetState()->channel();
    }

    zx_status_t SendKeepAliveResponse();

    zx_status_t HandleAnyMlmeMsg(const BaseMlmeMsg&);
    zx_status_t HandleAnyFrame(fbl::unique_ptr<Packet>);
    zx_status_t HandleTimeout();

    void PreSwitchOffChannel();
    void BackToMainChannel();

    ::fuchsia::wlan::stats::ClientMlmeStats stats() const;
    void ResetStats();

   private:
    static constexpr size_t kAssocBcnCountTimeout = 20;
    static constexpr size_t kSignalReportBcnCountTimeout = 10;
    static constexpr size_t kAutoDeauthBcnCountTimeout = 50;
    static constexpr zx::duration kOnChannelTimeAfterSend = zx::msec(500);

    zx_status_t HandleEthFrame(EthFrame&&);
    zx_status_t HandleAnyWlanFrame(fbl::unique_ptr<Packet>);
    zx_status_t HandleAnyMgmtFrame(MgmtFrame<>&&);
    zx_status_t HandleAnyDataFrame(DataFrame<>&&);
    bool ShouldDropMgmtFrame(const MgmtFrameView<>&);
    zx_status_t HandleBeacon(MgmtFrame<Beacon>&&);
    zx_status_t HandleAuthentication(MgmtFrame<Authentication>&&);
    zx_status_t HandleDeauthentication(MgmtFrame<Deauthentication>&&);
    zx_status_t HandleAssociationResponse(MgmtFrame<AssociationResponse>&&);
    zx_status_t HandleDisassociation(MgmtFrame<Disassociation>&&);
    zx_status_t HandleActionFrame(MgmtFrame<ActionFrame>&&);
    bool ShouldDropDataFrame(const DataFrameView<>&);
    zx_status_t HandleNullDataFrame(DataFrame<NullDataHdr>&& frame);
    zx_status_t HandleDataFrame(DataFrame<LlcHeader>&& frame);
    zx_status_t HandleLlcFrame(const FrameView<LlcHeader>& llc_frame, size_t llc_payload_len,
                               const common::MacAddr& src, const common::MacAddr& dest);
    zx_status_t HandleAmsduFrame(DataFrame<AmsduSubframeHeader>&&);
    zx_status_t HandleAddBaRequest(const AddBaRequestFrame&);

    zx_status_t HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req);
    zx_status_t HandleMlmeAuthReq(const MlmeMsg<wlan_mlme::AuthenticateRequest>& req);
    zx_status_t HandleMlmeDeauthReq(const MlmeMsg<wlan_mlme::DeauthenticateRequest>& req);
    zx_status_t HandleMlmeAssocReq(const MlmeMsg<wlan_mlme::AssociateRequest>& req);
    zx_status_t HandleMlmeEapolReq(const MlmeMsg<wlan_mlme::EapolRequest>& req);
    zx_status_t HandleMlmeSetKeysReq(const MlmeMsg<wlan_mlme::SetKeysRequest>& req);

    zx_status_t SendAddBaRequestFrame();

    // Send a non-data frame
    zx_status_t SendNonData(fbl::unique_ptr<Packet> packet);
    zx_status_t SetPowerManagementMode(bool ps_mode);
    zx_status_t SendPsPoll();
    zx_status_t SendDeauthFrame(wlan_mlme::ReasonCode reason_code);
    void DumpDataFrame(const DataFrameView<>&);

    zx::time deadline_after_bcn_period(size_t bcn_count);
    zx::duration FullAutoDeauthDuration();

    // Returns the STA's own MAC address.
    const common::MacAddr& self_addr() const { return device_->GetState()->address(); }

    bool IsCbw40Rx() const;
    bool IsQosReady() const;

    CapabilityInfo OverrideCapability(CapabilityInfo cap) const;
    zx_status_t OverrideHtCapability(HtCapabilities* htc) const;
    uint8_t GetTid();
    uint8_t GetTid(const EthFrame& frame);
    zx_status_t SetAssocContext(const MgmtFrameView<AssociationResponse>& resp);
    zx_status_t NotifyAssocContext();

    DeviceInterface* device_;
    TimerManager timer_mgr_;
    ChannelScheduler* chan_sched_;
    wlan_mlme::BSSDescriptionPtr bss_;
    common::MacAddr bssid_;
    Sequence seq_;

    WlanState state_ = WlanState::kUnjoined;
    TimedEvent join_timeout_;
    TimedEvent auth_timeout_;
    TimedEvent assoc_timeout_;
    TimedEvent signal_report_timeout_;
    TimedEvent auto_deauth_timeout_;
    // The remaining time we'll wait for a beacon before deauthenticating (while we are on channel)
    // Note: Off-channel time does not count against `remaining_auto_deauth_timeout_`
    zx::duration remaining_auto_deauth_timeout_ = zx::duration::infinite();
    // The last time we re-calculated the `remaining_auto_deauth_timeout_`
    // Note: During channel switching, `auto_deauth_last_accounted_` is set to the timestamp
    //       we go back on channel (to make computation easier).
    zx::time auto_deauth_last_accounted_;
    uint16_t aid_ = 0;
    common::MovingAverageDbm<20> avg_rssi_dbm_;
    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
    eapol::PortState controlled_port_ = eapol::PortState::kBlocked;

    wlan_channel_t join_chan_;
    common::WlanStats<common::ClientMlmeStats, ::fuchsia::wlan::stats::ClientMlmeStats> stats_;
    AssocContext assoc_ctx_{};
    AssocConfig assoc_cfg_{};
};

const wlan_band_info_t* FindBand(const wlan_info_t& ifc_info, bool is_5ghz);
zx_status_t ParseAssocRespIe(const uint8_t* ie_chains, size_t ie_chains_len,
                             AssocContext* assoc_ctx);
AssocContext ToAssocContext(const wlan_info_t& ifc_info, const wlan_channel_t join_chan);
void SetAssocCtxSuppRates(const AssocContext& from_ap, const AssocContext& from_client,
                          std::vector<SupportedRate>* supp_rates,
                          std::vector<SupportedRate>* ext_rates);

}  // namespace wlan
