// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
#define SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_

#include <fuchsia/input/report/llcpp/fidl.h>

namespace hid_input_report_dev {

class InputReportInstance;

class InputReportsReader : public ::llcpp::fuchsia::input::report::InputReportsReader::Interface {
 public:
  // The instance driver has to exist for the lifetime of the ReportsReader.
  // The pointer to InputReportInstance is unowned.
  // InputReportsReader will be freed by InputReportsInstance.
  explicit InputReportsReader(InputReportInstance* instance) : instance_(instance) {}

  // FIDL functions.
  void ReadInputReports(ReadInputReportsCompleter::Sync completer) override;

  InputReportInstance* instance_;
};

}  // namespace hid_input_report_dev

#endif  // SRC_UI_INPUT_DRIVERS_HID_INPUT_REPORT_INPUT_REPORTS_READER_H_
