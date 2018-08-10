// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/sequence.h>
#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/moving_average.h>
#include <wlan/common/stats.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

namespace wlan_mlme = wlan_mlme;

class Packet;
class Timer;

// Information defined only within a context of association
struct AssocContext {
    // TODO(porce): Move association-related variables of class Station to here
    zx::time ts_start;  // timestamp of the beginning of the association

    common::MacAddr bssid;
    CapabilityInfo cap;
    uint16_t aid;

    // Negotiated configurations
    // This is an outcome of intersection of capabilities and configurations.
    std::vector<uint8_t> supported_rates;
    std::vector<uint8_t> ext_supported_rates;

    bool has_ht_cap;
    HtCapabilities ht_cap;
    bool has_ht_op;
    HtOperation ht_op;
    bool has_vht_cap;
    VhtCapabilities vht_cap;
    bool has_vht_op;
    VhtOperation vht_op;
};

class Station {
   public:
    Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer);

    enum class WlanState {
        // State 0
        kUnjoined,

        // State 1
        kUnauthenticated,

        // State 2
        kAuthenticated,
        // State 3/4
        // TODO(tkilbourn): distinguish between states where 802.1X ports are blocked
        kAssociated,
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

    zx_status_t PreChannelChange(wlan_channel_t chan);
    zx_status_t PostChannelChange();

    const Timer& timer() const { return *timer_; }
    ::fuchsia::wlan::stats::ClientMlmeStats stats() const;

   private:
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
    zx_status_t HandleLlcFrame(const LlcHeader& llc_frame, size_t llc_frame_len,
                               const common::MacAddr& dest, const common::MacAddr& src);
    zx_status_t HandleAmsduFrame(DataFrame<AmsduSubframeHeader>&&);
    zx_status_t HandleAddBaRequest(const AddBaRequestFrame&);

    zx_status_t HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req);
    zx_status_t HandleMlmeAuthReq(const MlmeMsg<wlan_mlme::AuthenticateRequest>& req);
    zx_status_t HandleMlmeDeauthReq(const MlmeMsg<wlan_mlme::DeauthenticateRequest>& req);
    zx_status_t HandleMlmeAssocReq(const MlmeMsg<wlan_mlme::AssociateRequest>& req);
    zx_status_t HandleMlmeEapolReq(const MlmeMsg<wlan_mlme::EapolRequest>& req);
    zx_status_t HandleMlmeSetKeysReq(const MlmeMsg<wlan_mlme::SetKeysRequest>& req);

    zx_status_t SendAddBaRequestFrame();

    zx_status_t SetPowerManagementMode(bool ps_mode);
    zx_status_t SendPsPoll();
    void DumpDataFrame(const DataFrameView<>&);

    zx::time deadline_after_bcn_period(size_t bcn_count);

    bool IsHTReady() const;
    bool IsCbw40RxReady() const;
    bool IsCbw40TxReady() const;
    bool IsQosReady() const;
    bool IsAmsduRxReady() const;

    zx_status_t BuildMcsSet(SupportedMcsSet* mcs_set) const;
    zx_status_t BuildHtCapabilities(HtCapabilities* htc) const;
    uint8_t GetTid();
    uint8_t GetTid(const EthFrame& frame);
    zx_status_t SetAssocContext(const MgmtFrameView<AssociationResponse>& resp);

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    wlan_mlme::BSSDescriptionPtr bss_;
    common::MacAddr bssid_;
    Sequence seq_;

    WlanState state_ = WlanState::kUnjoined;
    zx::time join_timeout_;
    zx::time auth_timeout_;
    zx::time assoc_timeout_;
    zx::time signal_report_timeout_;
    zx::time last_seen_;
    uint16_t aid_ = 0;
    common::MovingAverageDbm<20> avg_rssi_dbm_;
    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
    eapol::PortState controlled_port_ = eapol::PortState::kBlocked;

    wlan_channel_t join_chan_;
    common::WlanStats<common::ClientMlmeStats, ::fuchsia::wlan::stats::ClientMlmeStats> stats_;
    AssocContext assoc_ctx_{};
};

const wlan_band_info_t* FindBand(const wlan_info_t& ifc_info, bool is_5ghz);
zx_status_t ParseAssocRespIe(const uint8_t* ie_chains, size_t ie_chains_len,
                             AssocContext* assoc_ctx);

}  // namespace wlan
