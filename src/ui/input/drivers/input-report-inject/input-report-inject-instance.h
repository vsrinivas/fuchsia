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
#include <fbl/intrusive_double_list.h>

#include "fake-input-report.h"
#include "src/ui/input/lib/hid-input-report/fidl.h"

#ifndef SRC_UI_INPUT_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_INSTANCE_H_
#define SRC_UI_INPUT_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_INSTANCE_H_

namespace input_report_inject {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;
namespace fuchsia_input_inject = ::llcpp::fuchsia::input::inject;

using ::input_report_instance::InputReportBase;
using ::input_report_instance::InputReportInstance;

class InputReportInjectInstance;
class InputReportInject;

using InstanceDeviceType = ddk::Device<InputReportInjectInstance, ddk::Closable, ddk::Messageable>;
class InputReportInjectInstance : public InstanceDeviceType,
                                  ::llcpp::fuchsia::input::inject::FakeInputReportDevice::Interface,
                                  public fbl::DoublyLinkedListable<InputReportInjectInstance*> {
 public:
  InputReportInjectInstance(zx_device_t* parent) : InstanceDeviceType(parent), parent_(parent) {}
  zx_status_t Bind(InputReportInject* base);

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { delete this; }
  zx_status_t DdkClose(uint32_t flags);

  // FIDL functions.
  void MakeDevice(fuchsia_input_report::DeviceDescriptor descriptor,
                  MakeDeviceCompleter::Sync completer) override;
  void SendInputReports(::fidl::VectorView<fuchsia_input_report::InputReport> reports,
                        SendInputReportsCompleter::Sync completer) override;

 private:
  zx_device_t* parent_;
  InputReportInject* base_ = nullptr;
  FakeInputReport* child_ = nullptr;
};

}  // namespace input_report_inject

#endif  // SRC_UI_INPUT_DRIVERS_INPUT_REPORT_INJECT_INPUT_REPORT_INJECT_INSTANCE_H_
