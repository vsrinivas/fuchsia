// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
#define SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_

#include "src/ui/tools/print-input-report/printer.h"

namespace print_input_report {

zx_status_t PrintInputDescriptor(Printer* printer,
                                 fuchsia_input_report::InputDevice::SyncClient* client);
int PrintInputReport(Printer* printer, fuchsia_input_report::InputDevice::SyncClient* client,
                     size_t num_reads);

void PrintMouseDesc(Printer* printer, const fuchsia_input_report::MouseInputDescriptor& mouse_desc);
void PrintMouseInputReport(Printer* printer,
                           const fuchsia_input_report::MouseInputReport& mouse_report);

void PrintSensorDesc(Printer* printer,
                     const fuchsia_input_report::SensorInputDescriptor& sensor_desc);
void PrintSensorInputReport(Printer* printer,
                            const fuchsia_input_report::SensorInputReport& sensor_report);

void PrintTouchDesc(Printer* printer, const fuchsia_input_report::TouchInputDescriptor& touch_desc);
void PrintTouchInputReport(Printer* printer,
                           const fuchsia_input_report::TouchInputReport& touch_report);

void PrintKeyboardDesc(Printer* printer,
                       const fuchsia_input_report::KeyboardInputDescriptor& keyboard_desc);
void PrintKeyboardInputReport(Printer* printer,
                              const fuchsia_input_report::KeyboardInputReport& keyboard_report);

}  // namespace print_input_report

#endif  // SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
