// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/mocks/mock_input_device.h"

namespace mozart {
namespace test {

MockInputDevice::MockInputDevice(
    uint32_t device_id,
    mozart::DeviceDescriptorPtr descriptor,
    fidl::InterfaceRequest<mozart::InputDevice> input_device_request,
    const OnReportCallback& on_report_callback)
    : id_(device_id),
      descriptor_(std::move(descriptor)),
      input_device_binding_(this, std::move(input_device_request)),
      on_report_callback_(on_report_callback) {}

MockInputDevice::~MockInputDevice() {}

void MockInputDevice::DispatchReport(mozart::InputReportPtr report) {
  if (on_report_callback_)
    on_report_callback_(std::move(report));
}

}  // namespace test
}  // namespace mozart
