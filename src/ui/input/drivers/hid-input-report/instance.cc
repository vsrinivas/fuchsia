// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instance.h"

#include <lib/fidl/epitaph.h>

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
  return DdkAdd("input-report-instance", DEVICE_ADD_INSTANCE);
}

zx_status_t InputReportInstance::DdkClose(uint32_t flags) {
  base_->RemoveInstanceFromList(this);
  return ZX_OK;
}

void InputReportInstance::GetInputReportsReader(zx::channel req,
                                                GetInputReportsReaderCompleter::Sync completer) {
  fbl::AutoLock lock(&report_lock_);

  if (input_reports_reader_) {
    fidl_epitaph_write(req.get(), ZX_ERR_ALREADY_BOUND);
    return;
  }

  // Check if the loop needs to be cleaned up.
  if (loop_) {
    if (loop_->GetState() == ASYNC_LOOP_QUIT) {
      loop_->Shutdown();
    }
    if (loop_->GetState() == ASYNC_LOOP_SHUTDOWN) {
      loop_.reset();
    }
  }

  if (!loop_.has_value()) {
    loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
    zx_status_t status = loop_->StartThread("hid-input-report-reader-loop");
    if (status != ZX_OK) {
      fidl_epitaph_write(req.get(), status);
      return;
    }
  }

  input_reports_reader_ = InputReportsReader(this);

  // Invoked when the channel is closed or on any binding-related error.
  fidl::OnUnboundFn<llcpp::fuchsia::input::report::InputReportsReader::Interface> unbound_fn(
      [](llcpp::fuchsia::input::report::InputReportsReader::Interface* dev, fidl::UnboundReason,
         zx_status_t, zx::channel) {
        auto* device = static_cast<InputReportsReader*>(dev)->instance_;
        fbl::AutoLock lock(&device->report_lock_);

        if (device->loop_) {
          device->loop_->Quit();
        }

        if (device->input_reports_waiting_read_) {
          device->input_reports_waiting_read_->ReplyError(ZX_ERR_PEER_CLOSED);
          device->input_reports_waiting_read_.reset();
        }

        device->input_reports_reader_.reset();
      });

  auto binding =
      fidl::BindServer(loop_->dispatcher(), std::move(req),
                       static_cast<llcpp::fuchsia::input::report::InputReportsReader::Interface*>(
                           &input_reports_reader_.value()),
                       std::move(unbound_fn));
  if (binding.is_error()) {
    return;
  }
  input_reports_reader_binding_.emplace(std::move(binding.value()));
}

void InputReportInstance::SetWaitingReportsReader(
    InputReportsReader::ReadInputReportsCompleter::Async waiting_read) {
  fbl::AutoLock lock(&report_lock_);

  if (input_reports_waiting_read_) {
    waiting_read.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }

  input_reports_waiting_read_ = std::move(waiting_read);
  SendReportsToWaitingRead();
}

void InputReportInstance::SendReportsToWaitingRead() {
  if (reports_data_.empty()) {
    return;
  }

  std::array<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports;
  size_t num_reports = 0;

  TRACE_DURATION("input", "InputReportInstance GetReports", "instance_id", instance_id_);
  while (!reports_data_.empty()) {
    TRACE_FLOW_STEP("input", "input_report", reports_data_.front().trace_id());
    reports[num_reports++] = std::move(reports_data_.front());
    reports_data_.pop();
  }

  input_reports_waiting_read_->ReplySuccess(fidl::VectorView<fuchsia_input_report::InputReport>(
      fidl::unowned_ptr(reports.data()), num_reports));
  input_reports_waiting_read_.reset();

  // We have sent the reports so reset the allocator.
  report_allocator_.inner_allocator().reset();
}

void InputReportInstance::GetDescriptor(GetDescriptorCompleter::Sync completer) {
  fidl::BufferThenHeapAllocator<kFidlDescriptorBufferSize> descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());

  base_->CreateDescriptor(&descriptor_allocator, &descriptor_builder);

  completer.Reply(descriptor_builder.build());
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

  // If we are full, pop the oldest report.
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(report_builder.build());
  TRACE_FLOW_BEGIN("input", "input_report", reports_data_.back().trace_id());

  if (input_reports_waiting_read_) {
    SendReportsToWaitingRead();
  }
}

}  // namespace hid_input_report_dev
