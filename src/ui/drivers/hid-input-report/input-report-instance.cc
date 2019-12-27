// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report-instance.h"

#include <ddk/debug.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>

#include "input-report.h"

namespace hid_input_report_dev {

zx_status_t InputReportInstance::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_input_report::InputDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t InputReportInstance::Bind(InputReportBase* base) {
  base_ = base;

  zx_status_t status = zx::event::create(0, &reports_event_);
  if (status != ZX_OK) {
    return status;
  }

  return DdkAdd("input-report-instance", DEVICE_ADD_INSTANCE);
}

zx_status_t InputReportInstance::DdkClose(uint32_t flags) {
  base_->RemoveInstanceFromList(this);
  return ZX_OK;
}

void InputReportInstance::GetReportsEvent(GetReportsEventCompleter::Sync _completer) {
  zx::event new_event;
  zx_status_t status;

  {
    fbl::AutoLock lock(&report_lock_);
    status = reports_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);
  }

  _completer.Reply(status, std::move(new_event));
}

void InputReportInstance::GetDescriptor(GetDescriptorCompleter::Sync _completer) {
  hid_input_report::FidlDescriptor descriptor_data = {};
  fuchsia_input_report::DeviceDescriptor descriptor;

  size_t size;
  const hid_input_report::ReportDescriptor* descriptors = base_->GetDescriptors(&size);

  zx_status_t status;
  for (size_t i = 0; i < size; i++) {
    status = hid_input_report::SetFidlDescriptor(descriptors[i], &descriptor_data);
    if (status != ZX_OK) {
      break;
    }
  }

  descriptor = descriptor_data.builder.view();
  _completer.Reply(std::move(descriptor));
}

void InputReportInstance::GetReports(GetReportsCompleter::Sync _completer) {
  fbl::AutoLock lock(&report_lock_);

  size_t index = 0;
  zx_status_t status = ZX_OK;
  while (!reports_data_.empty()) {
    status =
        hid_input_report::SetFidlInputReport(reports_data_.front(), &reports_fidl_data_[index]);
    reports_data_.pop();
    if (status != ZX_OK) {
      break;
    }
    reports_[index] = reports_fidl_data_[index].builder.view();
    index++;
  }

  if (reports_data_.empty()) {
    reports_event_.signal(DEV_STATE_READABLE, 0);
  }

  fidl::VectorView<fuchsia_input_report::InputReport> report_view(reports_.data(), index);
  _completer.Reply(report_view);
}

void InputReportInstance::SendOutputReport(fuchsia_input_report::OutputReport report,
                                           SendOutputReportCompleter::Sync completer) {
  zx_status_t status = base_->SendOutputReport(std::move(report));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  }
  completer.ReplySuccess();
}

void InputReportInstance::ReceiveReport(const hid_input_report::ReportDescriptor& descriptor,
                                        const hid_input_report::InputReport& input_report) {
  fbl::AutoLock lock(&report_lock_);

  if (reports_data_.empty()) {
    reports_event_.signal(0, DEV_STATE_READABLE);
  }
  // If we are full, pop the oldest report.
  if (reports_data_.full()) {
    reports_data_.pop();
  }
  reports_data_.push(input_report);
}

}  // namespace hid_input_report_dev
