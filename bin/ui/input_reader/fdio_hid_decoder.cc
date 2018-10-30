// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/hid_decoder.h"

#include <fcntl.h>
#include <unistd.h>

#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <lib/fxl/logging.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/device.h>
#include <zircon/input/c/fidl.h>

namespace {
bool log_err(ssize_t rc, const std::string& what, const std::string& name) {
  FXL_LOG(ERROR) << "hid: could not get " << what << " from " << name
                 << " (status=" << rc << ")";
  return false;
}

// TODO(SCN-843): We need to generalize these extraction functions

// Casting from unsigned to signed can change the bit pattern so
// we need to resort to this method.
int8_t signed_bit_cast(uint8_t src) {
  int8_t dest;
  memcpy(&dest, &src, sizeof(uint8_t));
  return dest;
}

// Extracts a up to 8 bits unsigned number from a byte vector |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the vector being long enough.
static uint8_t extract_uint8(const std::vector<uint8_t>& v, uint32_t begin,
                             uint32_t count) {
  uint8_t val = v[begin / 8u] >> (begin % 8u);
  return (count < 8) ? (val & ~(1u << count)) : val;
}

// Extracts a 16 bits unsigned number from a byte vector |v|.
// |begin| is in bits units. This function does not check for the vector
// being long enough.
static uint16_t extract_uint16(const std::vector<uint8_t>& v, uint32_t begin) {
  return static_cast<uint16_t>(extract_uint8(v, begin, 8)) |
         static_cast<uint16_t>(extract_uint8(v, begin + 8, 8)) << 8;
}

// Extracts up to 8 bits sign extended to int32_t from a byte vector |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the vector being long enough.
static int32_t extract_int8_ext(const std::vector<uint8_t>& v, uint32_t begin,
                                uint32_t count) {
  uint8_t val = extract_uint8(v, begin, count);
  return signed_bit_cast(val);
}

}  // namespace

namespace mozart {

HidDecoder::HidDecoder(const std::string& name, int fd)
    : fd_(fd), name_(name), protocol_(Protocol::Other) {}

bool HidDecoder::Init() {
  fzl::FdioCaller caller(fbl::move(fbl::unique_fd(fd_)));

  bool success = ParseProtocol(caller, &protocol_);
  if (!success) {
    // Do not close the fd we were given
    caller.release().release();
    return false;
  }

  uint16_t max_len = 0;
  zx_status_t status = zircon_input_DeviceGetMaxInputReportSize(
      caller.borrow_channel(), &max_len);
  caller.release().release();
  if (status != ZX_OK) {
    return false;
  }

  report_.resize(max_len);
  return true;
}

bool HidDecoder::GetEvent(zx_handle_t* handle) {
  ssize_t rc = ioctl_device_get_event_handle(fd_, handle);
  return (rc < 0) ? log_err(rc, "event handle", name_) : true;
}

HidDecoder::Protocol ExtractProtocol(hid::Usage input) {
  using ::hid::usage::Consumer;
  using ::hid::usage::Page;
  using ::hid::usage::Sensor;
  struct {
    hid::Usage usage;
    HidDecoder::Protocol protocol;
  } usage_to_protocol[] = {
      {{static_cast<uint16_t>(Page::kSensor),
        static_cast<uint32_t>(Sensor::kAmbientLight)},
       HidDecoder::Protocol::LightSensor},
      {{static_cast<uint16_t>(Page::kConsumer),
        static_cast<uint32_t>(Consumer::kConsumerControl)},
       HidDecoder::Protocol::Buttons},
      // Add more sensors here
  };
  for (auto& j : usage_to_protocol) {
    if (input.page == j.usage.page && input.usage == j.usage.usage) {
      return j.protocol;
    }
  }
  return HidDecoder::Protocol::Other;
}

bool HidDecoder::ParseProtocol(const fzl::FdioCaller& caller,
                               Protocol* protocol) {
  zx_handle_t svc = caller.borrow_channel();

  zircon_input_BootProtocol boot_protocol;
  zx_status_t status = zircon_input_DeviceGetBootProtocol(svc, &boot_protocol);
  if (status != ZX_OK)
    return log_err(status, "ioctl protocol", name_);

  // For most keyboards and mouses Zircon requests the boot protocol
  // which has a fixed layout. This covers the following two cases:

  if (boot_protocol == zircon_input_BootProtocol_KBD) {
    *protocol = Protocol::Keyboard;
    return true;
  }
  if (boot_protocol == zircon_input_BootProtocol_MOUSE) {
    *protocol = Protocol::Mouse;
    return true;
  }

  // For the rest of devices (zircon_input_BootProtocol_NONE) we need to parse
  // the report descriptor. The legacy method involves memcmp() of known
  // descriptors which cover the next 8 devices:

  uint16_t report_desc_len;
  status = zircon_input_DeviceGetReportDescSize(svc, &report_desc_len);
  if (status != ZX_OK)
    return log_err(status, "report descriptor length", name_);

  std::vector<uint8_t> desc(report_desc_len);
  size_t actual;
  status =
      zircon_input_DeviceGetReportDesc(svc, desc.data(), desc.size(), &actual);
  if (status != ZX_OK)
    return log_err(status, "report descriptor", name_);
  desc.resize(actual);

  if (is_acer12_touch_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::Acer12Touch;
    return true;
  }
  if (is_samsung_touch_report_desc(desc.data(), desc.size())) {
    setup_samsung_touch(fd_);
    *protocol = Protocol::SamsungTouch;
    return true;
  }
  if (is_paradise_touch_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseV1Touch;
    return true;
  }
  if (is_paradise_touch_v2_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseV2Touch;
    return true;
  }
  if (is_paradise_touch_v3_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseV3Touch;
    return true;
  }
  if (is_paradise_touchpad_v1_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseV1TouchPad;
    return true;
  }
  if (is_paradise_touchpad_v2_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseV2TouchPad;
    return true;
  }
  if (is_egalax_touchscreen_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::EgalaxTouch;
    return true;
  }
  if (is_paradise_sensor_report_desc(desc.data(), desc.size())) {
    *protocol = Protocol::ParadiseSensor;
    return true;
  }
  if (is_eyoyo_touch_report_desc(desc.data(), desc.size())) {
    setup_eyoyo_touch(fd_);
    *protocol = Protocol::EyoyoTouch;
    return true;
  }
  // TODO(SCN-867) Use HID parsing for all touch devices
  // will remove the need for this
  if (is_ft3x27_touch_report_desc(desc.data(), desc.size())) {
    setup_ft3x27_touch(fd_);
    *protocol = Protocol::Ft3x27Touch;
    return true;
  }

  // For the rest of devices we use the new way; with the hid-parser
  // library.

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(desc.data(), desc.size(), &dev_desc);
  if (parse_res != hid::ParseResult::kParseOk) {
    FXL_LOG(ERROR) << "hid-parser: error " << int(parse_res)
                   << " parsing report descriptor for " << name_;
    return false;
  }

  auto count = dev_desc->rep_count;
  if (count == 0) {
    FXL_LOG(ERROR) << "no report descriptors for " << name_;
    return false;
  }

  // Find the first input report.
  const hid::ReportField* input_fields = nullptr;
  size_t field_count = 0;
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportField* fields = dev_desc->report[rep].first_field;
    if (fields[0].type == hid::kInput) {
      input_fields = fields;
      field_count = dev_desc->report[rep].count;
      break;
    }
  }

