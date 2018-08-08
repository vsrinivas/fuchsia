// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer.h>
#include <wlan/protocol/info.h>

namespace wlan {

struct OffChannelHandler;

struct OffChannelRequest {
    wlan_channel_t chan;
    zx::duration duration;
    OffChannelHandler* handler;
};

struct OffChannelHandler {
    virtual void BeginOffChannelTime() = 0;
    virtual void HandleOffChannelFrame(fbl::unique_ptr<Packet>) = 0;

    // Invoked before switching back on channel.
    // Fill |next_req| and return true to schedule another off-channel request.
    virtual bool EndOffChannelTime(bool interrupted, OffChannelRequest* next_req) = 0;

    virtual ~OffChannelHandler() = default;
};

struct OnChannelHandler {
    virtual void HandleOnChannelFrame(fbl::unique_ptr<Packet>) = 0;
    virtual void PreSwitchOffChannel() = 0;
    virtual void ReturnedOnChannel() = 0;
};

class ChannelScheduler {
   public:
    ChannelScheduler(OnChannelHandler* handler, DeviceInterface* device,
                     fbl::unique_ptr<Timer> timer);

    void HandleIncomingFrame(fbl::unique_ptr<Packet>);

    // Set the 'on' channel. If we are currently on the main channel,
    // switch to the new main channel.
    zx_status_t SetChannel(const wlan_channel_t& chan);

    // Return true if we are currently on the main channel
    bool OnChannel() const { return on_channel_; }

    // Switch on channel immediately and ensure that we stay there
    // at least until |end|
    void EnsureOnChannel(zx::time end);

    // Request an off-channel time. Any previously existing request will be dropped.
    // Off-channel time might not begin immediately.
    // |request.handler->BeginOffChannelTime| will be called when the off-channel time begins.
    void RequestOffChannelTime(const OffChannelRequest& request);

    void HandleTimeout();

   private:
    void GoOffChannel();
    void ResetTimer(zx::time deadline);

    OnChannelHandler* on_channel_handler_;
    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;

    wlan_channel_t channel_ = {.primary = 1, .cbw = CBW20, .secondary80 = 0};
    bool on_channel_ = true;
    bool ensure_on_channel_ = false;
    bool pending_off_channel_request_ = false;
    OffChannelRequest off_channel_request_;
};

}  // namespace wlan
