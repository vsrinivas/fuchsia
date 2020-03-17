// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/testing/fake_input_report_device/fake.h"

#include <fbl/auto_lock.h>

namespace fake_input_report_device {

void FakeInputDevice::SetDescriptor(hid_input_report::ReportDescriptor descriptor) {
  fbl::AutoLock lock(&lock_);
  descriptor_ = descriptor;
}

void FakeInputDevice::GetDescriptor(GetDescriptorCompleter::Sync completer) {
  fbl::AutoLock lock(&lock_);
  hid_input_report::FidlDescriptor fidl;
  hid_input_report::SetFidlDescriptor(descriptor_, &fidl);

  fuchsia_input_report::DeviceDescriptor descriptor = fidl.builder.build();
  completer.Reply(std::move(descriptor));
}

void FakeInputDevice::GetReportsEvent(GetReportsEventCompleter::Sync completer) {
  fbl::AutoLock lock(&lock_);
  zx::event new_event;
  zx_status_t status = reports_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);
  completer.Reply(status, std::move(new_event));
}

void FakeInputDevice::GetReports(GetReportsCompleter::Sync completer) {
  fbl::AutoLock lock(&lock_);
  hid_input_report::FidlInputReport fidl;
  zx_status_t status = hid_input_report::SetFidlInputReport(report_, &fidl);
  if (status != ZX_OK) {
    completer.Reply(fidl::VectorView<fuchsia_input_report::InputReport>(nullptr, 0));
    return;
  }

  fuchsia_input_report::InputReport report = fidl.builder.build();
  reports_event_.signal(ZX_USER_SIGNAL_0, 0);
  completer.Reply(fidl::VectorView<fuchsia_input_report::InputReport>(fidl::unowned(&report), 1));
}

void FakeInputDevice::SendOutputReport(::llcpp::fuchsia::input::report::OutputReport report,
                                       SendOutputReportCompleter::Sync completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
};

void FakeInputDevice::SetReport(hid_input_report::InputReport report) {
  fbl::AutoLock lock(&lock_);
  report_ = report;
  reports_event_.signal(0, ZX_USER_SIGNAL_0);
}

}  // namespace fake_input_report_device
