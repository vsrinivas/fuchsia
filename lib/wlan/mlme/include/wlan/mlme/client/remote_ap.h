// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/mac_frame.h>

#include <fuchsia/cpp/wlan_mlme.h>

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

    RemoteAp(DeviceInterface* device, fbl::unique_ptr<Timer> timer, const common::MacAddr& bssid);
    ~RemoteAp();

    void HandleTimeout();
    void MoveToState(fbl::unique_ptr<BaseState> to);

   private:
    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    const common::MacAddr bssid_;
    fbl::unique_ptr<BaseState> state_;
};

class UnjoinedState : public RemoteAp::BaseState {
   public:
    static constexpr const char* kName = "Unjoined";

    explicit UnjoinedState(RemoteAp* ap);

    zx_status_t HandleMlmeJoinReq(const wlan_mlme::JoinRequest& req) override;

    const char* name() const override { return kName; }
};

}  // namespace wlan
