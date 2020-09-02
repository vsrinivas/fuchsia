// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-reports-reader.h"

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_lock.h>

namespace hid_input_report_dev {

std::unique_ptr<InputReportsReader> InputReportsReader::Create(InputReportBase* base,
                                                               uint32_t reader_id,
                                                               async_dispatcher_t* dispatcher,
                                                               zx::channel req) {
  // Invoked when the channel is closed or on any binding-related error.
  fidl::OnUnboundFn<llcpp::fuchsia::input::report::InputReportsReader::Interface> unbound_fn(
      [](llcpp::fuchsia::input::report::InputReportsReader::Interface* dev, fidl::UnbindInfo info,
         zx::channel) {
        auto* device = static_cast<InputReportsReader*>(dev);

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
  auto binding = fidl::BindServer(
      dispatcher, std::move(req),
      static_cast<llcpp::fuchsia::input::report::InputReportsReader::Interface*>(reader.get()),
      std::move(unbound_fn));
  fbl::AutoLock lock(&reader->readers_lock_);
  if (binding.is_error()) {
    zxlogf(ERROR, "InputReportsReader::Create: Failed to BindServer %d\n", binding.error());
    return nullptr;
  }

  reader->binding_.emplace(std::move(binding.value()));
  return reader;
}

void InputReportsReader::ReadInputReports(ReadInputReportsCompleter::Sync completer) {
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

  std::array<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports;
  size_t num_reports = 0;

  TRACE_DURATION("input", "InputReportInstance GetReports", "instance_id", reader_id_);
  while (!reports_data_.empty()) {
    TRACE_FLOW_STEP("input", "input_report", reports_data_.front().trace_id());
    reports[num_reports++] = std::move(reports_data_.front());
    reports_data_.pop();
  }

  fidl::Result result =
      waiting_read_->ReplySuccess(fidl::VectorView<fuchsia_input_report::InputReport>(
          fidl::unowned_ptr(reports.data()), num_reports));
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "SendReport: Failed to send reports (%s): %s\n", result.status_string(),
           result.error());
  }
  waiting_read_.reset();

  // We have sent the reports so reset the allocator.
  report_allocator_.inner_allocator().reset();
}

void InputReportsReader::ReceiveReport(const uint8_t* report, size_t report_size, zx_time_t time,
                                       hid_input_report::Device* device) {
  fbl::AutoLock lock(&readers_lock_);

  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator_.make<fuchsia_input_report::InputReport::Frame>());

  if (device->ParseInputReport(report, report_size, &report_allocator_, &report_builder) !=
      hid_input_report::ParseResult::kOk) {
    zxlogf(ERROR, "ReceiveReport: Device failed to parse report correctly\n");
    return;
  }

  report_builder.set_event_time(report_allocator_.make<zx_time_t>(time));
  report_builder.set_trace_id(report_allocator_.make<uint64_t>(TRACE_NONCE()));

  // If we are full, pop the oldest report.
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(report_builder.build());
  TRACE_FLOW_BEGIN("input", "input_report", reports_data_.back().trace_id());

  if (waiting_read_) {
    SendReportsToWaitingRead();
  }
}

}  // namespace hid_input_report_dev
