// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <wlan/mlme/client/channel_scheduler.h>

namespace wlan {

ChannelScheduler::ChannelScheduler(OnChannelHandler* handler, DeviceInterface* device,
                                   TimerManager<TimeoutTarget>* timer_mgr)
    : on_channel_handler_(handler), device_(device), timer_mgr_(timer_mgr) {}

void ChannelScheduler::HandleIncomingFrame(std::unique_ptr<Packet> packet) {
  if (on_channel_) {
    on_channel_handler_->HandleOnChannelFrame(std::move(packet));
  } else {
    off_channel_request_.handler->HandleOffChannelFrame(std::move(packet));
  }
}

zx_status_t ChannelScheduler::SetChannel(const wlan_channel_t& chan) {
  channel_ = chan;
  if (on_channel_) {
    return device_->SetChannel(chan);
  }
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
  if (!ensure_on_channel_) {
    GoOffChannel();
  }
}

void ChannelScheduler::ScheduleTimeout(zx::time deadline) {
  timer_mgr_->Schedule(deadline, TimeoutTarget::kChannelScheduler, &timeout_);
}

void ChannelScheduler::HandleTimeout() {
  if (on_channel_) {
    ensure_on_channel_ = false;
    CancelTimeout();
    if (pending_off_channel_request_) {
      GoOffChannel();
    }
  } else {
    if (off_channel_request_.handler->EndOffChannelTime(false, &off_channel_request_)) {
      GoOffChannel();
    } else {
      CancelTimeout();
      device_->SetChannel(channel_);
      on_channel_ = true;
      on_channel_handler_->ReturnedOnChannel();
    }
  }
}

void ChannelScheduler::CancelTimeout() { timer_mgr_->Cancel(timeout_); }

void ChannelScheduler::GoOffChannel() {
  if (on_channel_) {
    on_channel_handler_->PreSwitchOffChannel();
    on_channel_ = false;
  }
  pending_off_channel_request_ = false;
  CancelTimeout();
  device_->SetChannel(off_channel_request_.chan);
  ScheduleTimeout(timer_mgr_->Now() + off_channel_request_.duration);
  off_channel_request_.handler->BeginOffChannelTime();
}

void ChannelScheduler::ResetTimer(zx::time deadline) {
  CancelTimeout();
  ScheduleTimeout(deadline);
}

}  // namespace wlan
