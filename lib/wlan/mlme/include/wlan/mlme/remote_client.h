// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/bss_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/fsm.h>
#include <wlan/mlme/remote_client_interface.h>
#include <wlan/mlme/timer.h>

#include <zircon/types.h>

namespace wlan {

class BaseState;

class RemoteClient : public fsm::StateMachine<BaseState>, public RemoteClientInterface {
   public:
    enum class StateId : uint8_t {
        kUninitialized,
        kDeauthenticated,
        kAuthenticated,
        kAssociated,
    };

    struct Listener {
        virtual void HandleClientStateChange(const common::MacAddr& client, StateId from,
                                             StateId to) = 0;
    };

    RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                 Listener* listener, const common::MacAddr& addr);
    ~RemoteClient();

    void MoveToState(fbl::unique_ptr<BaseState> state) override;

    // RemoteClientInterface implementation
    void HandleTimeout() override;

    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;

    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);
    zx_status_t SendDeauthentication(reason_code::ReasonCode reason_code);
    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet);

    // Note: There can only ever by one timer running at a time.
    // TODO(hahnr): Evolve this to support multiple timeouts at the same time.
    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(zx_duration_t tus);
    bool IsDeadlineExceeded(zx::time deadline);

    uint16_t next_seq_no() { return last_seq_no_++ & kMaxSequenceNumber; }
    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    Listener* const listener_;
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    const fbl::unique_ptr<Timer> timer_;
    uint16_t last_seq_no_ = kMaxSequenceNumber;
};

class BaseState : public fsm::StateInterface, public FrameHandler {
   public:
    BaseState(RemoteClient* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void HandleTimeout() {}

    virtual RemoteClient::StateId id() const = 0;

    template <typename S, typename... Args> void MoveToState(Args&&... args);

   protected:
    RemoteClient* const client_;
};

class DeauthenticatedState : public BaseState {
   public:
    DeauthenticatedState(RemoteClient* client);

    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    inline RemoteClient::StateId id() const override {
        return RemoteClient::StateId::kDeauthenticated;
    }
};

class AuthenticatedState : public BaseState {
   public:
    AuthenticatedState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout() override;

    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                   const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAssociationRequest(const ImmutableMgmtFrame<AssociationRequest>& frame,
                                         const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                       const wlan_rx_info_t& rxinfo) override;

    inline RemoteClient::StateId id() const override {
        return RemoteClient::StateId::kAuthenticated;
    }

   private:
    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr zx_duration_t kAuthenticationTimeoutTu = 1800000;  // 30min

    zx::time auth_timeout_;
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(RemoteClient* client, uint16_t aid);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout() override;

    zx_status_t HandleAssociationRequest(const ImmutableMgmtFrame<AssociationRequest>& frame,
                                         const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleDataFrame(const ImmutableDataFrame<LlcHeader>& frame,
                                const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                       const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                     const wlan_rx_info_t& rxinfo) override;

    inline RemoteClient::StateId id() const override { return RemoteClient::StateId::kAssociated; }

   private:
    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr zx_duration_t kInactivityTimeoutTu = 300000;  // 5min
    const uint16_t aid_;
    zx::time inactive_timeout_;
    bool active_;
};

}  // namespace wlan
