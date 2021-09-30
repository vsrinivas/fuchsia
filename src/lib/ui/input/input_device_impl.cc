// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/ui/input/input_device_impl.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace ui_input {

InputDeviceImpl::InputDeviceImpl(
    uint32_t id, fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request,
    Listener* listener)
    : id_(id),
      descriptor_(std::move(descriptor)),
      input_device_binding_(this, std::move(input_device_request)),
      listener_(listener) {
  input_device_binding_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Device disconnected";
    listener_->OnDeviceDisconnected(this);
  });
}

InputDeviceImpl::~InputDeviceImpl() {}

void InputDeviceImpl::DispatchReport(fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "input_report_listener", "id", report.trace_id);
  TRACE_FLOW_END("input", "hid_read_to_listener", report.trace_id);
  TRACE_FLOW_BEGIN("input", "report_to_presenter", report.trace_id);
  if (descriptor_.media_buttons) {
    if (!last_report_) {
      last_report_ = std::make_unique<fuchsia::ui::input::InputReport>();
    }
    fidl::Clone(report, last_report_.get());
  }
  listener_->OnReport(this, std::move(report));
}

}  // namespace ui_input
