// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report-inject-instance.h"

#include "input-report-inject.h"

namespace input_report_inject {

zx_status_t InputReportInjectInstance::DdkClose(uint32_t flags) {
  base_->RemoveInstanceFromList(this);
  return ZX_OK;
}

void InputReportInjectInstance::MakeDevice(fuchsia_input_report::DeviceDescriptor descriptor,
                                           MakeDeviceCompleter::Sync completer) {
  child_ = FakeInputReport::Create(parent_, std::move(descriptor));
  completer.ReplySuccess();
}

void InputReportInjectInstance::SendInputReports(
    ::fidl::VectorView<fuchsia_input_report::InputReport> reports,
    SendInputReportsCompleter::Sync completer) {
  if (child_ == nullptr) {
    zxlogf(ERROR, "InputInject: Must call MakeDevice before calling SendInputReports!\n");
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  child_->ReceiveInput(std::move(reports));
  completer.ReplySuccess();
}

zx_status_t InputReportInjectInstance::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_input_inject::FakeInputReportDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t InputReportInjectInstance::Bind(InputReportInject* base) {
  base_ = base;
  return DdkAdd("InputReportInjectInstance", DEVICE_ADD_INSTANCE);
}

}  // namespace input_report_inject
