// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "bss_interface.h"
#include "device_interface.h"
#include "frame_handler.h"
#include "fsm.h"
#include "timer.h"

#include <zircon/types.h>

namespace wlan {

class RemoteClient;

class BaseState : public fsm::StateInterface, public FrameHandler {
   public:
    BaseState(RemoteClient* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void HandleTimeout() {}

    template <typename S, typename... Args> void MoveToState(Args&&... args);

   protected:
    RemoteClient* const client_;
};

class DeauthenticatedState : public BaseState {
   public:
    DeauthenticatedState(RemoteClient* client);

    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
};

class AuthenticatedState : public BaseState {
   public:
    AuthenticatedState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout() override;

    zx_status_t HandleAssociationRequest(const ImmutableMgmtFrame<AssociationRequest>& frame,
                                         const wlan_rx_info_t& rxinfo) override;
    // TODO(hahnr): Move into DeauthenticatedState when Deauthentication frame was received.

   private:
    static constexpr zx_duration_t kAuthenticationTimeoutTu = 1800000;  // 30min
    zx_time_t auth_timeout_ = 0;
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(RemoteClient* client, uint16_t aid);

    void OnExit() override;

    zx_status_t HandleAssociationRequest(const ImmutableMgmtFrame<AssociationRequest>& frame,
                                         const wlan_rx_info_t& rxinfo) override;

    // TODO(hahnr): Move into AuthenticatedState when Disassociation frame was received.
    // TODO(hahnr): Move into DeauthenticatedState when Deauthentication frame was received.

   private:
    const uint16_t aid_;
};

class RemoteClient : public fsm::StateMachine<BaseState>, public FrameHandler {
   public:
    RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                 const common::MacAddr& addr);

    void HandleTimeout();

    zx_status_t HandleAnyFrame() override;

    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);

    // Note: There can only ever by one timer running at a time.
    // TODO(hahnr): Evolve this to support multiple timeouts at the same time.
    zx_time_t StartTimer(zx_duration_t tus);
    bool HasTimerTriggered(zx_time_t deadline);
    void CancelTimer();

    uint16_t next_seq_no() { return last_seq_no_++ & kMaxSequenceNumber; }
    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    const fbl::unique_ptr<Timer> timer_;
    uint16_t last_seq_no_ = kMaxSequenceNumber;
};

}  // namespace wlan
