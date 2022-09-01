// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver_v1.h"

#include "src/ui/input/drivers/hid-input-report/hid_input_report_bind.h"

namespace hid_input_report_dev {

zx_status_t InputReportDriver::Bind() {
  zx_status_t status = input_report_.Start();
  if (status != ZX_OK) {
    return status;
  }
  status = DdkAdd(ddk::DeviceAddArgs("InputReport").set_inspect_vmo(input_report_.InspectVmo()));
  if (status != ZX_OK) {
    input_report_.Stop();
    return status;
  }
  return ZX_OK;
}

void InputReportDriver::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void InputReportDriver::DdkRelease() {
  input_report_.Stop();
  delete this;
}

void InputReportDriver::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                              GetInputReportsReaderCompleter::Sync& completer) {
  input_report_.GetInputReportsReader(request, completer);
}

void InputReportDriver::GetDescriptor(GetDescriptorCompleter::Sync& completer) {
  input_report_.GetDescriptor(completer);
}
void InputReportDriver::SendOutputReport(SendOutputReportRequestView request,
                                         SendOutputReportCompleter::Sync& completer) {
  input_report_.SendOutputReport(request, completer);
}

void InputReportDriver::GetFeatureReport(GetFeatureReportCompleter::Sync& completer) {
  input_report_.GetFeatureReport(completer);
}
void InputReportDriver::SetFeatureReport(SetFeatureReportRequestView request,
                                         SetFeatureReportCompleter::Sync& completer) {
  input_report_.SetFeatureReport(request, completer);
}
void InputReportDriver::GetInputReport(GetInputReportRequestView request,
                                       GetInputReportCompleter::Sync& completer) {
  input_report_.GetInputReport(request, completer);
}

zx_status_t input_report_bind_v1(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;

  ddk::HidDeviceProtocolClient hiddev(parent);
  if (!hiddev.is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  auto dev = fbl::make_unique_checked<InputReportDriver>(&ac, parent, hiddev);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t input_report_driver_ops_v1 = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = input_report_bind_v1;
  return ops;
}();

}  // namespace hid_input_report_dev

ZIRCON_DRIVER(hid_input_report, hid_input_report_dev::input_report_driver_ops_v1, "zircon", "0.1");
