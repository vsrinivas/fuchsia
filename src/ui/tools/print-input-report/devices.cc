// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/print-input-report/devices.h"

#include <fuchsia/input/report/llcpp/fidl.h>
#include <zircon/status.h>

#include <ddk/device.h>

namespace print_input_report {

zx_status_t PrintInputDescriptor(Printer* printer,
                                 fuchsia_input_report::InputDevice::SyncClient* client) {
  fuchsia_input_report::InputDevice::ResultOf::GetDescriptor result = client->GetDescriptor();
  if (result.status() != ZX_OK) {
    printer->Print("GetDescriptor FIDL call returned %s\n", zx_status_get_string(result.status()));
    return result.status();
  }

  printer->SetIndent(0);
  if (result->descriptor.has_mouse()) {
    if (result->descriptor.mouse().has_input()) {
      PrintMouseDesc(printer, result->descriptor.mouse().input());
    }
  }
  if (result->descriptor.has_sensor()) {
    if (result->descriptor.sensor().has_input()) {
      PrintSensorDesc(printer, result->descriptor.sensor().input());
    }
  }
  if (result->descriptor.has_touch()) {
    PrintTouchDesc(printer, result->descriptor.touch().input());
  }
  if (result->descriptor.has_keyboard()) {
    if (result->descriptor.keyboard().has_input()) {
      PrintKeyboardDesc(printer, result->descriptor.keyboard().input());
    }
  }
  return ZX_OK;
}

void PrintMouseDesc(Printer* printer,
                    const fuchsia_input_report::MouseInputDescriptor& mouse_desc) {
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

void PrintSensorDesc(Printer* printer,
                     const fuchsia_input_report::SensorInputDescriptor& sensor_desc) {
  printer->Print("Sensor Descriptor:\n");
  if (!sensor_desc.has_values()) {
    return;
  }

  printer->IncreaseIndent();
  for (size_t i = 0; i < sensor_desc.values().count(); i++) {
    printer->Print("Value %02d:\n", i);
    printer->IncreaseIndent();
    printer->Print("SensorType: %s\n", printer->SensorTypeToString(sensor_desc.values()[i].type));
    printer->PrintAxis(sensor_desc.values()[i].axis);
    printer->DecreaseIndent();
  }
  printer->DecreaseIndent();
}

void PrintTouchDesc(Printer* printer,
                    const fuchsia_input_report::TouchInputDescriptor& touch_desc) {
  printer->Print("Touch Descriptor:\n");
  printer->IncreaseIndent();
  if (touch_desc.has_touch_type()) {
    printer->Print("Touch Type: %s\n", printer->TouchTypeToString(touch_desc.touch_type()));
  }
  if (touch_desc.has_max_contacts()) {
    printer->Print("Max Contacts: %ld\n", touch_desc.max_contacts());
  }
  if (touch_desc.has_contacts()) {
    for (size_t i = 0; i < touch_desc.contacts().count(); i++) {
      const fuchsia_input_report::ContactInputDescriptor& contact = touch_desc.contacts()[i];

      printer->Print("Contact: %02d\n", i);
      printer->IncreaseIndent();

      if (contact.has_position_x()) {
        printer->Print("Position X:\n");
        printer->PrintAxisIndented(contact.position_x());
      }
      if (contact.has_position_y()) {
        printer->Print("Position Y:\n");
        printer->PrintAxisIndented(contact.position_y());
      }
      if (contact.has_pressure()) {
        printer->Print("Pressure:\n");
        printer->PrintAxisIndented(contact.pressure());
      }
      if (contact.has_contact_width()) {
        printer->Print("Contact Width:\n");
        printer->PrintAxisIndented(contact.contact_width());
      }
      if (contact.has_contact_height()) {
        printer->Print("Contact Height:\n");
        printer->PrintAxisIndented(contact.contact_height());
      }

      printer->DecreaseIndent();
    }
  }
  printer->DecreaseIndent();
}

void PrintKeyboardDesc(Printer* printer,
                       const fuchsia_input_report::KeyboardInputDescriptor& keyboard_desc) {
  printer->Print("Keyboard Descriptor:\n");
  printer->IncreaseIndent();
  if (keyboard_desc.has_keys()) {
    for (size_t i = 0; i < keyboard_desc.keys().count(); i++) {
      printer->Print("Key: %8ld\n", keyboard_desc.keys()[i]);
    }
  }
  printer->DecreaseIndent();
}

int PrintInputReport(Printer* printer, fuchsia_input_report::InputDevice::SyncClient* client,
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
    fuchsia_input_report::InputDevice::ResultOf::GetReports result = client->GetReports();
    if (result.status() != ZX_OK) {
      printer->Print("GetReports FIDL call returned %s\n", zx_status_get_string(result.status()));
      return 1;
    }

    auto& reports = result->reports;
    for (auto& report : reports) {
      printer->SetIndent(0);
      if (report.has_mouse()) {
        auto& mouse = report.mouse();
        PrintMouseInputReport(printer, mouse);
      }
      if (report.has_sensor()) {
        PrintSensorInputReport(printer, report.sensor());
      }
      if (report.has_touch()) {
        PrintTouchInputReport(printer, report.touch());
      }
      if (report.has_keyboard()) {
        PrintKeyboardInputReport(printer, report.keyboard());
      }
      printer->Print("\n");
    }
  }
  return 0;
}

void PrintMouseInputReport(Printer* printer,
                           const fuchsia_input_report::MouseInputReport& mouse_report) {
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

void PrintSensorInputReport(Printer* printer,
                            const fuchsia_input_report::SensorInputReport& sensor_report) {
  if (!sensor_report.has_values()) {
    return;
  }

  for (size_t i = 0; i < sensor_report.values().count(); i++) {
    printer->Print("Sensor[%02d]: %08d\n", i, sensor_report.values()[i]);
  }
}

void PrintTouchInputReport(Printer* printer,
                           const fuchsia_input_report::TouchInputReport& touch_report) {
  if (touch_report.has_contacts()) {
    for (size_t i = 0; i < touch_report.contacts().count(); i++) {
      const fuchsia_input_report::ContactInputReport& contact = touch_report.contacts()[i];

      if (contact.has_contact_id()) {
        printer->Print("Contact ID: %2ld\n", contact.contact_id());
      } else {
        printer->Print("Contact: %2d\n", i);
      }

      printer->IncreaseIndent();
      if (contact.has_position_x()) {
        printer->Print("Position X:     %08ld\n", contact.position_x());
      }
      if (contact.has_position_y()) {
        printer->Print("Position Y:     %08ld\n", contact.position_y());
      }
      if (contact.has_pressure()) {
        printer->Print("Pressure:       %08ld\n", contact.pressure());
      }
      if (contact.has_contact_width()) {
        printer->Print("Contact Width:  %08ld\n", contact.contact_width());
      }
      if (contact.has_contact_height()) {
        printer->Print("Contact Height: %08ld\n", contact.contact_height());
      }

      printer->DecreaseIndent();
    }
  }
}

void PrintKeyboardInputReport(Printer* printer,
                              const fuchsia_input_report::KeyboardInputReport& keyboard_report) {
  printer->Print("Keyboard Report\n");
  printer->IncreaseIndent();
  if (keyboard_report.has_pressed_keys()) {
    for (size_t i = 0; i < keyboard_report.pressed_keys().count(); i++) {
      printer->Print("Key: %8ld\n", keyboard_report.pressed_keys()[i]);
    }
    if (keyboard_report.pressed_keys().count() == 0) {
      printer->Print("No keys pressed\n");
    }
  }
  printer->DecreaseIndent();
}

}  // namespace print_input_report
