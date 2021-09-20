// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/testing/fake_input_report_device/fake.h"

#include <fbl/auto_lock.h>

#include "lib/fidl/cpp/clone.h"

namespace fake_input_report_device {

void FakeInputDevice::SetDescriptor(fuchsia::input::report::DeviceDescriptorPtr descriptor) {
  fbl::AutoLock lock(&lock_);
  descriptor_ = std::move(descriptor);
}

void FakeInputDevice::GetDescriptor(GetDescriptorCallback callback) {
  fbl::AutoLock lock(&lock_);
  fuchsia::input::report::DeviceDescriptor desc;
  fidl::Clone(*descriptor_, &desc);
  callback(std::move(desc));
}

void FakeInputDevice::GetInputReportsReader(
    fidl::InterfaceRequest<fuchsia::input::report::InputReportsReader> reader) {
  fbl::AutoLock lock(&lock_);
  if (reader_) {
    reader.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }
  reader_.emplace(std::move(reader), binding_.dispatcher(), this);
}

void FakeInputDevice::SendOutputReport(fuchsia::input::report::OutputReport report,
                                       SendOutputReportCallback callback) {
  callback(
      fuchsia::input::report::InputDevice_SendOutputReport_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
}

void FakeInputDevice::GetInputReport(::fuchsia::input::report::DeviceType device_type,
                                     GetInputReportCallback callback) {
  fbl::AutoLock lock(&lock_);
  if (reports_.empty()) {
    callback(
        fuchsia::input::report::InputDevice_GetInputReport_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  } else {
    fuchsia::input::report::InputDevice_GetInputReport_Response response(std::move(reports_[0]));
    callback(fuchsia::input::report::InputDevice_GetInputReport_Result::WithResponse(
        std::move(response)));
    reports_.erase(reports_.begin());
  }
}

void FakeInputDevice::SetReports(std::vector<fuchsia::input::report::InputReport> reports) {
  fbl::AutoLock lock(&lock_);
  reports_ = std::move(reports);
  if (reader_) {
    reader_->QueueCallback();
  }
}

std::vector<fuchsia::input::report::InputReport> FakeInputDevice::ReadReports() {
  fbl::AutoLock lock(&lock_);
  return std::move(reports_);
}

}  // namespace fake_input_report_device
