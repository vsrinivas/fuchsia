// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_REMOTE_CLIENT_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_REMOTE_CLIENT_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/remote_client_interface.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/eapol.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer_manager.h>

#include <zircon/types.h>

#include <optional>
#include <queue>

namespace wlan {

class BaseState;

class RemoteClient : public RemoteClientInterface {
   public:
    struct Listener {
        virtual void HandleClientFailedAuth(const common::MacAddr& client) = 0;
        virtual void HandleClientDeauth(const common::MacAddr& client) = 0;
        virtual void HandleClientDisassociation(aid_t aid) = 0;
        virtual void HandleClientBuChange(const common::MacAddr& client, aid_t aid,
                                          size_t bu_count) = 0;
    };

    RemoteClient(DeviceInterface* device, TimerManager&& timer_mgr, BssInterface* bss,
                 Listener* listener, const common::MacAddr& addr);
    ~RemoteClient();

    // RemoteClientInterface implementation
    void HandleTimeout() override;
    void HandleAnyEthFrame(EthFrame&& frame) override;
    void HandleAnyMgmtFrame(MgmtFrame<>&& frame) override;
    void HandleAnyDataFrame(DataFrame<>&& frame) override;
    void HandleAnyCtrlFrame(CtrlFrame<>&& frame) override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& mlme_msg) override;
    zx_status_t SendAuthentication(status_code::StatusCode result);
    zx_status_t SendAssociationResponse(aid_t aid, status_code::StatusCode result);
    zx_status_t SendDeauthentication(::fuchsia::wlan::mlme::ReasonCode reason_code);
    zx_status_t SendAddBaRequest();
    zx_status_t SendAddBaResponse(const AddBaRequestFrame& rx_frame);

    wlan_assoc_ctx_t BuildAssocContext(uint16_t aid);

    uint8_t GetTid();
    uint8_t GetTid(const EthFrame& frame);

    void MoveToState(fbl::unique_ptr<BaseState> state);
    void ReportBuChange(aid_t aid, size_t bu_count);
    void ReportFailedAuth();
    void ReportDeauthentication();
    void ReportDisassociation(aid_t aid);

    zx_status_t ScheduleTimer(zx::time deadline, TimedEvent* event);
    zx::time DeadlineAfterTus(wlan_tu_t tus);

    DeviceInterface* device() { return device_; }
    BssInterface* bss() { return bss_; }
    const common::MacAddr& addr() { return addr_; }

   private:
    Listener* const listener_;
    DeviceInterface* const device_;
    BssInterface* const bss_;
    const common::MacAddr addr_;
    TimerManager timer_mgr_;
    // Queue which holds buffered ethernet frames while the client is dozing.
    std::queue<EthFrame> bu_queue_;
    fbl::unique_ptr<BaseState> state_;
};

class BaseState {
   public:
    BaseState(RemoteClient* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void HandleTimeout(zx::time now) {}
    virtual zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) { return ZX_OK; }
    virtual void HandleAnyDataFrame(DataFrame<>&&) {}
    virtual void HandleAnyMgmtFrame(MgmtFrame<>&&) {}
    virtual void HandleAnyCtrlFrame(CtrlFrame<>&&) {}
    virtual void HandleEthFrame(EthFrame&&) {}

    virtual const char* name() const = 0;

   protected:
    template <typename S, typename... Args> void MoveToState(Args&&... args);

    RemoteClient* const client_;
};

class DeauthenticatingState : public BaseState {
   public:
    DeauthenticatingState(RemoteClient* client, ::fuchsia::wlan::mlme::ReasonCode reason_code,
                          bool send_deauth_frame);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Deauthenticating";

    ::fuchsia::wlan::mlme::ReasonCode reason_code_;
    bool send_deauth_frame_;
};

class DeauthenticatedState : public BaseState {
   public:
    enum class MoveReason {
        // DeauthenticatedState is created when RemoteClient is first initialized
        INIT,
        // RemoteClient moved to DeauthenticatedState due to a deauthenticate request, whether from
        // the user or due to the AP deciding that the client is inactive
        EXPLICIT_DEAUTH,
        // RemoteClient moved to DeauthenticatedState due to a failed authentication attempt
        FAILED_AUTH,
        // RemoteClient received an authentication frame while already authenticated or associated,
        // and moved to DeauthenticatedState to re-authenticate again
        REAUTH,
    };

