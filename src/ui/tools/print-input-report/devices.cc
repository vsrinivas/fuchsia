// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/print-input-report/devices.h"

#include <fuchsia/input/report/llcpp/fidl.h>
#include <zircon/status.h>

#include <ddk/device.h>

namespace print_input_report {

zx_status_t PrintInputDescriptor(Printer* printer, llcpp_report::InputDevice::SyncClient* client) {
  llcpp_report::InputDevice::ResultOf::GetDescriptor result = client->GetDescriptor();
  if (result.status() != ZX_OK) {
    printer->Print("GetDescriptor FIDL call returned %s\n", zx_status_get_string(result.status()));
    return result.status();
  }

  printer->SetIndent(0);
  if (result->descriptor.has_mouse()) {
    PrintMouseDesc(printer, result->descriptor.mouse());
  }
  return ZX_OK;
}

void PrintMouseDesc(Printer* printer, const llcpp_report::MouseDescriptor& mouse_desc) {
  printer->Print("Mouse Descriptor:\n");
  printer->IncreaseIndent();
  if (mouse_desc.has_movement_x()) {
    printer->Print("Movement X:\n");
    printer->PrintAxisIndented(mouse_desc.movement_x());
  }
  if (mouse_desc.has_movement_y()) {
    printer->Print("Movement Y:\n");
    printer->PrintAxisIndented(mouse_desc.movement_y());
  }
  if (mouse_desc.has_buttons()) {
    for (uint8_t button : mouse_desc.buttons()) {
      printer->Print("Button: %d\n", button);
    }
  }
  printer->DecreaseIndent();
}

int PrintInputReport(Printer* printer, llcpp_report::InputDevice::SyncClient* client,
                     size_t num_reads) {
  // Get the reports event.
  auto result = client->GetReportsEvent();
  if (result.status() != ZX_OK) {
    printer->Print("GetReportsEvent FIDL call returned %s\n",
                   zx_status_get_string(result.status()));
    return 1;
  }
  zx_status_t status = result->status;
  if (status != ZX_OK) {
    printer->Print("GetReportsEvent FIDL call returned %s\n", zx_status_get_string(result->status));
    return 1;
  }
  zx::event event = std::move(result->event);

  while (num_reads--) {
    // Wait on the event to be readable.
    status = event.wait_one(DEV_STATE_READABLE, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      return 1;
    }

    // Get the report.
    llcpp_report::InputDevice::ResultOf::GetReports result = client->GetReports();
    if (result.status() != ZX_OK) {
      return 1;
    }

    auto& reports = result->reports;
    for (auto& report : reports) {
      printer->SetIndent(0);
      if (report.has_mouse()) {
        auto& mouse = report.mouse();
        PrintMouseReport(printer, mouse);
      }
      printer->Print("\n");
    }
  }
  return 0;
}

void PrintMouseReport(Printer* printer, const llcpp_report::MouseReport& mouse_report) {
  if (mouse_report.has_movement_x()) {
    printer->Print("Movement x: %08ld\n", mouse_report.movement_x());
  }
  if (mouse_report.has_movement_y()) {
    printer->Print("Movement y: %08ld\n", mouse_report.movement_y());
  }
  if (mouse_report.has_pressed_buttons()) {
    for (uint8_t button : mouse_report.pressed_buttons()) {
      printer->Print("Button %02d pressed\n", button);
    }
  }
}

}  // namespace print_input_report
