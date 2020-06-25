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

void FakeInputDevice::GetReportsEvent(GetReportsEventCallback callback) {
  fbl::AutoLock lock(&lock_);
  zx::event new_event;
  zx_status_t status = reports_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);
  callback(status, std::move(new_event));
}

void FakeInputDevice::GetReports(GetReportsCallback callback) {
  fbl::AutoLock lock(&lock_);
  reports_event_.signal(ZX_USER_SIGNAL_0, 0);
  callback(std::move(reports_));
}

void FakeInputDevice::SendOutputReport(fuchsia::input::report::OutputReport report,
                                       SendOutputReportCallback callback) {
  callback(
      fuchsia::input::report::InputDevice_SendOutputReport_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
}

void FakeInputDevice::SetReports(std::vector<fuchsia::input::report::InputReport> reports) {
  fbl::AutoLock lock(&lock_);
  reports_ = std::move(reports);
  reports_event_.signal(0, ZX_USER_SIGNAL_0);
  if (reader_) {
    reader_->QueueCallback();
  }
}

std::vector<fuchsia::input::report::InputReport> FakeInputDevice::ReadReports() {
  fbl::AutoLock lock(&lock_);
  reports_event_.signal(ZX_USER_SIGNAL_0, 0);
  return std::move(reports_);
}

}  // namespace fake_input_report_device
