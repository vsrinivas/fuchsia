// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/fsm.h>
#include <wlan/mlme/packet.h>
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
        virtual void HandleClientPowerSaveMode(const common::MacAddr& client, bool dozing) = 0;
    };

    RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                 Listener* listener, const common::MacAddr& addr);
    ~RemoteClient();

    void MoveToState(fbl::unique_ptr<BaseState> state) override;

    // RemoteClientInterface implementation
    void HandleTimeout() override;

    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;

    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);
    zx_status_t SendDeauthentication(reason_code::ReasonCode reason_code);
    zx_status_t SendEthernet(fbl::unique_ptr<Packet> packet);
    zx_status_t SendDataFrame(fbl::unique_ptr<Packet> packet);
    zx_status_t SendEapolIndication(const EapolFrame& eapol, const common::MacAddr& src,
                                    const common::MacAddr& dst);
    zx_status_t SendEapolResponse(EapolResultCodes result_code);

    // Enqueues an ethernet frame which can be sent at a later point in time.
    zx_status_t EnqueueEthernetFrame(const ImmutableBaseFrame<EthernetII>& frame);
    zx_status_t DequeueEthernetFrame(fbl::unique_ptr<Packet>* out_packet);
    bool HasBufferedFrames() const;
    zx_status_t ConvertEthernetToDataFrame(const ImmutableBaseFrame<EthernetII>& frame,
                                           fbl::unique_ptr<Packet>* out_frame);
    void HandlePowerSaveModeChange(bool dozing);

    // Note: There can only ever be one timer running at a time.
    // TODO(hahnr): Evolve this to support multiple timeouts at the same time.
    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(zx_duration_t tus);
    bool IsDeadlineExceeded(zx::time deadline);

    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    // Maximum amount of packets buffered while the client is in power saving mode.
    // Use `0` buffers to disable buffering until PS mode is ready
    static constexpr size_t kMaxPowerSavingQueueSize = 0;

    Listener* const listener_;
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    const fbl::unique_ptr<Timer> timer_;
    // Queue which holds buffered `EthernetII` packets while the client is in power saving mode.
    PacketQueue ps_pkt_queue_;
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

    zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) override;
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
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleMlmeEapolReq(const EapolRequest& req) override;

    inline RemoteClient::StateId id() const override { return RemoteClient::StateId::kAssociated; }

   private:
    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr zx_duration_t kInactivityTimeoutTu = 300000;  // 5min

    void UpdatePowerSaveMode(const FrameControl& fc);

    const uint16_t aid_;
    zx::time inactive_timeout_;
    // `true` if the client was active during the last inactivity timeout.
    bool active_;
    // `true` if the client entered Power Saving mode's doze state.
    bool dozing_;
};

}  // namespace wlan
