// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
#define SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/zx/status.h>

#include "src/ui/tools/print-input-report/printer.h"

namespace print_input_report {

zx::result<fidl::WireSharedClient<fuchsia_input_report::InputReportsReader>> GetReaderClient(
    fidl::WireSharedClient<fuchsia_input_report::InputDevice>* client,
    async_dispatcher_t* dispatcher);

zx_status_t PrintFeatureReports(
    std::string filename, Printer* printer,
    fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
    fit::closure callback = [] {});
zx_status_t PrintInputDescriptor(
    std::string filename, Printer* printer,
    fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
    fit::closure callback = [] {});
void PrintInputReports(
    std::string filename, Printer* printer,
    fidl::WireSharedClient<fuchsia_input_report::InputReportsReader> reader, size_t num_reads,
    fit::closure callback = [] {});
void GetAndPrintInputReport(
    std::string filename, fuchsia_input_report::wire::DeviceType device_type, Printer* printer,
    fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
    fit::closure callback = [] {});

void PrintMouseDesc(Printer* printer,
                    const fuchsia_input_report::wire::MouseInputDescriptor& mouse_desc);
void PrintMouseInputReport(Printer* printer,
                           const fuchsia_input_report::wire::MouseInputReport& mouse_report);

void PrintSensorDesc(Printer* printer,
                     const fuchsia_input_report::wire::SensorInputDescriptor& sensor_desc);
void PrintSensorInputReport(Printer* printer,
                            const fuchsia_input_report::wire::SensorInputReport& sensor_report);

void PrintTouchDesc(Printer* printer,
                    const fuchsia_input_report::wire::TouchDescriptor& touch_desc);
void PrintTouchInputReport(Printer* printer,
                           const fuchsia_input_report::wire::TouchInputReport& touch_report);

void PrintKeyboardDesc(Printer* printer,
                       const fuchsia_input_report::wire::KeyboardDescriptor& keyboard_desc);
void PrintKeyboardInputReport(
    Printer* printer, const fuchsia_input_report::wire::KeyboardInputReport& keyboard_report);

void PrintConsumerControlDesc(
    Printer* printer, const fuchsia_input_report::wire::ConsumerControlDescriptor& descriptor);
void PrintConsumerControlInputReport(
    Printer* printer, const fuchsia_input_report::wire::ConsumerControlInputReport& report);

}  // namespace print_input_report

#endif  // SRC_UI_TOOLS_PRINT_INPUT_REPORT_DEVICES_H_
