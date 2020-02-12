// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/inject/llcpp/fidl.h>
#include <fuchsia/input/report/llcpp/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/ui/lib/hid-input-report/fidl.h"
#include "src/ui/lib/input-report-instance-driver/instance.h"

#ifndef SRC_UI_DRIVERS_INPUT_REPORT_INJECT_FAKE_INPUT_REPORT_H_
#define SRC_UI_DRIVERS_INPUT_REPORT_INJECT_FAKE_INPUT_REPORT_H_

namespace input_report_inject {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;
namespace fuchsia_input_inject = ::llcpp::fuchsia::input::inject;

using ::input_report_instance::InputReportBase;
using ::input_report_instance::InputReportInstance;

class InputReportInjectInstance;
class InputReportInject;
class FakeInputReport;

using FakeInputReportDeviceType = ddk::Device<FakeInputReport, ddk::Openable, ddk::UnbindableNew>;
class FakeInputReport : public FakeInputReportDeviceType,
                        public InputReportBase,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_INPUTREPORT> {
 public:
  FakeInputReport(zx_device_t* parent) : FakeInputReportDeviceType(parent) {}
  virtual ~FakeInputReport() = default;

  // Ddk functions.
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkRelease() { delete this; }
  void DdkUnbindNew(ddk::UnbindTxn txn);

  static FakeInputReport* Create(zx_device_t* parent,
                                 fuchsia_input_report::DeviceDescriptor descriptor);
  void ReceiveInput(fidl::VectorView<fuchsia_input_report::InputReport> reports);

  // InputReportBase functions.
  virtual void RemoveInstanceFromList(InputReportInstance* instance) override;
  virtual const hid_input_report::ReportDescriptor* GetDescriptors(size_t* size) override;
  virtual zx_status_t SendOutputReport(fuchsia_input_report::OutputReport report) override;

 private:
  void ConvertDescriptors(const fuchsia_input_report::DeviceDescriptor& descriptor);

  fbl::Mutex instance_lock_;
  // Unmanaged linked-list because the HidInstances free themselves through DdkRelease.
  fbl::DoublyLinkedList<InputReportInstance*> instance_list_ __TA_GUARDED(instance_lock_);

  std::vector<hid_input_report::ReportDescriptor> descriptors_;
};

}  // namespace input_report_inject

#endif  // SRC_UI_DRIVERS_INPUT_REPORT_INJECT_FAKE_INPUT_REPORT_H_
