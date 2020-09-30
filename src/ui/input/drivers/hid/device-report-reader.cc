// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-report-reader.h"

#include <ddk/trace/event.h>

#include "hid.h"

namespace hid_driver {

static constexpr uint64_t hid_report_trace_id(uint32_t instance_id, uint64_t report_id) {
  return (report_id << 32) | instance_id;
}

zx_status_t DeviceReportsReader::ReadReportFromFifo(uint8_t* buf, size_t buf_size, zx_time_t* time,
                                                    size_t* out_report_size) {
  if (data_fifo_.empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  uint8_t report_id = data_fifo_.front();

  size_t report_size = base_->GetReportSizeById(report_id, ReportType::INPUT);
  if (report_size == 0) {
    zxlogf(ERROR, "error reading hid device: unknown report id (%u)!", report_id);
    return ZX_ERR_BAD_STATE;
  }

  // Check if we have enough room left in the buffer.
  if (report_size > buf_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if (report_size > data_fifo_.size()) {
    // Something went wrong. The fifo should always contain full reports in it.
    return ZX_ERR_INTERNAL;
  }

  for (size_t i = 0; i < report_size; i++) {
    buf[i] = data_fifo_.front();
    data_fifo_.pop();
  }

  *out_report_size = report_size;
  *time = timestamps_.front();
  timestamps_.pop();

  reports_sent_ += 1;
  TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(trace_id_, reports_sent_));

  return ZX_OK;
}

void DeviceReportsReader::ReadReports(ReadReportsCompleter::Sync completer) {
  fbl::AutoLock lock(&reader_lock_);
  if (waiting_read_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }

  waiting_read_ = completer.ToAsync();

  zx_status_t status = SendReports();
  if ((status != ZX_OK) && (status != ZX_ERR_SHOULD_WAIT)) {
    zxlogf(ERROR, "ReadReports SendReports failed %d\n", status);
  }
}

zx_status_t DeviceReportsReader::SendReports() {
  if (!waiting_read_) {
    return ZX_ERR_BAD_STATE;
  }
  if (data_fifo_.size() == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }

  std::array<uint8_t, ::llcpp::fuchsia::hardware::input::MAX_REPORT_DATA> buf;
  size_t buf_index = 0;

  std::array<::llcpp::fuchsia::hardware::input::Report,
             ::llcpp::fuchsia::hardware::input::MAX_REPORTS_COUNT>
      reports;
  size_t reports_size = 0;
  zx_status_t status = ZX_OK;

  {
    zx_time_t time;
    while (status == ZX_OK) {
      size_t report_size;
      status =
          ReadReportFromFifo(buf.data() + buf_index, buf.size() - buf_index, &time, &report_size);
      if (status == ZX_OK) {
        reports[reports_size].time = time;
        reports[reports_size].data =
            fidl::VectorView<uint8_t>(fidl::unowned_ptr(buf.data() + buf_index), report_size);
        reports_size++;
        buf_index += report_size;
      }
    }
  }

  if ((buf_index > 0) && ((status == ZX_ERR_BUFFER_TOO_SMALL) || (status == ZX_ERR_SHOULD_WAIT))) {
    status = ZX_OK;
  }

  if (status != ZX_OK) {
    waiting_read_->ReplyError(status);
    waiting_read_.reset();
    return status;
  }

  waiting_read_->ReplySuccess(::fidl::VectorView<llcpp::fuchsia::hardware::input::Report>(
      fidl::unowned_ptr(reports.data()), reports_size));
  waiting_read_.reset();

  return ZX_OK;
}

zx_status_t DeviceReportsReader::WriteToFifo(const uint8_t* report, size_t report_len,
                                             zx_time_t time) {
  fbl::AutoLock lock(&reader_lock_);

  if (timestamps_.full()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  if ((data_fifo_.capacity() - data_fifo_.size()) < report_len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  for (size_t i = 0; i < report_len; i++) {
    data_fifo_.push(report[i]);
  }
  timestamps_.push(time);

  TRACE_FLOW_BEGIN("input", "hid_report", hid_report_trace_id(trace_id_, reports_written_));
  ++reports_written_;
  if (waiting_read_) {
    zx_status_t status = SendReports();
    if (status != ZX_OK) {
      zxlogf(ERROR, "WriteToFifo SendReports failed %d\n", status);
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace hid_driver
