// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_rtc_device.h"

namespace rtc = fuchsia::hardware::rtc;

namespace time_server {

fidl::InterfaceRequestHandler<rtc::Device> FakeRtcDevice::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeRtcDevice::Set(const rtc::Time rtc_time) {
  current_rtc_time_ = rtc_time;
}

const rtc::Time FakeRtcDevice::Get() const { return current_rtc_time_; }

void FakeRtcDevice::Set(rtc::Time rtc, rtc::Device::SetCallback callback) {
  Set(rtc);
  callback(ZX_OK);
}

void FakeRtcDevice::Get(rtc::Device::GetCallback callback) {
  callback(current_rtc_time_);
}

}  // namespace time_server
