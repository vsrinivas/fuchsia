// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>

#include <wlan_mlme/cpp/fidl.h>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class Packet;
class Timer;

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
             const wlan_mlme::BSSDescription& bssid);
    ~RemoteAp();

    DeviceInterface* device() { return device_; }
    const common::MacAddr& bssid() { return bssid_; }
    const char* bssid_str() { return bssid_.ToString().c_str(); }
    const wlan_mlme::BSSDescription& bss() { return *bss_.get(); }
    const wlan_channel_t& bss_chan() { return bss_chan_; }

    zx_status_t StartTimer(zx::time deadline);
    zx_status_t CancelTimer();
    zx::time CreateTimerDeadline(wlan_tu_t tus);
    bool IsDeadlineExceeded(zx::time deadline);
    void HandleTimeout();
    void MoveToState(fbl::unique_ptr<BaseState> to);

   private:
    zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) override;

    wlan_channel_t SanitizeChannel(wlan_channel_t chan);
    // Stub functions filled with logic in the future.
    bool IsCbw40RxReady() const;
    bool IsCbw40TxReady() const;

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    common::MacAddr bssid_;
    wlan_mlme::BSSDescriptionPtr bss_;
    wlan_channel_t bss_chan_;
    fbl::unique_ptr<BaseState> state_;
};

class InitState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Init";

    explicit InitState(RemoteAp* ap);

    void HandleTimeout() override;

    const char* name() const override { return kName; }

   private:
    zx_status_t HandleBeacon(const ImmutableMgmtFrame<Beacon>& frame,
                             const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleProbeResponse(const ImmutableMgmtFrame<ProbeResponse>& frame,
                                    const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) override;

    void OnExit() override;
    void MoveToJoinedState();

    zx::time join_deadline_;
};

class JoinedState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Joined";

    explicit JoinedState(RemoteAp* ap);

    const char* name() const override { return kName; }
};

}  // namespace wlan
