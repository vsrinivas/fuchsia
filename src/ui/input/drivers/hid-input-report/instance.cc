// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instance.h"

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>

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

void InputReportInstance::GetReportsEvent(GetReportsEventCompleter::Sync completer) {
  zx::event new_event;
  zx_status_t status;

  {
    fbl::AutoLock lock(&report_lock_);
    status = reports_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);
  }

  completer.Reply(status, std::move(new_event));
}

void InputReportInstance::GetDescriptor(GetDescriptorCompleter::Sync completer) {
  fidl::BufferThenHeapAllocator<kFidlDescriptorBufferSize> descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());

  base_->CreateDescriptor(&descriptor_allocator, &descriptor_builder);

  completer.Reply(descriptor_builder.build());
}

void InputReportInstance::GetReports(GetReportsCompleter::Sync completer) {
  fbl::AutoLock lock(&report_lock_);

  std::array<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports;
  size_t num_reports = 0;

  TRACE_DURATION("input", "InputReportInstance GetReports", "instance_id", instance_id_);
  while (!reports_data_.empty()) {
    TRACE_FLOW_STEP("input", "input_report", reports_data_.front().trace_id());
    reports[num_reports++] = std::move(reports_data_.front());
    reports_data_.pop();
  }

  reports_event_.signal(DEV_STATE_READABLE, 0);

  completer.Reply(fidl::VectorView<fuchsia_input_report::InputReport>(
      fidl::unowned_ptr(reports.data()), num_reports));

  // We have sent the reports so reset the allocator.
  report_allocator_.inner_allocator().reset();
}

void InputReportInstance::SendOutputReport(fuchsia_input_report::OutputReport report,
                                           SendOutputReportCompleter::Sync completer) {
  zx_status_t status = base_->SendOutputReport(std::move(report));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  }
  completer.ReplySuccess();
}

void InputReportInstance::ReceiveReport(const uint8_t* report, size_t report_size, zx_time_t time,
                                        hid_input_report::Device* device) {
  fbl::AutoLock lock(&report_lock_);

  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator_.make<fuchsia_input_report::InputReport::Frame>());

  if (device->ParseInputReport(report, report_size, &report_allocator_, &report_builder) !=
      hid_input_report::ParseResult::kOk) {
    zxlogf(ERROR, "ReceiveReport: Device failed to parse report correctly\n");
    return;
  }

  report_builder.set_event_time(report_allocator_.make<zx_time_t>(time));
  report_builder.set_trace_id(report_allocator_.make<uint64_t>(TRACE_NONCE()));

  if (reports_data_.empty()) {
    reports_event_.signal(0, DEV_STATE_READABLE);
  }
  // If we are full, pop the oldest report.
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(report_builder.build());
  TRACE_FLOW_BEGIN("input", "input_report", reports_data_.back().trace_id());
}

}  // namespace hid_input_report_dev
