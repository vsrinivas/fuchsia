// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/tests/mock_hid_decoder.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/input_reader/touch.h"

namespace ui_input {

namespace {

const std::string kDeviceName = "MockHidDecoder";

}

MockHidDecoder::~MockHidDecoder() = default;

fxl::WeakPtr<MockHidDecoder> MockHidDecoder::GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

const std::string& MockHidDecoder::name() const { return kDeviceName; }

bool MockHidDecoder::Init() { return true; }

zx::event MockHidDecoder::GetEvent() {
  zx::event dup;
  zx::event::create(0, &event_);
  event_.duplicate(ZX_RIGHTS_BASIC, &dup);
  // If any errors occur, returning ZX_HANDLE_INVALID is fine.
  return dup;
}

HidDecoder::BootMode MockHidDecoder::ReadBootMode() const { return boot_mode_; }

const std::vector<uint8_t>& MockHidDecoder::ReadReportDescriptor(int* bytes_read) {
  FXL_CHECK(report_descriptor_.size() != 0);
  *bytes_read = report_descriptor_.size();
  return report_descriptor_;
}

zx_status_t MockHidDecoder::Read(uint8_t* data, size_t data_size, size_t* report_size,
                                 zx_time_t* timestamp) {
  if (reports_.empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  if (data_size < reports_.front().first.size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(data, reports_.front().first.data(), reports_.front().first.size());
  *report_size = reports_.front().first.size();
  *timestamp = reports_.front().second;

  ClearReport();

  return ZX_OK;
}

zx_status_t MockHidDecoder::Send(ReportType type, uint8_t report_id,
                                 const std::vector<uint8_t>& report) {
  last_output_report_ = report;
  return ZX_OK;
}

std::vector<uint8_t> MockHidDecoder::GetLastOutputReport() { return last_output_report_; }

zx_status_t MockHidDecoder::GetReport(ReportType type, uint8_t report_id,
                                      std::vector<uint8_t>* report) {
  FXL_CHECK(!reports_.empty());
  // Copy the report data over
  *report = reports_.front().first;

  ClearReport();
  return ZX_OK;
}

void MockHidDecoder::QueueDeviceReport(std::vector<uint8_t> bytes) { reports_.push({bytes, 0}); }

void MockHidDecoder::QueueDeviceReport(std::vector<uint8_t> bytes, zx_time_t timestamp) {
  reports_.push({bytes, timestamp});
}

void MockHidDecoder::SignalDeviceRead() { Signal(); }

void MockHidDecoder::SetReportDescriptor(std::vector<uint8_t> bytes) {
  FXL_CHECK(report_descriptor_.size() == 0);
  report_descriptor_ = bytes;
}

void MockHidDecoder::SetBootMode(HidDecoder::BootMode boot_mode) { boot_mode_ = boot_mode; }

void MockHidDecoder::Close() {
  // Signalling while the device is not readable indicates that it should be
  // removed.
  FXL_CHECK(reports_.size() == 0);
  Signal();
}

void MockHidDecoder::Signal() { FXL_CHECK(event_.signal(0, ZX_USER_SIGNAL_0) == ZX_OK); }

void MockHidDecoder::ClearReport() {
  reports_.pop();
  FXL_CHECK(event_.signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
}

}  // namespace ui_input
