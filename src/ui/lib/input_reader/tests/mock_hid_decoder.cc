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
  FXL_CHECK(report_descriptor_.length != 0);
  *bytes_read = report_descriptor_.length;
  return report_descriptor_.data;
}

size_t MockHidDecoder::Read(uint8_t* data, size_t data_size) {
  FXL_CHECK(report_.length != 0);
  FXL_CHECK(data_size > report_.length);

  memcpy(data, report_.data.data(), report_.length);

  int bytes_read = report_.length;
  ClearReport();
  return bytes_read;
}

zx_status_t MockHidDecoder::Send(ReportType type, uint8_t report_id,
                                 const std::vector<uint8_t>& report) {
  last_output_report_ = report;
  return ZX_OK;
}

std::vector<uint8_t> MockHidDecoder::GetLastOutputReport() { return last_output_report_; }

zx_status_t MockHidDecoder::GetReport(ReportType type, uint8_t report_id,
                                      std::vector<uint8_t>* report) {
  FXL_CHECK(report_.length != 0);
  // Copy the report data over
  *report = report_.data;
  ClearReport();
  return ZX_OK;
}

void MockHidDecoder::SetHidDecoderRead(std::vector<uint8_t> bytes, int length) {
  FXL_CHECK(report_.length == 0);
  report_.data = std::move(bytes);
  report_.length = length;
  Signal();
}

void MockHidDecoder::SetReportDescriptor(std::vector<uint8_t> bytes, int length) {
  FXL_CHECK(report_descriptor_.length == 0);
  report_descriptor_.data = std::move(bytes);
  report_descriptor_.length = length;
}

void MockHidDecoder::SetBootMode(HidDecoder::BootMode boot_mode) { boot_mode_ = boot_mode; }

void MockHidDecoder::Close() {
  // Signalling while the device is not readable indicates that it should be
  // removed.
  FXL_CHECK(report_.length == 0);
  Signal();
}

void MockHidDecoder::Signal() { FXL_CHECK(event_.signal(0, ZX_USER_SIGNAL_0) == ZX_OK); }

void MockHidDecoder::ClearReport() {
  report_.length = 0;
  FXL_CHECK(event_.signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
}

}  // namespace ui_input