  if (input_fields == nullptr) {
    FXL_LOG(ERROR) << "no input report fields for " << name_;
    return false;
  }

  // Traverse up the nested collections to the Application collection.
  auto collection = input_fields[0].col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication) {
      break;
    }
    collection = collection->parent;
  }

  if (collection == nullptr) {
    FXL_LOG(ERROR) << "invalid hid collection for " << name_;
    return false;
  }

  FXL_LOG(INFO) << "hid-parser succesful for " << name_ << " with usage page "
                << collection->usage.page << " and usage "
                << collection->usage.usage;

  // Most modern gamepads report themselves as Joysticks. Madness.
  if (collection->usage.page == hid::usage::Page::kGenericDesktop &&
      collection->usage.usage == hid::usage::GenericDesktop::kJoystick &&
      ParseGamepadDescriptor(input_fields, field_count)) {
    protocol_ = Protocol::Gamepad;
  } else {
    protocol_ = ExtractProtocol(collection->usage);
    switch (protocol_) {
      case Protocol::LightSensor:
        ParseAmbientLightDescriptor(input_fields, field_count);
        break;
      case Protocol::Buttons:
        ParseButtonsDescriptor(input_fields, field_count);
        break;
      // Add more protocols here
      default:
        break;
    }
  }

  *protocol = protocol_;
  return true;
}

bool HidDecoder::use_legacy_mode() const {
  return protocol_ != Protocol::Gamepad && protocol_ != Protocol::Buttons &&
         protocol_ != Protocol::LightSensor;
}

bool HidDecoder::ParseGamepadDescriptor(const hid::ReportField* fields,
                                        size_t count) {
  // Need to recover the five fields as seen in HidGamepadSimple and put
  // them into the decoder_ in the same order.
  if (count < 5u)
    return false;

  decoder_.resize(6u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  // Needs to be kept in sync with HidGamepadSimple {}.
  const uint16_t table[] = {
      static_cast<uint16_t>(hid::usage::GenericDesktop::kX),         // left X.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kY),         // left Y.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kZ),         // right X.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kRz),        // right Y.
      static_cast<uint16_t>(hid::usage::GenericDesktop::kHatSwitch)  // buttons
  };

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    for (size_t iy = 0; iy != countof(table); iy++) {
      if (fields[ix].attr.usage.usage == table[iy]) {
        // Found a required usage.
        decoder_[iy + 1] =
            DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
        break;
      }
    }

    bit_count += fields[ix].attr.bit_sz;
  }

  // Here |decoder_| should look like this:
  // [rept_id][left X][left Y]....[hat_sw]
  // With each box, the location in a report for each item, for example:
  // [0, 0, 0][24, 0, 0][8, 0, 0][0, 0, 0]...[64, 4, 0]
  return true;
}

