// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/input_interpreter.h"

#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <hid-parser/descriptor.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>
#include <trace/event.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"
#include "src/ui/lib/input_reader/device.h"
#include "src/ui/lib/input_reader/fdio_hid_decoder.h"
#include "src/ui/lib/input_reader/protocols.h"

namespace {

uint64_t CalculateTraceId(uint32_t trace_id, uint32_t report_id) {
  return (static_cast<uint64_t>(report_id) << 32) | (trace_id);
}

int64_t InputEventTimestampNow() { return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds(); }

fuchsia::ui::input::InputReport CloneReport(const fuchsia::ui::input::InputReport& report) {
  fuchsia::ui::input::InputReport result;
  fidl::Clone(report, &result);
  return result;
}

}  // namespace

namespace ui_input {

InputInterpreter::InputInterpreter(std::unique_ptr<HidDecoder> hid_decoder,
                                   fuchsia::ui::input::InputDeviceRegistry* registry)
    : registry_(registry), hid_decoder_(std::move(hid_decoder)) {
  FXL_DCHECK(hid_decoder_);
}

InputInterpreter::~InputInterpreter() {
  if (hid_descriptor_) {
    hid::FreeDeviceDescriptor(hid_descriptor_);
  }
}

void InputInterpreter::DispatchReport(InputDevice* device) {
  device->report->event_time = InputEventTimestampNow();
  device->report->trace_id = TRACE_NONCE();
  TRACE_FLOW_BEGIN("input", "hid_read_to_listener", device->report->trace_id);
  device->input_device->DispatchReport(CloneReport(*device->report));
}

bool InputInterpreter::Initialize() {
  if (!hid_decoder_->Init())
    return false;

  if (!ParseProtocol())
    return false;

  hardcoded_.Initialize(protocol_);

  event_ = hid_decoder_->GetEvent();
  if (!event_)
    return false;

  NotifyRegistry();

  for (size_t i = 0; i < devices_.size(); i++) {
    InputDevice& device = devices_[i];
    // If we are a media button then query for an initial report.
    if (device.descriptor.protocol == Protocol::MediaButtons) {
      std::vector<uint8_t> initial_input;
      zx_status_t status = hid_decoder_->GetReport(HidDecoder::ReportType::INPUT,
                                                   device.device->ReportId(), &initial_input);
      if (status != ZX_OK) {
        return false;
      }
      if (device.device->ParseReport(initial_input.data(), initial_input.size(),
                                     device.report.get())) {
        DispatchReport(&device);
      }
    }
  }

  return true;
}

void InputInterpreter::NotifyRegistry() {
  hardcoded_.NotifyRegistry(registry_);

  // Register the generic device's descriptors.
  for (size_t i = 0; i < devices_.size(); i++) {
    fuchsia::ui::input::DeviceDescriptor descriptor;
    InputDevice& device = devices_[i];
    if (device.descriptor.has_keyboard) {
      fidl::Clone(device.descriptor.keyboard_descriptor, &descriptor.keyboard);
    }
    if (device.descriptor.has_mouse) {
      fidl::Clone(device.descriptor.mouse_descriptor, &descriptor.mouse);
    }
    if (device.descriptor.has_stylus) {
      fidl::Clone(device.descriptor.stylus_descriptor, &descriptor.stylus);
    }
    if (device.descriptor.has_touchscreen) {
      fidl::Clone(device.descriptor.touchscreen_descriptor, &descriptor.touchscreen);
    }
    if (device.descriptor.has_sensor) {
      fidl::Clone(device.descriptor.sensor_descriptor, &descriptor.sensor);
    }
    if (device.descriptor.has_media_buttons) {
      fidl::Clone(device.descriptor.buttons_descriptor, &descriptor.media_buttons);
    }
    registry_->RegisterDevice(std::move(descriptor), device.input_device.NewRequest());
  }
}

bool InputInterpreter::Read(bool discard) {
  TRACE_DURATION("input", "hid_read");
  std::array<uint8_t, fuchsia_hardware_input_MAX_REPORT_DATA> report_data;

  size_t bytes_read = hid_decoder_->Read(report_data.data(), report_data.size());

  if (bytes_read == 0) {
    FXL_LOG(ERROR) << "Failed to read from input: " << bytes_read << " for " << name();
    // TODO(cpu) check whether the device was actually closed or not.
    return false;
  }

  size_t data_index = 0;
  while (data_index < bytes_read) {
    TRACE_FLOW_END("input", "hid_report", CalculateTraceId(trace_id_, reports_read_));
    ++reports_read_;

    uint8_t* report = &report_data[data_index];
    size_t report_size =
        hid::GetReportSizeFromFirstByte(*hid_descriptor_, hid::kReportInput, report[0]);
    if (report_size == 0) {
      FXL_LOG(ERROR) << "input_reader: Unable to get Report Size from Id " << report[0] << " : "
                     << name();
      return false;
    }

    hardcoded_.Read(report, report_size, discard);

    for (auto& device : devices_) {
      if (!device.device->MatchesReportId(report[0])) {
        continue;
      }
      if (device.device->ParseReport(report, report_size, device.report.get()) && !discard) {
        DispatchReport(&device);
      }
    }
    data_index += report_size;
  }

  return true;
}

Protocol InputInterpreter::ExtractProtocol(hid::Usage input) {
  using ::hid::usage::Consumer;
  using ::hid::usage::Digitizer;
  using ::hid::usage::GenericDesktop;
  using ::hid::usage::Page;
  using ::hid::usage::Sensor;
  struct {
    hid::Usage usage;
    Protocol protocol;
  } usage_to_protocol[] = {
      {{static_cast<uint16_t>(Page::kConsumer), static_cast<uint32_t>(Consumer::kConsumerControl)},
       Protocol::MediaButtons},
      {{static_cast<uint16_t>(Page::kDigitizer), static_cast<uint32_t>(Digitizer::kTouchScreen)},
       Protocol::Touch},
      {{static_cast<uint16_t>(Page::kDigitizer), static_cast<uint32_t>(Digitizer::kTouchPad)},
       Protocol::Touchpad},
      {{static_cast<uint16_t>(Page::kDigitizer), static_cast<uint32_t>(Digitizer::kStylus)},
       Protocol::Stylus},
      {{static_cast<uint16_t>(Page::kDigitizer), static_cast<uint32_t>(Digitizer::kPen)},
       Protocol::Stylus},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kMouse)},
       Protocol::Mouse},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kPointer)},
       Protocol::Pointer},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kKeyboard)},
       Protocol::Keyboard},
      // Add more sensors here
  };

  if (input.page == Page::kSensor) {
    return Protocol::Sensor;
  }

  for (auto& j : usage_to_protocol) {
    if (input.page == j.usage.page && input.usage == j.usage.usage) {
      return j.protocol;
    }
  }
  return Protocol::Other;
}

