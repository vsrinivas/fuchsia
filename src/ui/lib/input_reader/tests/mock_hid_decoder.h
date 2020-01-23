// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
#define SRC_UI_LIB_INPUT_READER_TESTS_MOCK_HID_DECODER_H_

#include <lib/fit/function.h>
#include <lib/zx/event.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/input_reader/hid_decoder.h"

namespace ui_input {

// Mocks HidDecoder and allows sending arbitrary ReportDescriptors and Reports
// through |SendReportDescriptor| and |Send|.
class MockHidDecoder : public HidDecoder {
 public:
  MockHidDecoder() : weak_ptr_factory_(this) {}
  MockHidDecoder(std::vector<uint8_t> report_descriptor) : weak_ptr_factory_(this) {
    report_descriptor_.data = report_descriptor;
    report_descriptor_.length = report_descriptor.size();
  }
  MockHidDecoder(std::vector<uint8_t> report_descriptor, std::vector<uint8_t> initial_report)
      : weak_ptr_factory_(this) {
    report_descriptor_.data = report_descriptor;
    report_descriptor_.length = report_descriptor.size();
    report_.data = initial_report;
    report_.length = initial_report.size();
  }
  MockHidDecoder(std::vector<uint8_t> report_descriptor, BootMode boot_mode)
      : weak_ptr_factory_(this) {
    boot_mode_ = boot_mode;
    report_descriptor_.data = report_descriptor;
    report_descriptor_.length = report_descriptor.size();
  }
  ~MockHidDecoder() override;

  fxl::WeakPtr<MockHidDecoder> GetWeakPtr();

  // |HidDecoder|
  const std::string& name() const override;
  // |HidDecoder|
  bool Init() override;
  // |HidDecoder|
  zx::event GetEvent() override;
  uint32_t GetTraceId() const override { return 0; }
  // |HidDecoder|
  BootMode ReadBootMode() const override;
  // |HidDecoder|
  const std::vector<uint8_t>& ReadReportDescriptor(int* bytes_read) override;
  // |HidDecoder|
  size_t Read(uint8_t* data, size_t data_size) override;
  // |HidDecoder|
  zx_status_t Send(ReportType type, uint8_t report_id, const std::vector<uint8_t>& report) override;
  // |HidDecoder|
  zx_status_t GetReport(ReportType type, uint8_t report_id, std::vector<uint8_t>* report) override;

  // Emulates the Device sending a report, which will be read by |Read|.
  // There must not be a pending report that has not been |Read|.
  void SetHidDecoderRead(std::vector<uint8_t> bytes, int length);
  // Returns a copy of the last output report sent to |MockHidDecoder|.
  std::vector<uint8_t> GetLastOutputReport();
  // Sets the report descripter, which will be read by
  // |ReadReportDescriptor|. This should only be called once at the beginning
  // of setting up |MockHidDecoder|.
  void SetReportDescriptor(std::vector<uint8_t> bytes, int length);
  // Sets the Boot Mode, which is read by |ReadBootMode|.
  void SetBootMode(HidDecoder::BootMode boot_mode);
  // Emulates removing the device. There must not be a pending report that has
  // not been |Read|.
  void Close();

 private:
  struct Report {
    std::vector<uint8_t> data;
    // This can be shorter than the length of the |data| vector.
    size_t length;
  };

  void Signal();
  void ClearReport();

  zx::event event_;
  Report report_ = {};
  Report report_descriptor_ = {};
  std::vector<uint8_t> last_output_report_;
  HidDecoder::BootMode boot_mode_ = HidDecoder::BootMode::NONE;

  fxl::WeakPtrFactory<MockHidDecoder> weak_ptr_factory_;
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
