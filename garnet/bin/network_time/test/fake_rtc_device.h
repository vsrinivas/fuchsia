// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TEST_FAKE_RTC_DEVICE_H_
#define GARNET_BIN_NETWORK_TIME_TEST_FAKE_RTC_DEVICE_H_

#include "fuchsia/hardware/rtc/cpp/fidl.h"
#include "lib/fidl/cpp/binding_set.h"

namespace time_server {

// Fake implementation of |rtc::Device| that allows directly setting the time.
//
// The clock does not advance automatically.
class FakeRtcDevice : fuchsia::hardware::rtc::Device {
 public:
  fidl::InterfaceRequestHandler<fuchsia::hardware::rtc::Device> GetHandler();

  void Set(fuchsia::hardware::rtc::Time rtc_time);
  const fuchsia::hardware::rtc::Time Get() const;

  // |fuchsia::hardware::rtc::Device|
  void Set(fuchsia::hardware::rtc::Time time,
           fuchsia::hardware::rtc::Device::SetCallback callback) override;
  // |fuchsia::hardware::rtc::Device|
  void Get(fuchsia::hardware::rtc::Device::GetCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::hardware::rtc::Device> bindings_;
  fuchsia::hardware::rtc::Time current_rtc_time_;
};

}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TEST_FAKE_RTC_DEVICE_H_