bool InputInterpreter::ParseHidFeatureReportDescriptor(const hid::ReportDescriptor& report_desc) {
  // Traverse up the nested collections to the Application collection.
  auto collection = report_desc.input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    FXL_LOG(INFO) << "Can't process HID feature report descriptor for " << name()
                  << "; Needed a valid Collection but didn't get one";
    return false;
  }

  // If we have a touchscreen feature report then we enable multitouch mode.
  if (collection->usage ==
      hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchScreenConfiguration)) {
    std::vector<uint8_t> feature_report(report_desc.feature_byte_sz);
    if (report_desc.report_id != 0) {
      feature_report[0] = report_desc.report_id;
    }
    for (size_t i = 0; i < report_desc.feature_count; i++) {
      const hid::ReportField& field = report_desc.input_fields[i];
      if (field.attr.usage ==
          hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTouchScreenInputMode)) {
        InsertUint(feature_report.data(), feature_report.size(), field.attr,
                   static_cast<uint32_t>(hid::usage::TouchScreenInputMode::kMultipleInput));
      }
    }
    hid_decoder_->Send(HidDecoder::ReportType::FEATURE, report_desc.report_id, feature_report);
  }
  return true;
}

bool InputInterpreter::ParseHidInputReportDescriptor(const hid::ReportDescriptor* input_desc) {
  FXL_CHECK(input_desc);

  // Traverse up the nested collections to the Application collection.
  hid::Collection* collection = input_desc->input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    FXL_LOG(INFO) << "Can't process HID report descriptor for " << name()
                  << "; Needed a valid Collection but didn't get one";
    return false;
  }

  InputDevice input_device = {};
  input_device.report = fuchsia::ui::input::InputReport::New();

  // Most modern gamepads report themselves as Joysticks. Madness.
  if (collection->usage.page == hid::usage::Page::kGenericDesktop &&
      collection->usage.usage == hid::usage::GenericDesktop::kJoystick &&
      hardcoded_.ParseGamepadDescriptor(input_desc->input_fields, input_desc->input_count)) {
    protocol_ = Protocol::Gamepad;
    return true;
  } else {
    protocol_ = ExtractProtocol(collection->usage);
    switch (protocol_) {
      case Protocol::LightSensor:
        hardcoded_.ParseAmbientLightDescriptor(input_desc->input_fields, input_desc->input_count);
        return true;
      case Protocol::MediaButtons: {
        FXL_VLOG(2) << "Device " << name() << " has HID media buttons";

        input_device.device = std::make_unique<Buttons>();
        input_device.report->media_buttons = fuchsia::ui::input::MediaButtonsReport::New();
        break;
      }
      case Protocol::Pointer: {
        FXL_VLOG(2) << "Device " << name() << " has HID pointer";

        input_device.device = std::make_unique<Pointer>();
        input_device.report->touchscreen = fuchsia::ui::input::TouchscreenReport::New();
        break;
      }
      case Protocol::Sensor: {
        FXL_VLOG(2) << "Device " << name() << " has HID sensor";

        input_device.device = std::make_unique<Sensor>();
        input_device.report->sensor = fuchsia::ui::input::SensorReport::New();
        break;
      }
      case Protocol::Touchpad: {
        FXL_VLOG(2) << "Device " << name() << " has HID touchpad";

        input_device.device = std::make_unique<Touchpad>();
        input_device.report->mouse = fuchsia::ui::input::MouseReport::New();
        break;
      }
      case Protocol::Touch: {
        FXL_VLOG(2) << "Device " << name() << " has HID touch";

        input_device.device = std::make_unique<TouchScreen>();
        input_device.report->touchscreen = fuchsia::ui::input::TouchscreenReport::New();
        break;
      }
      case Protocol::Mouse: {
        FXL_VLOG(2) << "Device " << name() << " has HID mouse";

        input_device.device = std::make_unique<Mouse>();
        input_device.report->mouse = fuchsia::ui::input::MouseReport::New();
        break;
      }
      case Protocol::Keyboard: {
        FXL_VLOG(2) << "Device " << name() << " has HID keyboard";

        input_device.device = std::make_unique<Keyboard>();
        input_device.report->keyboard = fuchsia::ui::input::KeyboardReport::New();
        break;
      }
      case Protocol::Stylus: {
        FXL_VLOG(2) << "Device " << name() << " has HID stylus";

        input_device.device = std::make_unique<Stylus>();
        input_device.report->stylus = fuchsia::ui::input::StylusReport::New();
        break;
      }
      // Add more protocols here
      default:
        // Not being able to match on a given HID report descriptor is not
        // an error and will happen frequently. We only need to match a single
        // report in the report descriptor to be valid.
        return true;
    }
  }

  if (!input_device.device->ParseReportDescriptor(*input_desc, &input_device.descriptor)) {
    FXL_LOG(INFO) << "Can't process HID report descriptor for " << name()
                  << "; Failed to do generic device parsing";
    return false;
  }
  devices_.push_back(std::move(input_device));

  FXL_LOG(INFO) << "hid-parser successful for " << name() << " with usage page "
                << collection->usage.page << " and usage " << collection->usage.usage;

  return true;
}

