// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_DRIVER_V1_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_DRIVER_V1_H_

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/ui/input/drivers/hid-input-report/input-report.h"

namespace hid_input_report_dev {

class InputReportDriver;
using DriverDeviceType = ddk::Device<InputReportDriver, ddk::Unbindable,
                                     ddk::Messageable<fuchsia_input_report::InputDevice>::Mixin>;
class InputReportDriver : public DriverDeviceType,
                          public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  InputReportDriver(zx_device_t* parent, ddk::HidDeviceProtocolClient hiddev)
      : DriverDeviceType(parent), input_report_(hiddev) {}

  // DDK Functions.
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // FIDL functions.
  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override;
  void GetDescriptor(GetDescriptorCompleter::Sync& completer) override;
  void SendOutputReport(SendOutputReportRequestView request,
                        SendOutputReportCompleter::Sync& completer) override;
  void GetFeatureReport(GetFeatureReportCompleter::Sync& completer) override;
  void SetFeatureReport(SetFeatureReportRequestView request,
                        SetFeatureReportCompleter::Sync& completer) override;
  void GetInputReport(GetInputReportRequestView request,
                      GetInputReportCompleter::Sync& completer) override;

  InputReport& input_report() { return input_report_; }

 private:
  InputReport input_report_;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_DRIVER_V1_H_