bool HidDecoder::ParseAmbientLightDescriptor(const hid::ReportField* fields,
                                             size_t count) {
  if (count == 0u)
    return false;

  decoder_.resize(2u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    if (fields[ix].attr.usage.usage == hid::usage::Sensor::kLightIlluminance) {
      decoder_[1] = DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
      // Found a required usage.
      // Here |decoder_| should look like this:
      // [rept_id][abs_light]
      return true;
    }

    bit_count += fields[ix].attr.bit_sz;
  }
  return false;
}

bool HidDecoder::ParseButtonsDescriptor(const hid::ReportField* fields,
                                        size_t count) {
  if (count == 0u)
    return false;

  decoder_.resize(3u);
  uint8_t offset = 0;

  if (fields[0].report_id != 0) {
    // If exists, the first entry (8-bits) is always the report id and
    // all items start after the first byte.
    decoder_[0] = DataLocator{0u, 8u, fields[0].report_id};
    offset = 8u;
  }

  // Needs to be kept in sync with HidButtons {}.
  const uint16_t table[] = {
      static_cast<uint16_t>(hid::usage::Consumer::kVolume),
      static_cast<uint16_t>(hid::usage::Telephony::kPhoneMute),
  };

  uint32_t bit_count = 0;

  // Traverse each input report field and see if there is a match in the table.
  // If so place the location in |decoder_| array.
  for (size_t ix = 0; ix != count; ix++) {
    if (fields[ix].type != hid::kInput)
      continue;

    for (size_t iy = 0; iy != countof(table); iy++) {
      if (fields[ix].attr.usage.usage == table[iy]) {
        // Found a required usage.
        decoder_[iy + 1] =
            DataLocator{bit_count + offset, fields[ix].attr.bit_sz, 0};
        break;
      }
    }

    bit_count += fields[ix].attr.bit_sz;
  }

  // Here |decoder_| should look like this:
  // [rept_id][volume][mic_mute]
  return true;
}

const std::vector<uint8_t>& HidDecoder::Read(int* bytes_read) {
  *bytes_read = read(fd_, report_.data(), report_.size());
  return report_;
}

bool HidDecoder::Read(HidGamepadSimple* gamepad) {
  if (protocol_ != Protocol::Gamepad)
    return false;

  int rc;
  auto report = Read(&rc);
  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc;
    return false;
  }

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *gamepad = {};
      return true;
    }
    ++cur;
  }

  gamepad->left_x = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->left_y = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->right_x = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->right_y = extract_int8_ext(report, cur->begin, cur->count) / 2;
  ++cur;
  gamepad->hat_switch = extract_int8_ext(report, cur->begin, cur->count);
  return true;
}

bool HidDecoder::Read(HidAmbientLightSimple* data) {
  if (protocol_ != Protocol::LightSensor)
    return false;

  int rc;
  auto report = Read(&rc);
  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc;
    return false;
  }

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *data = {};
      return true;
    }
    ++cur;
  }
  if (cur->count != 16u) {
    FXL_LOG(ERROR) << "Unexpected count in report from ambient light:"
                   << cur->count;
    return false;
  }
  data->illuminance = extract_uint16(report, cur->begin);
  return true;
}

bool HidDecoder::Read(HidButtons* data) {
  if (protocol_ != Protocol::Buttons)
    return false;

  int rc;
  auto report = Read(&rc);
  if (rc < 1) {
    FXL_LOG(ERROR) << "Failed to read from input: " << rc;
    return false;
  }

  auto cur = &decoder_[0];
  if ((cur->match != 0) && (cur->count == 8u)) {
    // The first byte is the report id.
    if (report[0] != cur->match) {
      // This is a normal condition. The device can generate reports
      // for controls we don't yet handle.
      *data = {};
      return true;
    }
    ++cur;
  }

  // 2 bits, see zircon/system/ulib/hid's buttons.c and include/hid/buttons.h
  if (cur->count != 2u) {
    FXL_LOG(ERROR) << "Unexpected count in report from buttons:" << cur->count;
    return false;
  }
  // TODO(SCN-843): We need to generalize these extraction functions, e.g. add
  // extract_int8
  data->volume = extract_uint8(report, cur->begin, 2u);
  if (data->volume == 3) {  // 2 bits unsigned 3 is signed -1
    data->volume = -1;
  }
  ++cur;

  // 1 bit, see zircon/system/ulib/hid's buttons.c and include/hid/buttons.h
  if (cur->count != 1u) {
    FXL_LOG(ERROR) << "Unexpected count in report from buttons:" << cur->count;
    return false;
  }
  data->mic_mute = extract_uint8(report, cur->begin, 1u);
  return true;
}

}  // namespace mozart
