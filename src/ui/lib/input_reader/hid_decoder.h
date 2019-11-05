// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_INPUT_READER_HID_DECODER_H_
#define SRC_UI_LIB_INPUT_READER_HID_DECODER_H_

#include <lib/zx/event.h>

#include <string>
#include <vector>

#include "src/ui/lib/input_reader/mouse.h"
#include "src/ui/lib/input_reader/touch.h"

namespace ui_input {

// This interface wraps the file descriptor associated with a HID input
// device and presents a simpler Read() interface. This is a transitional
// step towards fully wrapping the HID protocol.
class HidDecoder {
 public:
  enum class ReportType { INPUT, OUTPUT, FEATURE };
  // TODO(SCN-867) - The two below enums should be removed when we finally
  // remove all of the Hardcoded devices.
  enum class BootMode {
    NONE,
    MOUSE,
    KEYBOARD,
  };
  enum class Device {
    EYOYO,
    FT3X27,
    SAMSUNG,
  };

  HidDecoder();
  virtual ~HidDecoder();

  virtual const std::string& name() const = 0;

  // Inits the internal state. Returns false if any underlying call
  // fails. If so the decoder is not usable.
  virtual bool Init() = 0;

  // Returns the event that signals when the device is ready to be read.
  virtual zx::event GetEvent() = 0;

  // Get the TraceID. The full TraceID should have this ID as the bottom 32 bits and
  // the report number as the top 32 bits.
  virtual uint32_t GetTraceId() const = 0;

  // Checks if the kernel has set a bootmode for the device. If the kernel
  // has, then the hid descriptor and report must follow a specific format.
  // TODO (SCN-1266) - This should be removed when we can just run these
  // through generic HID parsers.
  virtual BootMode ReadBootMode() const = 0;

  // Reads the Report descriptor from the device.
  virtual const std::vector<uint8_t>& ReadReportDescriptor(int* bytes_read) = 0;

  // Reads up to |data_size| data of reports from the device. This API will never
  // return partial reports, so it must be given a buffer large enough to read
  // at least one report. This API may return multiple reports.
  virtual size_t Read(uint8_t* data, size_t data_size) = 0;

  // Sends a single Report to the device. |type| must be either
  // OUTPUT or FEATURE.
  virtual zx_status_t Send(ReportType type, uint8_t report_id,
                           const std::vector<uint8_t>& report) = 0;
  // Requests a given report with a given report ID from the device. GetReport
  // is an active request to the device, where Read passively waits for the
  // device to send a report.
  virtual zx_status_t GetReport(ReportType type, uint8_t report_id,
                                std::vector<uint8_t>* report) = 0;
};

}  // namespace ui_input

#endif  // SRC_UI_LIB_INPUT_READER_HID_DECODER_H_
