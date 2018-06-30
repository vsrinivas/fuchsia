// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/service.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class Packet;
class Timer;

namespace remote_ap {

class RemoteAp : public FrameHandler {
   public:
    class BaseState : public FrameHandler {
       public:
        explicit BaseState(RemoteAp* ap) : ap_(ap) {}
        virtual ~BaseState() = default;

        virtual void OnEnter() {}
        virtual void OnExit() {}
        virtual void HandleTimeout() {}

        virtual const char* name() const = 0;

       protected:
        template <typename S, typename... Args> void MoveToState(Args&&... args);

        RemoteAp* const ap_;
    };

    RemoteAp(DeviceInterface* device, fbl::unique_ptr<Timer> timer,
             const ::fuchsia::wlan::mlme::BSSDescription& bssid);
    ~RemoteAp();

    DeviceInterface* device() { return device_; }
    const common::MacAddr& bssid() { return bssid_; }
    const char* bssid_str() { return bssid_.ToString().c_str(); }
    const ::fuchsia::wlan::mlme::BSSDescription& bss() { return *bss_.get(); }
    const wlan_channel_t& bss_chan() { return bss_chan_; }
    Sequence* seq() { return &seq_; }

    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(wlan_tu_t tus);
    bool IsDeadlineExceeded(zx::time deadline);
    void HandleTimeout();
    void MoveToState(fbl::unique_ptr<BaseState> to);

    // Stub functions filled with logic in the future.
    bool IsHTReady() const;
    bool IsCbw40RxReady() const;
    bool IsCbw40TxReady() const;
    bool IsQosReady() const;
    bool IsAmsduRxReady() const;
    // TODO(porce): Find intersection of:
    // - BSS capabilities
    // - Client radio capabilities
    // - Client configuration
    HtCapabilities BuildHtCapabilities();

   private:
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;
    wlan_channel_t SanitizeChannel(wlan_channel_t chan);

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    common::MacAddr bssid_;
    ::fuchsia::wlan::mlme::BSSDescriptionPtr bss_;
    wlan_channel_t bss_chan_;
    fbl::unique_ptr<BaseState> state_;
    Sequence seq_;
};

class InitState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Init";

    explicit InitState(RemoteAp* ap);

    void HandleTimeout() override;

    const char* name() const override { return kName; }

   private:
    zx_status_t HandleBeacon(const MgmtFrame<Beacon>& frame) override;
    zx_status_t HandleProbeResponse(const MgmtFrame<ProbeResponse>& frame) override;
    zx_status_t HandleMlmeJoinReq(const MlmeMsg<::fuchsia::wlan::mlme::JoinRequest>& req);

    void OnExit() override;
    void MoveToJoinedState();

    zx::time join_deadline_;
};

class JoinedState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Joined";

    explicit JoinedState(RemoteAp* ap);

    const char* name() const override { return kName; }

   private:
    zx_status_t HandleMlmeAuthReq(
        const MlmeMsg<::fuchsia::wlan::mlme::AuthenticateRequest>& req);
};

class AuthenticatingState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Authenticating";

    explicit AuthenticatingState(RemoteAp* ap, AuthAlgorithm auth_alg, wlan_tu_t auth_timeout_tu);

    void HandleTimeout() override;
    const char* name() const override { return kName; }

   private:
    void OnExit() override;

    zx_status_t HandleAuthentication(const MgmtFrame<Authentication>& frame) override;
    template <typename State> void MoveOn(::fuchsia::wlan::mlme::AuthenticateResultCodes result_code);

    zx::time auth_deadline_;
    AuthAlgorithm auth_alg_;
};

class AuthenticatedState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Authenticated";

    explicit AuthenticatedState(RemoteAp* ap);

    const char* name() const override { return kName; }

   private:
    zx_status_t HandleMlmeAssocReq(
        const MlmeMsg<::fuchsia::wlan::mlme::AssociateRequest>& req);
};

class AssociatingState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Associating";

    explicit AssociatingState(RemoteAp* ap);

    const char* name() const override { return kName; }

   private:
    static constexpr size_t kAssocTimeoutTu = 500;  // ~500ms

    void OnExit() override;
    void HandleTimeout() override;

    zx_status_t HandleAssociationResponse(const MgmtFrame<AssociationResponse>& frame) override;

    zx::time assoc_deadline_;
};

class AssociatedState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Associated";

    explicit AssociatedState(RemoteAp* ap, aid_t aid);

    const char* name() const override { return kName; }
};

}  // namespace remote_ap
}  // namespace wlan
