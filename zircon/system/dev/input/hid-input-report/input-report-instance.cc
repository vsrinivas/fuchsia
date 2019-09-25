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
  llcpp_report::InputDevice::Dispatch(this, msg, &transaction);
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
  Descriptor descriptor_data = {};
  llcpp_report::DeviceDescriptor descriptor;

  size_t size;
  const hid_input_report::ReportDescriptor* descriptors = base_->GetDescriptors(&size);

  zx_status_t status;
  for (size_t i = 0; i < size; i++) {
    if (std::holds_alternative<hid_input_report::MouseDescriptor>(descriptors[i].descriptor)) {
      status = SetMouseDescriptor(descriptors[i], &descriptor_data);
    }
    if (status != ZX_OK) {
      break;
    }
  }

  descriptor = descriptor_data.descriptor.view();
  _completer.Reply(std::move(descriptor));
}

void InputReportInstance::GetReports(GetReportsCompleter::Sync _completer) {
  fbl::AutoLock lock(&report_lock_);
  // These two arrays store the information to build the FIDL tables.
  std::array<llcpp_report::InputReport, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports;
  std::array<Report, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports_fidl_data;

  // TODO(dgilhooley): |reports_data| can be removed if RingBuffer supports indexing.
  std::array<hid_input_report::Report, llcpp_report::MAX_DEVICE_REPORT_COUNT> reports_data;

  size_t index = 0;
  zx_status_t status = ZX_OK;
  while (!reports_data_.empty()) {
    reports_data[index] = std::move(reports_data_.front());
    reports_data_.pop();
    if (std::holds_alternative<hid_input_report::MouseReport>(reports_data[index].report)) {
      status = SetMouseReport(&reports_data[index], &reports_fidl_data[index]);
    }
    if (status != ZX_OK) {
      break;
    }
    reports[index] = reports_fidl_data[index].report.view();
    index++;
  }

  if (reports_data_.empty()) {
    reports_event_.signal(DEV_STATE_READABLE, 0);
  }

  fidl::VectorView<llcpp_report::InputReport> report_view(reports.data(), index);
  _completer.Reply(report_view);
}

void InputReportInstance::ReceiveReport(const hid_input_report::ReportDescriptor& descriptor,
                                        const hid_input_report::Report& input_report) {
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
