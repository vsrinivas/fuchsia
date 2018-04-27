// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/remote_ap.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer.h>

namespace wlan {

// RemoteAp implementation.

RemoteAp::RemoteAp(DeviceInterface* device, fbl::unique_ptr<Timer> timer,
                   const common::MacAddr& bssid)
    : device_(device), timer_(fbl::move(timer)), bssid_(bssid) {
    ZX_DEBUG_ASSERT(timer_ != nullptr);
    debugclt("[ap] [%s] spawned\n", bssid_.ToString().c_str());

    MoveToState(fbl::make_unique<UnjoinedState>(this));
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

// BaseState implementation.

template <typename S, typename... Args> void RemoteAp::BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "Invalid State type");
    ap_->MoveToState(fbl::make_unique<S>(ap_, std::forward<Args>(args)...));
}

// UnjoinedState implementation.

UnjoinedState::UnjoinedState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

zx_status_t UnjoinedState::HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) {
    debugfn();
    // TODO(hahnr): Implement
    return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace wlan