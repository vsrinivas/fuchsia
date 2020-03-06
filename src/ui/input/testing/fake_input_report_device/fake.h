// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_
#define SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_

#include <fuchsia/input/report/llcpp/fidl.h>

#include <fbl/mutex.h>

#include "src/ui/input/lib/hid-input-report/fidl.h"

namespace fake_input_report_device {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

// Creates a fake device that vends fuchsia.input.report FIDL.
// This device needs to be fidl::Bind to a thread in order to start
// receiving requests.
// Calling `SetReport` and `SetDescriptor` will change the behavior of
// the device when the client goes to read the report or the descriptor.
class FakeInputDevice final : public fuchsia_input_report::InputDevice::Interface {
 public:
  FakeInputDevice() : lock_() {
    zx::event::create(0, &reports_event_);
    assert(reports_event_.is_valid());
  }

  // Sets the fake's report, which will be read with |GetReports|. This also
  // triggers the |reports_events_| signal which wakes up any clients waiting
  // for report dta.
  void SetReport(hid_input_report::InputReport report);

  // Sets the fake's descriptor, which will be read with |GetDescriptor|.
  void SetDescriptor(hid_input_report::ReportDescriptor descriptor);

  // The overriden FIDL function calls.
  void GetReportsEvent(GetReportsEventCompleter::Sync completer) override;
  void GetReports(GetReportsCompleter::Sync completer) override;
  void SendOutputReport(::llcpp::fuchsia::input::report::OutputReport report,
                        SendOutputReportCompleter::Sync completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync completer) override;

 private:
  // This lock makes the class thread-safe, which is important because setting the
  // reports and handling the FIDL calls happen on seperate threads.
  fbl::Mutex lock_ = {};
  zx::event reports_event_ __TA_GUARDED(lock_);
  hid_input_report::InputReport report_ __TA_GUARDED(lock_) = {};
  hid_input_report::ReportDescriptor descriptor_ __TA_GUARDED(lock_) = {};
};

}  // namespace fake_input_report_device

#endif  // SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_
