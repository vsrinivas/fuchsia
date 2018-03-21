// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/sequence.h>

#include <fuchsia/cpp/wlan_mlme.h>

#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <wlan/common/moving_average.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

class Packet;
class Timer;

class Station : public FrameHandler {
   public:
    Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer);

    enum class PortState : bool { kBlocked = false, kOpen = true };

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

    zx_status_t HandleMlmeMessage(const wlan_mlme::Method& method) override;
    zx_status_t HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) override;
    zx_status_t HandleMlmeAuthReq(const wlan_mlme::AuthenticateRequest& req) override;
    zx_status_t HandleMlmeDeauthReq(const wlan_mlme::DeauthenticateRequest& req) override;
    zx_status_t HandleMlmeAssocReq(const wlan_mlme::AssociateRequest& req) override;
    zx_status_t HandleMlmeEapolReq(const wlan_mlme::EapolRequest& req) override;
    zx_status_t HandleMlmeSetKeysReq(const wlan_mlme::SetKeysRequest& req) override;

    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                             const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                       const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAssociationResponse(const ImmutableMgmtFrame<AssociationResponse>& frame,
                                          const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaRequestFrame(const ImmutableMgmtFrame<AddBaRequestFrame>& frame,
                                        const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaResponseFrame(const ImmutableMgmtFrame<AddBaResponseFrame>& frame,
                                         const wlan_rx_info& rxinfo) override;

    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleNullDataFrame(const ImmutableDataFrame<NilHeader>& frame,
                                    const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDataFrame(const ImmutableDataFrame<LlcHeader>& frame,
                                const wlan_rx_info_t& rxinfo) override;

    zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) override;
    zx_status_t HandleTimeout();

    zx_status_t PreChannelChange(wlan_channel_t chan);
    zx_status_t PostChannelChange();

    void DumpDataFrame(const ImmutableDataFrame<LlcHeader>& frame);

    const Timer& timer() const { return *timer_; }

   private:
    zx_status_t SendAddBaRequestFrame();

    zx_status_t SetPowerManagementMode(bool ps_mode);
    zx_status_t SendPsPoll();

    zx::time deadline_after_bcn_period(zx_duration_t tus);

    bool IsHTReady() const;
    bool IsCbw40RxReady() const;
    bool IsCbw40TxReady() const;
    HtCapabilities BuildHtCapabilities() const;
    uint8_t GetTid();
    uint8_t GetTid(const ImmutableBaseFrame<EthernetII>& frame);

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
    common::MovingAverage<uint8_t, uint16_t, 20> avg_rssi_;
    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
    PortState controlled_port_ = PortState::kBlocked;

    wlan_channel_t join_chan_;
};

}  // namespace wlan