    DeauthenticatedState(RemoteClient* client, DeauthenticatedState::MoveReason move_reason);

    void OnEnter() override;

    inline const char* name() const override { return kName; }

    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;
    void FailAuthentication(const status_code::StatusCode st_code);

   private:
    static constexpr const char* kName = "Deauthenticated";

    DeauthenticatedState::MoveReason move_reason_;
    std::optional<MgmtFrame<Authentication>> reauth_frame_;
};

class AuthenticatingState : public BaseState {
   public:
    AuthenticatingState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout(zx::time now) override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticating";
    static constexpr wlan_tu_t kAuthenticatingTimeoutTu = 60000;  // ~1 minute

    zx_status_t FinalizeAuthenticationAttempt(const status_code::StatusCode st_code);

    TimedEvent auth_timeout_;
};

class AuthenticatedState : public BaseState {
   public:
    AuthenticatedState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout(zx::time now) override;
    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Authenticated";

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kAuthenticationTimeoutTu = 1800000;  // ~30min

    void HandleAuthentication(MgmtFrame<Authentication>&&);
    void HandleAssociationRequest(MgmtFrame<AssociationRequest>&&);
    void HandleDeauthentication(MgmtFrame<Deauthentication>&&);

    TimedEvent auth_timeout_;
};

class AssociatingState : public BaseState {
   public:
    AssociatingState(RemoteClient* client);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout(zx::time now) override;
    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associating";
    static constexpr wlan_tu_t kAssociatingTimeoutTu = 60000;  // ~1 minute

    zx_status_t FinalizeAssociationAttempt(std::optional<uint16_t> aid,
                                           status_code::StatusCode st_code);

    TimedEvent assoc_timeout_;
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(RemoteClient* client, uint16_t aid);

    void OnEnter() override;
    void OnExit() override;

    void HandleTimeout(zx::time now) override;

    zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
    void HandleAnyDataFrame(DataFrame<>&&) override;
    void HandleAnyMgmtFrame(MgmtFrame<>&&) override;
    void HandleAnyCtrlFrame(CtrlFrame<>&&) override;
    void HandleEthFrame(EthFrame&&) override;

    inline const char* name() const override { return kName; }

   private:
    static constexpr const char* kName = "Associated";
    // Maximum number of packets buffered while the client is in power saving
    // mode.
    // TODO(NET-687): Find good BU limit.
    static constexpr size_t kMaxPowerSavingQueueSize = 30;

    zx_status_t HandleMlmeEapolReq(const MlmeMsg<::fuchsia::wlan::mlme::EapolRequest>& req);
    zx_status_t HandleMlmeDeauthReq(
        const MlmeMsg<::fuchsia::wlan::mlme::DeauthenticateRequest>& req);

    // TODO(hahnr): Use WLAN_MIN_TU once defined.
    static constexpr wlan_tu_t kInactivityTimeoutTu = 300000;  // ~5min
    zx_status_t SendNextBu();
    void UpdatePowerSaveMode(const FrameControl& fc);

    void HandleAuthentication(MgmtFrame<Authentication>&&);
    void HandleAssociationRequest(MgmtFrame<AssociationRequest>&&);
    void HandleDeauthentication(MgmtFrame<Deauthentication>&&);
    void HandleDisassociation(MgmtFrame<Disassociation>&&);
    void HandleActionFrame(MgmtFrame<ActionFrame>&&);
    void HandleDataLlcFrame(DataFrame<LlcHeader>&&);
    void HandlePsPollFrame(CtrlFrame<PsPollFrame>&&);

    std::optional<DataFrame<LlcHeader>> EthToDataFrame(const EthFrame& eth_frame);

    // Enqueues an ethernet frame which can be sent at a later point in time.
    zx_status_t EnqueueEthernetFrame(EthFrame&& frame);
    std::optional<EthFrame> DequeueEthernetFrame();
    bool HasBufferedFrames() const;

    const uint16_t aid_;
    TimedEvent inactive_timeout_;
    // `true` if the client was active during the last inactivity timeout.
    bool active_ = false;
    // `true` if the client entered Power Saving mode's doze state.
    bool dozing_ = false;
    eapol::PortState eapol_controlled_port_ = eapol::PortState::kBlocked;
    // Queue which holds buffered ethernet frames while the client is dozing.
    std::queue<EthFrame> bu_queue_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_REMOTE_CLIENT_H_
