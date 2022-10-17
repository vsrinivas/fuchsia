// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/print-input-report/devices.h"

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/ddk/device.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

namespace print_input_report {

zx_status_t PrintFeatureReports(std::string filename, Printer* printer,
                                fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
                                fit::closure callback) {
  client->GetFeatureReport().ThenExactlyOnce(
      [filename, printer, callback = std::move(callback), _ = client.Clone()](
          fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetFeatureReport>&
              call_result) {
        if (!call_result.ok()) {
          return;
        }
        auto* result = call_result.Unwrap();
        if (result->is_error()) {
          callback();
          return;
        }
        auto& report = result->value()->report;

        printer->SetIndent(0);
        printer->Print("Feature Report from file: %s\n", filename.c_str());
        printer->IncreaseIndent();
        if (report.has_sensor()) {
          printer->Print("Sensor Feature Report:\n");
          printer->IncreaseIndent();
          if (report.sensor().has_report_interval()) {
            printer->Print("Report Interval: %ld\n", report.sensor().report_interval());
          }
          if (report.sensor().has_reporting_state()) {
            printer->Print("Reporting State: %u\n", report.sensor().reporting_state());
          }
          if (report.sensor().has_sensitivity()) {
            printer->Print("Sensitivity:\n");
            printer->IncreaseIndent();
            for (size_t i = 0; i < report.sensor().sensitivity().count(); i++) {
              printer->Print("%ld ", report.sensor().sensitivity().data()[i]);
            }
            printer->Print("\n");
            printer->DecreaseIndent();
          }
          if (report.sensor().has_threshold_high()) {
            printer->Print("Threshold High:\n");
            printer->IncreaseIndent();
            for (size_t i = 0; i < report.sensor().threshold_high().count(); i++) {
              printer->Print("%ld ", report.sensor().threshold_high().data()[i]);
            }
            printer->Print("\n");
            printer->DecreaseIndent();
          }
          if (report.sensor().has_threshold_low()) {
            printer->Print("Threshold Low:\n");
            printer->IncreaseIndent();
            for (size_t i = 0; i < report.sensor().threshold_low().count(); i++) {
              printer->Print("%ld ", report.sensor().threshold_low().data()[i]);
            }
            printer->Print("\n");
            printer->DecreaseIndent();
          }
          if (report.sensor().has_sampling_rate()) {
            printer->Print("Sampling Rate: %u\n", report.sensor().sampling_rate());
          }
          printer->DecreaseIndent();
        }

        if (report.has_touch()) {
          printer->Print("Touch Feature Report:\n");
          printer->IncreaseIndent();
          if (report.touch().has_input_mode()) {
            printer->Print("Input Mode: %u\n", report.touch().input_mode());
          }
          if (report.touch().has_selective_reporting()) {
            printer->Print("Selective Reporting:\n");
            printer->IncreaseIndent();
            if (report.touch().selective_reporting().has_surface_switch()) {
              printer->Print("Surface Switch: %u\n",
                             report.touch().selective_reporting().surface_switch());
            }
            if (report.touch().selective_reporting().has_button_switch()) {
              printer->Print("Button Switch: %u\n",
                             report.touch().selective_reporting().button_switch());
            }
            printer->DecreaseIndent();
          }
          printer->DecreaseIndent();
        }
        printer->DecreaseIndent();
        callback();
      });
  return ZX_OK;
}

zx_status_t PrintInputDescriptor(std::string filename, Printer* printer,
                                 fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
                                 fit::closure callback) {
  client->GetDescriptor().ThenExactlyOnce(
      [filename, printer, callback = std::move(callback), _ = client.Clone()](
          fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetDescriptor>& call_result) {
        if (!call_result.ok()) {
          return;
        }
        auto* result = call_result.Unwrap();
        printer->SetIndent(0);
        printer->Print("Descriptor from file: %s\n", filename.c_str());
        if (result->descriptor.has_mouse()) {
          if (result->descriptor.mouse().has_input()) {
            PrintMouseDesc(printer, result->descriptor.mouse().input());
          }
        }
        if (result->descriptor.has_sensor()) {
          if (result->descriptor.sensor().has_input()) {
            for (const auto& input : result->descriptor.sensor().input()) {
              PrintSensorDesc(printer, input);
            }
          }
        }
        if (result->descriptor.has_touch()) {
          PrintTouchDesc(printer, result->descriptor.touch());
        }
        if (result->descriptor.has_keyboard()) {
          PrintKeyboardDesc(printer, result->descriptor.keyboard());
        }
        if (result->descriptor.has_consumer_control()) {
          PrintConsumerControlDesc(printer, result->descriptor.consumer_control());
        }
        callback();
      });
  return ZX_OK;
}

void PrintMouseDesc(Printer* printer,
                    const fuchsia_input_report::wire::MouseInputDescriptor& mouse_desc) {
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
  if (mouse_desc.has_position_x()) {
    printer->Print("Position X:\n");
    printer->PrintAxisIndented(mouse_desc.position_x());
  }
  if (mouse_desc.has_position_y()) {
    printer->Print("Position Y:\n");
    printer->PrintAxisIndented(mouse_desc.position_y());
  }
  if (mouse_desc.has_buttons()) {
    for (uint8_t button : mouse_desc.buttons()) {
      printer->Print("Button: %d\n", button);
    }
  }
  printer->DecreaseIndent();
}

void PrintSensorDesc(Printer* printer,
                     const fuchsia_input_report::wire::SensorInputDescriptor& sensor_desc) {
  printer->Print("Sensor Descriptor:\n");
  if (!sensor_desc.has_values()) {
    return;
  }

  printer->IncreaseIndent();
  if (sensor_desc.has_report_id()) {
    printer->Print("ReportID: %02d\n", sensor_desc.report_id());
  }
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
                    const fuchsia_input_report::wire::TouchDescriptor& touch_desc) {
  printer->Print("Touch Descriptor:\n");
  printer->IncreaseIndent();
  if (touch_desc.has_input()) {
    printer->Print("Input Report:\n");
    printer->IncreaseIndent();
    const auto& input = touch_desc.input();
    if (input.has_touch_type()) {
      printer->Print("Touch Type: %s\n", printer->TouchTypeToString(input.touch_type()));
    }
    if (input.has_max_contacts()) {
      printer->Print("Max Contacts: %ld\n", input.max_contacts());
    }
    if (input.has_contacts()) {
      for (size_t i = 0; i < input.contacts().count(); i++) {
        const fuchsia_input_report::wire::ContactInputDescriptor& contact = input.contacts()[i];

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

  if (touch_desc.has_feature()) {
    printer->Print("Feature Report:\n");
    printer->IncreaseIndent();
    const auto& feature = touch_desc.feature();
    printer->Print("Supports InputMode: %u\n",
                   feature.has_supports_input_mode() ? feature.supports_input_mode() : false);
    printer->Print("Supports SelectiveReporting: %u\n", feature.has_supports_selective_reporting()
                                                            ? feature.supports_selective_reporting()
                                                            : false);
    printer->DecreaseIndent();
  }
  printer->DecreaseIndent();
}

void PrintKeyboardDesc(Printer* printer,
                       const fuchsia_input_report::wire::KeyboardDescriptor& descriptor) {
  printer->Print("Keyboard Descriptor:\n");

  if (descriptor.has_input()) {
    const fuchsia_input_report::wire::KeyboardInputDescriptor& input = descriptor.input();
    printer->Print("Input Report:\n");
    printer->IncreaseIndent();
    if (input.has_keys3()) {
      for (size_t i = 0; i < input.keys3().count(); i++) {
        printer->Print("Key: %8ld\n", input.keys3()[i]);
      }
    }
    printer->DecreaseIndent();
  }
  if (descriptor.has_output()) {
    const fuchsia_input_report::wire::KeyboardOutputDescriptor& output = descriptor.output();
    printer->Print("Output Report:\n");
    printer->IncreaseIndent();
    if (output.has_leds()) {
      for (size_t i = 0; i < output.leds().count(); i++) {
        printer->Print("Led: %s\n", Printer::LedTypeToString(output.leds()[i]));
      }
    }
    printer->DecreaseIndent();
  }
}

void PrintConsumerControlDesc(
    Printer* printer, const fuchsia_input_report::wire::ConsumerControlDescriptor& descriptor) {
  printer->Print("ConsumerControl Descriptor:\n");

  if (descriptor.has_input()) {
    const fuchsia_input_report::wire::ConsumerControlInputDescriptor& input = descriptor.input();
    printer->Print("Input Report:\n");
    printer->IncreaseIndent();
    if (input.has_buttons()) {
      for (size_t i = 0; i < input.buttons().count(); i++) {
        printer->Print("Button: %16s\n",
                       Printer::ConsumerControlButtonToString(input.buttons()[i]));
      }
    }
    printer->DecreaseIndent();
  }
}

void PrintInputReports(std::string filename, Printer* printer,
                       fidl::WireSharedClient<fuchsia_input_report::InputReportsReader> reader,
                       size_t num_reads, fit::closure callback) {
  if (num_reads == 0) {
    callback();
    return;
  }
  // Read the reports.
  // We need the ReadInputReport's callback to be mutable because the PrintInputReports callback is
  // moved into the next ReadInputReport's call.
  reader->ReadInputReports().ThenExactlyOnce(
      [=, reader = reader.Clone(), callback = std::move(callback)](
          fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
              call_result) mutable {
        if (!call_result.ok()) {
          return;
        }
        auto* result = call_result.Unwrap();
        size_t reads_left = num_reads;
        if (result->is_error()) {
          callback();
          return;
        }
        auto& reports = result->value()->reports;
        TRACE_DURATION("input", "print-input-report ReadReports");
        for (auto& report : reports) {
          if (reads_left == 0) {
            callback();
            return;
          }
          reads_left -= 1;
          printer->SetIndent(0);
          printer->Print("Report from file: %s\n", filename.c_str());
          if (report.has_event_time()) {
            printer->Print("EventTime: 0x%016lx\n", report.event_time());
          }
          if (report.has_trace_id()) {
            TRACE_FLOW_END("input", "input_report", report.trace_id());
          }
          if (report.has_report_id()) {
            printer->Print("ReportID: %02d\n", report.report_id());
          }
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
          if (report.has_consumer_control()) {
            PrintConsumerControlInputReport(printer, report.consumer_control());
          }
          printer->Print("\n");
        }
        PrintInputReports(filename, printer, std::move(reader), reads_left, std::move(callback));
      });
}

void GetAndPrintInputReport(std::string filename,
                            fuchsia_input_report::wire::DeviceType device_type, Printer* printer,
                            fidl::WireSharedClient<fuchsia_input_report::InputDevice> client,
                            fit::closure callback) {
  client->GetInputReport(device_type)
      .ThenExactlyOnce(
          [=, callback = std::move(callback), _ = client.Clone()](
              fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetInputReport>&
                  call_result) {
            if (!call_result.ok()) {
              return;
            }
            auto* result = call_result.Unwrap();
            if (result->is_error()) {
              callback();
              return;
            }
            auto& report = result->value()->report;
            TRACE_DURATION("input", "print-input-report GetReport");
            printer->SetIndent(0);
            printer->Print("Report from file: %s\n", filename.c_str());
            if (report.has_event_time()) {
              printer->Print("EventTime: 0x%016lx\n", report.event_time());
            }
            if (report.has_trace_id()) {
              TRACE_FLOW_END("input", "input_report", report.trace_id());
            }
            if (report.has_report_id()) {
              printer->Print("ReportID: %02d\n", report.report_id());
            }
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
            if (report.has_consumer_control()) {
              PrintConsumerControlInputReport(printer, report.consumer_control());
            }
            printer->Print("\n");
            callback();
          });
}

zx::result<fidl::WireSharedClient<fuchsia_input_report::InputReportsReader>> GetReaderClient(
    fidl::WireSharedClient<fuchsia_input_report::InputDevice>* client,
    async_dispatcher_t* dispatcher) {
  fidl::ClientEnd<fuchsia_input_report::InputReportsReader> reader_client;
  auto reader_server = fidl::CreateEndpoints(&reader_client);

  if (!reader_server.is_ok())
    return zx::error(reader_server.take_error());
  auto result = (*client)->GetInputReportsReader(std::move(*reader_server));

  if (result.status() != ZX_OK) {
    return zx::error(result.status());
  }

  return zx::ok(fidl::WireSharedClient(std::move(reader_client), dispatcher));
}

void PrintMouseInputReport(Printer* printer,
                           const fuchsia_input_report::wire::MouseInputReport& mouse_report) {
  if (mouse_report.has_movement_x()) {
    printer->Print("Movement x: %08ld\n", mouse_report.movement_x());
  }
  if (mouse_report.has_movement_y()) {
    printer->Print("Movement y: %08ld\n", mouse_report.movement_y());
  }
  if (mouse_report.has_position_x()) {
    printer->Print("Position x: %08ld\n", mouse_report.position_x());
  }
  if (mouse_report.has_position_y()) {
    printer->Print("Position y: %08ld\n", mouse_report.position_y());
  }
  if (mouse_report.has_scroll_v()) {
    printer->Print("Scroll v: %08ld\n", mouse_report.scroll_v());
  }
  if (mouse_report.has_pressed_buttons()) {
    for (uint8_t button : mouse_report.pressed_buttons()) {
      printer->Print("Button %02d pressed\n", button);
    }
  }
}

void PrintSensorInputReport(Printer* printer,
                            const fuchsia_input_report::wire::SensorInputReport& sensor_report) {
  if (!sensor_report.has_values()) {
    return;
  }

  for (size_t i = 0; i < sensor_report.values().count(); i++) {
    printer->Print("Sensor[%02d]: %08d\n", i, sensor_report.values()[i]);
  }
}

void PrintTouchInputReport(Printer* printer,
                           const fuchsia_input_report::wire::TouchInputReport& touch_report) {
  if (touch_report.has_contacts()) {
    for (size_t i = 0; i < touch_report.contacts().count(); i++) {
      const fuchsia_input_report::wire::ContactInputReport& contact = touch_report.contacts()[i];

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
      if (contact.has_confidence()) {
        printer->Print("Confidence: %d\n", contact.confidence());
      }

      printer->DecreaseIndent();
    }
  }
}

void PrintKeyboardInputReport(
    Printer* printer, const fuchsia_input_report::wire::KeyboardInputReport& keyboard_report) {
  printer->Print("Keyboard Report\n");
  printer->IncreaseIndent();
  if (keyboard_report.has_pressed_keys3()) {
    for (size_t i = 0; i < keyboard_report.pressed_keys3().count(); i++) {
      printer->Print("Key: %8ld\n", keyboard_report.pressed_keys3()[i]);
    }
    if (keyboard_report.pressed_keys3().count() == 0) {
      printer->Print("No keys pressed\n");
    }
  }
  printer->DecreaseIndent();
}

void PrintConsumerControlInputReport(
    Printer* printer, const fuchsia_input_report::wire::ConsumerControlInputReport& report) {
  printer->Print("ConsumerControl Report\n");
  printer->IncreaseIndent();
  if (report.has_pressed_buttons()) {
    for (size_t i = 0; i < report.pressed_buttons().count(); i++) {
      printer->Print("Button: %16s\n",
                     Printer::ConsumerControlButtonToString(report.pressed_buttons()[i]));
    }
  }
  printer->DecreaseIndent();
}

}  // namespace print_input_report
