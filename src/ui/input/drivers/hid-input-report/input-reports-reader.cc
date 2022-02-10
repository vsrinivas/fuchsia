// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-reports-reader.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>

#include <fbl/auto_lock.h>

namespace hid_input_report_dev {

std::unique_ptr<InputReportsReader> InputReportsReader::Create(
    InputReportBase* base, uint32_t reader_id, async_dispatcher_t* dispatcher,
    fidl::ServerEnd<fuchsia_input_report::InputReportsReader> request) {
  // Invoked when the channel is closed or on any binding-related error.
  fidl::OnUnboundFn<InputReportsReader> unbound_fn(
      [](InputReportsReader* device, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_input_report::InputReportsReader>) {
        {
          fbl::AutoLock lock(&device->readers_lock_);
          // Any pending LLCPP completer must be either replied to or closed before we destroy it.
          if (device->waiting_read_) {
            device->waiting_read_->Close(ZX_ERR_PEER_CLOSED);
            device->waiting_read_.reset();
          }
        }
        // This frees the InputReportsReader class.
        device->base_->RemoveReaderFromList(device);
      });

  auto reader = std::make_unique<InputReportsReader>(base, reader_id);
  auto binding =
      fidl::BindServer(dispatcher, std::move(request), reader.get(), std::move(unbound_fn));
  fbl::AutoLock lock(&reader->readers_lock_);
  reader->binding_.emplace(std::move(binding));
  return reader;
}

void InputReportsReader::ReadInputReports(ReadInputReportsRequestView request,
                                          ReadInputReportsCompleter::Sync& completer) {
  fbl::AutoLock lock(&readers_lock_);

  if (waiting_read_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }

  waiting_read_ = completer.ToAsync();
  SendReportsToWaitingRead();
}

void InputReportsReader::SendReportsToWaitingRead() {
  if (reports_data_.empty()) {
    return;
  }

  std::array<fuchsia_input_report::wire::InputReport,
             fuchsia_input_report::wire::kMaxDeviceReportCount>
      reports;
  size_t num_reports = 0;

  TRACE_DURATION("input", "InputReportInstance GetReports", "instance_id", reader_id_);
  while (!reports_data_.empty()) {
    TRACE_FLOW_STEP("input", "input_report", reports_data_.front().trace_id());
    reports[num_reports++] = std::move(reports_data_.front());
    reports_data_.pop();
  }

  fidl::Result result = waiting_read_->ReplySuccess(
      fidl::VectorView<fuchsia_input_report::wire::InputReport>::FromExternal(reports.data(),
                                                                              num_reports));
  if (!result.ok()) {
    zxlogf(ERROR, "SendReport: Failed to send reports: %s\n", result.FormatDescription().c_str());
  }
  waiting_read_.reset();

  // We have sent the reports so reset the allocator.
  report_allocator_.Reset();
}

void InputReportsReader::ReceiveReport(const uint8_t* raw_report, size_t raw_report_size,
                                       zx_time_t time, hid_input_report::Device* device) {
  fbl::AutoLock lock(&readers_lock_);

  fuchsia_input_report::wire::InputReport report(report_allocator_);

  if (!device->InputReportId().has_value()) {
    zxlogf(ERROR, "ReceiveReport: Device cannot receive input reports\n");
    return;
  }

  if (device->ParseInputReport(raw_report, raw_report_size, report_allocator_, report) !=
      hid_input_report::ParseResult::kOk) {
    zxlogf(ERROR, "ReceiveReport: Device failed to parse report correctly\n");
    return;
  }

  report.set_report_id(*device->InputReportId());
  report.set_event_time(report_allocator_, time);
  report.set_trace_id(report_allocator_, TRACE_NONCE());

  // If we are full, pop the oldest report.
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(std::move(report));
  TRACE_FLOW_BEGIN("input", "input_report", reports_data_.back().trace_id());

  if (waiting_read_) {
    SendReportsToWaitingRead();
  }
}

}  // namespace hid_input_report_dev
