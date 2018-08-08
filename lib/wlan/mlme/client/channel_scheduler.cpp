// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/client/channel_scheduler.h>

namespace wlan {

ChannelScheduler::ChannelScheduler(OnChannelHandler* handler, DeviceInterface* device,
                                   fbl::unique_ptr<Timer> timer)
    : on_channel_handler_(handler), device_(device), timer_(fbl::move(timer)) {}

void ChannelScheduler::HandleIncomingFrame(fbl::unique_ptr<Packet> packet) {
    if (on_channel_) {
        on_channel_handler_->HandleOnChannelFrame(fbl::move(packet));
    } else {
        off_channel_request_.handler->HandleOffChannelFrame(fbl::move(packet));
    }
}

zx_status_t ChannelScheduler::SetChannel(const wlan_channel_t& chan) {
    channel_ = chan;
    if (on_channel_) { return device_->SetChannel(chan); }
    return ZX_OK;
}

void ChannelScheduler::EnsureOnChannel(zx::time end) {
    if (!on_channel_) {
        pending_off_channel_request_ =
            off_channel_request_.handler->EndOffChannelTime(true, &off_channel_request_);
        device_->SetChannel(channel_);
        on_channel_ = true;
        on_channel_handler_->ReturnedOnChannel();
    }
    ensure_on_channel_ = true;
    ResetTimer(end);
}

void ChannelScheduler::RequestOffChannelTime(const OffChannelRequest& request) {
    off_channel_request_ = request;
    pending_off_channel_request_ = true;
    if (!ensure_on_channel_) { GoOffChannel(); }
}

void ChannelScheduler::HandleTimeout() {
    if (on_channel_) {
        ensure_on_channel_ = false;
        timer_->CancelTimer();
        if (pending_off_channel_request_) { GoOffChannel(); }
    } else {
        if (off_channel_request_.handler->EndOffChannelTime(false, &off_channel_request_)) {
            GoOffChannel();
        } else {
            timer_->CancelTimer();
            device_->SetChannel(channel_);
            on_channel_ = true;
            on_channel_handler_->ReturnedOnChannel();
        }
    }
}

void ChannelScheduler::GoOffChannel() {
    if (on_channel_) {
        on_channel_handler_->PreSwitchOffChannel();
        on_channel_ = false;
    }
    pending_off_channel_request_ = false;
    ResetTimer(timer_->Now() + off_channel_request_.duration);
    device_->SetChannel(off_channel_request_.chan);
    off_channel_request_.handler->BeginOffChannelTime();
}

void ChannelScheduler::ResetTimer(zx::time deadline) {
    timer_->CancelTimer();
    timer_->SetTimer(deadline);
}

}  // namespace wlan
