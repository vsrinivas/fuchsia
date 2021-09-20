// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_
#define SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_

#include <fuchsia/input/report/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <fbl/mutex.h>

#include "reports_reader.h"

namespace fake_input_report_device {

// Creates a fake device that vends fuchsia.input.report FIDL.
// This device needs to be fidl::BindSingleInFlightOnly to a thread in order to start
// receiving requests.
// If this class is bound on a seperate thread, that thread must be joined before
// this class is destructed.
// Calling `SetReport` and `SetDescriptor` will change the behavior of
// the device when the client goes to read the report or the descriptor.
class FakeInputDevice final : public fuchsia::input::report::InputDevice {
 public:
  explicit FakeInputDevice(fidl::InterfaceRequest<fuchsia::input::report::InputDevice> request,
                           async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {}

  // Sets the fake's report, which will be read with |ReadInputReports| and
  // |GetInputReport|. This also triggers the |reports_events_| signal which
  // wakes up any clients waiting for report dta.
  void SetReports(std::vector<fuchsia::input::report::InputReport> reports);

  // Sets the fake's descriptor, which will be read with |GetDescriptor|.
  void SetDescriptor(fuchsia::input::report::DeviceDescriptorPtr descriptor);

  // The overriden FIDL function calls.
  void GetInputReportsReader(
      fidl::InterfaceRequest<fuchsia::input::report::InputReportsReader> reader) override;
  void GetDescriptor(GetDescriptorCallback callback) override;
  void SendOutputReport(fuchsia::input::report::OutputReport report,
                        SendOutputReportCallback callback) override;
  void GetFeatureReport(GetFeatureReportCallback callback) override {
    callback(
        fuchsia::input::report::InputDevice_GetFeatureReport_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }
  void SetFeatureReport(::fuchsia::input::report::FeatureReport report,
                        SetFeatureReportCallback callback) override {
    callback(
        fuchsia::input::report::InputDevice_SetFeatureReport_Result::WithErr(ZX_ERR_NOT_SUPPORTED));
  }
  void GetInputReport(::fuchsia::input::report::DeviceType device_type,
                      GetInputReportCallback callback) override;

 private:
  friend class FakeInputReportsReader;

  // This is used by the InputReportsReader to read the reports and send them to the client.
  std::vector<fuchsia::input::report::InputReport> ReadReports();

  fidl::Binding<fuchsia::input::report::InputDevice> binding_;

  // This lock makes the class thread-safe, which is important because setting the
  // reports and handling the FIDL calls can happen on seperate threads.
  fbl::Mutex lock_;

  std::vector<fuchsia::input::report::InputReport> reports_ __TA_GUARDED(lock_);
  fuchsia::input::report::DeviceDescriptorPtr descriptor_ __TA_GUARDED(lock_);
  std::optional<FakeInputReportsReader> reader_ __TA_GUARDED(lock_);
};

}  // namespace fake_input_report_device

#endif  // SRC_UI_INPUT_TESTING_FAKE_INPUT_REPORT_DEVICE_FAKE_H_