bool InputInterpreter::ParseProtocol() {
  trace_id_ = hid_decoder_->GetTraceId();

  // Read and parse the hid desccriptor.
  int desc_size;
  const std::vector<uint8_t>& desc = hid_decoder_->ReadReportDescriptor(&desc_size);
  if (desc_size <= 0) {
    return false;
  }
  hid::ParseResult parse_res =
      hid::ParseReportDescriptor(desc.data(), desc.size(), &hid_descriptor_);
  if (parse_res != hid::ParseResult::kParseOk) {
    FXL_LOG(INFO) << "hid-parser: error " << int(parse_res) << " parsing report descriptor for "
                  << name();
    return false;
  }

  // Check the report descriptor against a hardcoded one. This involves memcmp() of the
  // known hardcoded descriptors.
  Protocol protocol = hardcoded_.MatchProtocol(desc, hid_decoder_.get());
  if (protocol != Protocol::Other) {
    protocol_ = protocol;
    return true;
  }

  // For the rest of devices we use the new way; with the hid-parser
  // library.
  auto count = hid_descriptor_->rep_count;
  if (count == 0) {
    FXL_LOG(ERROR) << "no report descriptors for " << name();
    return false;
  }

  // Parse each input report.
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor* desc = &hid_descriptor_->report[rep];
    if (desc->input_count != 0) {
      if (!ParseHidInputReportDescriptor(desc)) {
        continue;
      }
    }
    if (desc->feature_count != 0) {
      if (!ParseHidFeatureReportDescriptor(*desc)) {
        return false;
      }
    }
  }

  // If we never parsed a single device correctly then fail.
  if (devices_.size() == 0) {
    FXL_LOG(INFO) << "Can't process HID report descriptor for " << name()
                  << "; All parsing attempts failed.";
    return false;
  }

  return true;
}

}  // namespace ui_input
