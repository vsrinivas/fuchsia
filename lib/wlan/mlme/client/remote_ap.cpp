// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/mlme/client/remote_ap.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

namespace wlan {

// RemoteAp implementation.

RemoteAp::RemoteAp(DeviceInterface* device, fbl::unique_ptr<Timer> timer,
                   const wlan_mlme::BSSDescription& bss)
    : device_(device), timer_(fbl::move(timer)) {
    ZX_DEBUG_ASSERT(timer_ != nullptr);
    debugclt("[ap] [%s] spawned\n", bssid_.ToString().c_str());

    bss_ = wlan_mlme::BSSDescription::New();
    bss.Clone(bss_.get());
    bssid_.Set(bss_->bssid.data());
    wlan_channel_t bss_chan{
        .primary = bss_->chan.primary,
        .cbw = static_cast<uint8_t>(bss_->chan.cbw),
    };
    bss_chan_ = SanitizeChannel(bss_chan);

    MoveToState(fbl::make_unique<InitState>(this));
}

RemoteAp::~RemoteAp() {
    // Terminate the current state.
    state_->OnExit();
    state_.reset();

    debugclt("[ap] [%s] destroyed\n", bssid_.ToString().c_str());
}

void RemoteAp::MoveToState(fbl::unique_ptr<BaseState> to) {
    ZX_DEBUG_ASSERT(to != nullptr);
    if (to == nullptr) {
        auto from_name = (state_ == nullptr ? "(init)" : state_->name());
        errorf("attempt to transition to a nullptr from state: %s\n", from_name);
        return;
    }

    if (state_ != nullptr) { state_->OnExit(); }

    debugclt("[ap] [%s] %s -> %s\n", bssid_.ToString().c_str(), state_->name(), to->name());
    state_ = fbl::move(to);
    state_->OnEnter();
}

zx_status_t RemoteAp::StartTimer(zx::time deadline) {
    CancelTimer();
    return timer_->SetTimer(deadline);
}

zx_status_t RemoteAp::CancelTimer() {
    return timer_->CancelTimer();
}

zx::time RemoteAp::CreateTimerDeadline(wlan_tu_t tus) {
    return timer_->Now() + WLAN_TU(tus);
}

bool RemoteAp::IsDeadlineExceeded(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

void RemoteAp::HandleTimeout() {
    state_->HandleTimeout();
}

zx_status_t RemoteAp::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // TODO(hahnr): Add stats support.

    // Drop all management frames from other BSS.
    return (bssid_ != hdr.addr3 ? ZX_ERR_STOP : ZX_OK);
}

// TODO(NET-449): Move this logic to policy engine
wlan_channel_t RemoteAp::SanitizeChannel(wlan_channel_t chan) {
    // Validation and sanitization
    if (!common::IsValidChan(chan)) {
        wlan_channel_t chan_sanitized = chan;
        chan_sanitized.cbw = common::GetValidCbw(chan);
        errorf("Wlanstack attempts to configure an invalid channel: %s. Falling back to %s\n",
               common::ChanStr(chan).c_str(), common::ChanStr(chan_sanitized).c_str());
        chan = chan_sanitized;
    }

    // Override with CBW40 support
    if (IsCbw40RxReady()) {
        wlan_channel_t chan_override = chan;
        chan_override.cbw = CBW40;
        chan_override.cbw = common::GetValidCbw(chan_override);

        infof("CBW40 Rx is ready. Overriding the channel configuration from %s to %s\n",
              common::ChanStr(chan).c_str(), common::ChanStr(chan_override).c_str());
        chan = chan_override;
    }
    return chan;
}

bool RemoteAp::IsCbw40RxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    return true;
}

bool RemoteAp::IsCbw40TxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
    return false;
}

// BaseState implementation.

template <typename S, typename... Args> void RemoteAp::BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "Invalid State type");
    ap_->MoveToState(fbl::make_unique<S>(ap_, std::forward<Args>(args)...));
}

// InitState implementation.

InitState::InitState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

void InitState::OnExit() {
    ap_->CancelTimer();
}

zx_status_t InitState::HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) {
    debugfn();

    const auto& chan = ap_->bss_chan();
    debugclt("[ap] [%s] setting channel to %s\n", ap_->bssid_str(), common::ChanStr(chan).c_str());
    auto status = ap_->device()->SetChannel(chan);
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not set wlan channel to %s (status %d)\n", ap_->bssid_str(),
               common::ChanStr(chan).c_str(), status);
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    wlan_tu_t tu = ap_->bss().beacon_period * req.join_failure_timeout;
    join_deadline_ = ap_->CreateTimerDeadline(tu);
    status = ap_->StartTimer(join_deadline_);
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not set join timer: %d\n", ap_->bssid_str(), status);
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    // TODO(hahnr): Update when other BSS types are supported.
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = true,
    };
    ap_->bssid().CopyTo(cfg.bssid);
    ap_->device()->ConfigureBss(&cfg);
    return status;
}

zx_status_t InitState::HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                                    const wlan_rx_info_t& rxinfo) {
    MoveToJoinedState();
    return ZX_OK;
}

zx_status_t InitState::HandleProbeResponse(const ImmutableMgmtFrame<ProbeResponse>& frame,
                                           const wlan_rx_info_t& rxinfo) {
    MoveToJoinedState();
    return ZX_OK;
}

void InitState::MoveToJoinedState() {
    debugfn();

    // Beacon or ProbeResponse received; cancel deadline and move to joined-state.
    join_deadline_ = zx::time();
    service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::SUCCESS);
    MoveToState<JoinedState>();
}

void InitState::HandleTimeout() {
    if (ap_->IsDeadlineExceeded(join_deadline_)) {
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    }
}

// JoinedState implementation.

JoinedState::JoinedState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

}  // namespace wlan