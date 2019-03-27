// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_interpreter.h"
#include "garnet/bin/ui/input_reader/device.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/ui/input/cpp/formatting.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/time/time_point.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "garnet/bin/ui/input_reader/fdio_hid_decoder.h"
#include "garnet/bin/ui/input_reader/protocols.h"

namespace {

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

fuchsia::ui::input::InputReport CloneReport(
    const fuchsia::ui::input::InputReport& report) {
  fuchsia::ui::input::InputReport result;
  fidl::Clone(report, &result);
  return result;
}

}  // namespace

namespace mozart {

InputInterpreter::InputInterpreter(
    std::unique_ptr<HidDecoder> hid_decoder,
    fuchsia::ui::input::InputDeviceRegistry* registry)
    : registry_(registry), hid_decoder_(std::move(hid_decoder)) {
  FXL_DCHECK(hid_decoder_);
}

InputInterpreter::~InputInterpreter() {}

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
      fidl::Clone(device.descriptor.touchscreen_descriptor,
                  &descriptor.touchscreen);
    }
    if (device.descriptor.has_sensor) {
      fidl::Clone(device.descriptor.sensor_descriptor, &descriptor.sensor);
    }
    if (device.descriptor.has_media_buttons) {
      fidl::Clone(device.descriptor.buttons_descriptor,
                  &descriptor.media_buttons);
    }
    registry_->RegisterDevice(std::move(descriptor),
                              device.input_device.NewRequest());
  }
}

bool InputInterpreter::Read(bool discard) {
  TRACE_DURATION("input", "hid_read");

  // If positive |rc| is the number of bytes read. If negative the error
  // while reading.
  int rc = 1;
  auto report = hid_decoder_->Read(&rc);

  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc << " for " << name();
    // TODO(cpu) check whether the device was actually closed or not.
    return false;
  }

  hardcoded_.Read(report, rc, discard);

  for (size_t i = 0; i < devices_.size(); i++) {
    InputDevice& device = devices_[i];
    if (device.device->ReportId() != 0 &&
        device.device->ReportId() != report[0]) {
      continue;
    }
    if (device.device->ParseReport(report.data(), rc, device.report.get())) {
      if (!discard) {
        device.report->event_time = InputEventTimestampNow();
        device.report->trace_id = TRACE_NONCE();
        TRACE_FLOW_BEGIN("input", "hid_read_to_listener",
                         device.report->trace_id);
        device.input_device->DispatchReport(CloneReport(*device.report));
      }
    }
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
      {{static_cast<uint16_t>(Page::kConsumer),
        static_cast<uint32_t>(Consumer::kConsumerControl)},
       Protocol::MediaButtons},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchScreen)},
       Protocol::Touch},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kTouchPad)},
       Protocol::Touchpad},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kStylus)},
       Protocol::Stylus},
      {{static_cast<uint16_t>(Page::kDigitizer),
        static_cast<uint32_t>(Digitizer::kPen)},
       Protocol::Stylus},
      {{static_cast<uint16_t>(Page::kGenericDesktop),
        static_cast<uint32_t>(GenericDesktop::kMouse)},
       Protocol::Mouse},
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

bool InputInterpreter::ParseHidFeatureReportDescriptor(
    const hid::ReportDescriptor& report_desc) {
  // Traverse up the nested collections to the Application collection.
  auto collection = report_desc.input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    FXL_LOG(ERROR) << "Can't process HID feature report descriptor for "
                   << name()
                   << "; Needed a valid Collection but didn't get one";
    return false;
  }

  // If we have a touchscreen feature report then we enable multitouch mode.
  if (collection->usage ==
      hid::USAGE(hid::usage::Page::kDigitizer,
                 hid::usage::Digitizer::kTouchScreenConfiguration)) {
    std::vector<uint8_t> feature_report(report_desc.feature_byte_sz);
    if (report_desc.report_id != 0) {
      feature_report[0] = report_desc.report_id;
    }
    for (size_t i = 0; i < report_desc.feature_count; i++) {
      const hid::ReportField& field = report_desc.input_fields[i];
      if (field.attr.usage ==
          hid::USAGE(hid::usage::Page::kDigitizer,
                     hid::usage::Digitizer::kTouchScreenInputMode)) {
        InsertUint(feature_report.data(), feature_report.size(), field.attr,
                   static_cast<uint32_t>(
                       hid::usage::TouchScreenInputMode::kMultipleInput));
      }
    }
    hid_decoder_->Send(HidDecoder::ReportType::FEATURE, report_desc.report_id,
                       feature_report);
  }
  return true;
}

