// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
#define SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_

#include "src/ui/tools/print-input-report/printer.h"

namespace print_input_report {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

zx_status_t PrintInputDescriptor(Printer* printer, llcpp_report::InputDevice::SyncClient* client);
int PrintInputReport(Printer* printer, llcpp_report::InputDevice::SyncClient* client,
                     size_t num_reads);

void PrintMouseDesc(Printer* printer, const llcpp_report::MouseDescriptor& mouse_desc);
void PrintMouseReport(Printer* printer, const llcpp_report::MouseReport& mouse_report);

}  // namespace print_input_report

#endif  // SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
