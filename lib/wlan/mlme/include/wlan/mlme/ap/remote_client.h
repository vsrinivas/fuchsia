// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan_mlme/cpp/fidl.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer.h>

#include <zircon/types.h>

namespace wlan {

class BaseState;

class RemoteClient : public RemoteClientInterface {
   public:
    struct Listener {
        virtual zx_status_t HandleClientDeauth(const common::MacAddr& client) = 0;
        virtual void HandleClientBuChange(const common::MacAddr& client, size_t bu_count) = 0;
    };

    RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                 Listener* listener, const common::MacAddr& addr);
    ~RemoteClient();

    // RemoteClientInterface implementation
    void HandleTimeout() override;

    zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) override;
    zx_status_t HandleDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaRequestFrame(const ImmutableMgmtFrame<AddBaRequestFrame>& rx_frame,
                                        const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaResponseFrame(const ImmutableMgmtFrame<AddBaResponseFrame>& rx_frame,
                                         const wlan_rx_info& rxinfo) override;

    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);
    zx_status_t SendDeauthentication(reason_code::ReasonCode reason_code);
    zx_status_t SendAddBaRequest();
    zx_status_t SendAddBaResponse(const ImmutableMgmtFrame<AddBaRequestFrame>& rx_frame);

    uint8_t GetTid();
    uint8_t GetTid(const ImmutableBaseFrame<EthernetII>& frame);

    // Enqueues an ethernet frame which can be sent at a later point in time.
    zx_status_t EnqueueEthernetFrame(const ImmutableBaseFrame<EthernetII>& frame);
    zx_status_t DequeueEthernetFrame(fbl::unique_ptr<Packet>* out_packet);
    bool HasBufferedFrames() const;
    zx_status_t ConvertEthernetToDataFrame(const ImmutableBaseFrame<EthernetII>& frame,
                                           fbl::unique_ptr<Packet>* out_frame);
    void ReportBuChange(size_t bu_count);
    zx_status_t ReportDeauthentication();

    void MoveToState(fbl::unique_ptr<BaseState> state);

    // Note: There can only ever be one timer running at a time.
    // TODO(hahnr): Evolve this to support multiple timeouts at the same time.
    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(wlan_tu_t tus);
    bool IsDeadlineExceeded(zx::time deadline);

    DeviceInterface* device() { return device_; }
    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    zx_status_t WriteHtCapabilities(ElementWriter* w);
    zx_status_t WriteHtOperation(ElementWriter* w);

    // Maximum number of packets buffered while the client is in power saving
    // mode.
    // TODO(NET-687): Find good BU limit.
    static constexpr size_t kMaxPowerSavingQueueSize = 30;

    Listener* const listener_;
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    const fbl::unique_ptr<Timer> timer_;
    // Queue which holds buffered `EthernetII` packets while the client is in
    // power saving mode.
    PacketQueue bu_queue_;
    fbl::unique_ptr<BaseState> state_;
};

class BaseState : public FrameHandler {
   public:
    BaseState(RemoteClient* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void HandleTimeout() {}

    virtual const char* name() const = 0;

   protected:
    template <typename S, typename... Args> void MoveToState(Args&&... args);

    RemoteClient* const client_;
};

class DeauthenticatingState : public BaseState {
   public:
    DeauthenticatingState(RemoteClient* client);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Deauthenticating";
};

class DeauthenticatedState : public BaseState {
   public:
    DeauthenticatedState(RemoteClient* client);

    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Deauthenticated";
};

class AuthenticatingState : public BaseState {
   public:
    AuthenticatingState(RemoteClient* client, const ImmutableMgmtFrame<Authentication>& frame);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticating";

    status_code::StatusCode status_code_;
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

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticated";

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kAuthenticationTimeoutTu = 1800000;  // ~30min

    zx::time auth_timeout_;
};

class AssociatingState : public BaseState {
   public:
    AssociatingState(RemoteClient* client, const ImmutableMgmtFrame<AssociationRequest>& frame);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associating";

    status_code::StatusCode status_code_;
    uint16_t aid_;
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(RemoteClient* client, uint16_t aid);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout() override;

    zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) override;
    zx_status_t HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
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
    zx_status_t HandleCtrlFrame(const FrameControl& fc) override;
    zx_status_t HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                  const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleMlmeEapolReq(const wlan_mlme::EapolRequest& req) override;
    zx_status_t HandleMlmeSetKeysReq(const wlan_mlme::SetKeysRequest& req) override;
    zx_status_t HandleAddBaRequestFrame(const ImmutableMgmtFrame<AddBaRequestFrame>& frame,
                                        const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaResponseFrame(const ImmutableMgmtFrame<AddBaResponseFrame>& frame,
                                         const wlan_rx_info& rxinfo) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associated";

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kInactivityTimeoutTu = 300000;  // ~5min
    zx_status_t SendNextBu();
    void UpdatePowerSaveMode(const FrameControl& fc);

    const uint16_t aid_;
    zx::time inactive_timeout_;
    // `true` if the client was active during the last inactivity timeout.
    bool active_;
    // `true` if the client entered Power Saving mode's doze state.
    bool dozing_;
    // `true` if a Deauthentication notification should be sent when leaving the
    // state.
    bool req_deauth_ = true;
    eapol::PortState eapol_controlled_port_ = eapol::PortState::kBlocked;
};

}  // namespace wlan