bool InputInterpreter::ParseHidInputReportDescriptor(
    const hid::ReportDescriptor* input_desc) {
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
    FXL_LOG(ERROR) << "Can't process HID report descriptor for " << name()
                   << "; Needed a valid Collection but didn't get one";
    return false;
  }

  InputDevice input_device = {};
  input_device.report = fuchsia::ui::input::InputReport::New();

  // Most modern gamepads report themselves as Joysticks. Madness.
  if (collection->usage.page == hid::usage::Page::kGenericDesktop &&
      collection->usage.usage == hid::usage::GenericDesktop::kJoystick &&
      hardcoded_.ParseGamepadDescriptor(input_desc->input_fields,
                                        input_desc->input_count)) {
    protocol_ = Protocol::Gamepad;
    return true;
  } else {
    protocol_ = ExtractProtocol(collection->usage);
    switch (protocol_) {
      case Protocol::LightSensor:
        hardcoded_.ParseAmbientLightDescriptor(input_desc->input_fields,
                                               input_desc->input_count);
        return true;
      case Protocol::MediaButtons: {
        FXL_VLOG(2) << "Device " << name() << " has HID media buttons";

        input_device.device = std::make_unique<Buttons>();
        input_device.report->media_buttons =
            fuchsia::ui::input::MediaButtonsReport::New();
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
        input_device.report->touchscreen =
            fuchsia::ui::input::TouchscreenReport::New();
        break;
      }
      case Protocol::Mouse: {
        FXL_VLOG(2) << "Device " << name() << " has HID mouse";

        input_device.device = std::make_unique<Mouse>();
        input_device.report->mouse = fuchsia::ui::input::MouseReport::New();
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

  if (!input_device.device->ParseReportDescriptor(*input_desc,
                                                  &input_device.descriptor)) {
    FXL_LOG(ERROR) << "Can't process HID report descriptor for " << name()
                   << "; Failed to do generic device parsing";
    return false;
  }
  devices_.push_back(std::move(input_device));

  FXL_LOG(INFO) << "hid-parser successful for " << name() << " with usage page "
                << collection->usage.page << " and usage "
                << collection->usage.usage;

  return true;
}

bool InputInterpreter::ParseProtocol() {
  HidDecoder::BootMode boot_mode = hid_decoder_->ReadBootMode();
  // For most keyboards and mouses Zircon requests the boot protocol
  // which has a fixed layout. This covers the following two cases:
  if (boot_mode == HidDecoder::BootMode::KEYBOARD) {
    protocol_ = Protocol::Keyboard;
    return true;
  }
  if (boot_mode == HidDecoder::BootMode::MOUSE) {
    protocol_ = Protocol::BootMouse;
    return true;
  }

  int desc_size;
  auto desc = hid_decoder_->ReadReportDescriptor(&desc_size);
  if (desc_size == 0) {
    return false;
  }

  // See if we match a hardcoded descriptor. This involves memcmp() of the
  // known hardcoded descriptors.
  Protocol protocol = hardcoded_.MatchProtocol(desc, hid_decoder_.get());
  if (protocol != Protocol::Other) {
    protocol_ = protocol;
    return true;
  }

  // For the rest of devices we use the new way; with the hid-parser
  // library.

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(desc.data(), desc.size(), &dev_desc);
  if (parse_res != hid::ParseResult::kParseOk) {
    FXL_LOG(ERROR) << "hid-parser: error " << int(parse_res)
                   << " parsing report descriptor for " << name();
    return false;
  }

  auto count = dev_desc->rep_count;
  if (count == 0) {
    FXL_LOG(ERROR) << "no report descriptors for " << name();
    return false;
  }

  // Parse each input report.
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor* desc = &dev_desc->report[rep];
    if (desc->input_count != 0) {
      if (!ParseHidInputReportDescriptor(desc)) {
        return false;
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
    FXL_LOG(ERROR) << "Can't process HID report descriptor for " << name()
                   << "; All parsing attempts failed.";
    return false;
  }

  return true;
}

}  // namespace mozart
