// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_FDIO_HID_DECODER_H_
#define SRC_UI_LIB_INPUT_READER_FDIO_HID_DECODER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fzl/fdio.h>

#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/ui/lib/input_reader/hid_decoder.h"
#include "src/ui/lib/input_reader/mouse.h"
#include "src/ui/lib/input_reader/touch.h"

namespace fzl {
class FdioCaller;
}

namespace hid {
struct ReportField;
}

namespace ui_input {

// This is the "real" FDIO implementation of |HidDecoder|.
// FdioHidDecoder takes ownership of an fd that represents a single Hid device.
// The FdioHidDecoder sends reports to and from the Hid device for the lifetime
// of the Hid device.
class FdioHidDecoder : public HidDecoder {
 public:
  FdioHidDecoder(const std::string& name, fbl::unique_fd fd);
  ~FdioHidDecoder() override;

  // |HidDecoder|
  const std::string& name() const override { return name_; }

  // |HidDecoder|
  bool Init() override;
  // |HidDecoder|
  zx::event GetEvent() override;
  // |HidDecoder|
  uint32_t GetTraceId() const override { return trace_id_; }
  // |HidDecoder|
  BootMode ReadBootMode() const override { return boot_mode_; }
  // |HidDecoder|
  const std::vector<uint8_t>& ReadReportDescriptor(int* bytes_read) override;
  // |HidDecoder|
  size_t Read(uint8_t* data, size_t data_size) override;
  // |HidDecoder|
  zx_status_t Send(ReportType type, uint8_t report_id, const std::vector<uint8_t>& report) override;
  // |HidDecoder|
  zx_status_t GetReport(ReportType type, uint8_t report_id, std::vector<uint8_t>* report) override;

 private:
  fzl::FdioCaller caller_;
  const std::string name_;
  BootMode boot_mode_ = BootMode::NONE;
  std::vector<uint8_t> report_descriptor_;
  uint32_t trace_id_ = 0;
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_READER_FDIO_HID_DECODER_H_
