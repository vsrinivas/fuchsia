// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/hardcoded.h"

#include <fuchsia/hardware/input/c/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/ambient-light.h>
#include <hid/boot.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/hid.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <hid/usages.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/time/time_point.h>
#include <lib/ui/input/cpp/formatting.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace {

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

// Casting from unsigned to signed can change the bit pattern so
// we need to resort to this method.
int8_t signed_bit_cast(uint8_t src) {
  int8_t dest;
  memcpy(&dest, &src, sizeof(uint8_t));
  return dest;
}

// Extracts up to 8 bits unsigned number from a byte array |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the array being long enough.
static uint8_t extract_uint8(const uint8_t* v, uint32_t begin, uint32_t count) {
  uint8_t val = v[begin / 8u] >> (begin % 8u);
  return (count < 8) ? (val & ~(1u << count)) : val;
}

// Extracts a 16 bits unsigned number from a byte array |v|.
// |begin| is in bits units. This function does not check for the array
// being long enough.
static uint16_t extract_uint16(const uint8_t* v, uint32_t begin) {
  return static_cast<uint16_t>(extract_uint8(v, begin, 8)) |
         static_cast<uint16_t>(extract_uint8(v, begin + 8, 8)) << 8;
}

// Extracts up to 8 bits sign extended to int32_t from a byte array |v|.
// Both |begin| and |count| are in bits units. This function does not
// check for the array being long enough.
static int32_t extract_int8_ext(const uint8_t* v, uint32_t begin,
                                uint32_t count) {
  uint8_t val = extract_uint8(v, begin, count);
  return signed_bit_cast(val);
}

// TODO(SCN-473): Extract sensor IDs from HID.
const size_t kParadiseAccLid = 0;
const size_t kParadiseAccBase = 1;
const size_t kAmbientLight = 2;

}  // namespace

namespace mozart {

bool Hardcoded::ParseGamepadDescriptor(const hid::ReportField* fields,
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

    for (size_t iy = 0; iy != arraysize(table); iy++) {
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

bool Hardcoded::ParseButtonsDescriptor(const hid::ReportField* fields,
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

    for (size_t iy = 0; iy != arraysize(table); iy++) {
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

bool Hardcoded::ParseAmbientLightDescriptor(const hid::ReportField* fields,
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

void Hardcoded::ParseKeyboardReport(
    uint8_t* report, size_t len,
    fuchsia::ui::input::InputReport* keyboard_report) {
  hid_keys_t key_state;
  uint8_t keycode;
  hid_kbd_parse_report(report, &key_state);
  keyboard_report->event_time = InputEventTimestampNow();
  keyboard_report->trace_id = TRACE_NONCE();

  auto& pressed_keys = keyboard_report->keyboard->pressed_keys;
  pressed_keys.resize(0);
  hid_for_every_key(&key_state, keycode) { pressed_keys.push_back(keycode); }
  FXL_VLOG(2) << name() << " parsed: " << *keyboard_report;
}

void Hardcoded::ParseMouseReport(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* mouse_report) {
  auto report = reinterpret_cast<hid_boot_mouse_report_t*>(r);
  mouse_report->event_time = InputEventTimestampNow();
  mouse_report->trace_id = TRACE_NONCE();

  mouse_report->mouse->rel_x = report->rel_x;
  mouse_report->mouse->rel_y = report->rel_y;
  mouse_report->mouse->pressed_buttons = report->buttons;
  FXL_VLOG(2) << name() << " parsed: " << *mouse_report;
}

bool Hardcoded::ParseReport(const uint8_t* report, size_t len,
                            HidGamepadSimple* gamepad) {
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

bool Hardcoded::ParseGamepadMouseReport(
    uint8_t* report, size_t len,
    fuchsia::ui::input::InputReport* mouse_report) {
  HidGamepadSimple gamepad = {};
  if (!ParseReport(report, len, &gamepad))
    return false;
  mouse_report->event_time = InputEventTimestampNow();
  mouse_report->trace_id = TRACE_NONCE();

  mouse_report->mouse->rel_x = gamepad.left_x;
  mouse_report->mouse->rel_y = gamepad.left_y;
  mouse_report->mouse->pressed_buttons = gamepad.hat_switch;
  return true;
}

bool Hardcoded::ParseAcer12TouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(acer12_touch_t)) {
    return false;
  }

  // Acer12 touch reports come in pairs when there are more than 5 fingers
  // First report has the actual number of fingers stored in contact_count,
  // second report will have a contact_count of 0.
  auto report = reinterpret_cast<acer12_touch_t*>(r);
  if (report->contact_count > 0) {
    acer12_touch_reports_[0] = *report;
  } else {
    acer12_touch_reports_[1] = *report;
  }
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();

  size_t index = 0;
  touchscreen_report->touchscreen->touches.resize(index);

  for (uint8_t i = 0; i < 2; i++) {
    // Only 5 touches per report
    for (uint8_t c = 0; c < 5; c++) {
      auto fid = acer12_touch_reports_[i].fingers[c].finger_id;

      if (!acer12_finger_id_tswitch(fid))
        continue;
      fuchsia::ui::input::Touch touch;
      touch.finger_id = acer12_finger_id_contact(fid);
      touch.x = acer12_touch_reports_[i].fingers[c].x;
      touch.y = acer12_touch_reports_[i].fingers[c].y;
      touch.width = acer12_touch_reports_[i].fingers[c].width;
      touch.height = acer12_touch_reports_[i].fingers[c].height;
      touchscreen_report->touchscreen->touches.resize(index + 1);
      touchscreen_report->touchscreen->touches.at(index++) = std::move(touch);
    }
  }
  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report;
  return true;
}

bool Hardcoded::ParseAcer12StylusReport(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* stylus_report) {
  if (len != sizeof(acer12_stylus_t)) {
    return false;
  }

  auto report = reinterpret_cast<acer12_stylus_t*>(r);
  stylus_report->event_time = InputEventTimestampNow();
  stylus_report->trace_id = TRACE_NONCE();

  stylus_report->stylus->x = report->x;
  stylus_report->stylus->y = report->y;
  stylus_report->stylus->pressure = report->pressure;

  stylus_report->stylus->is_in_contact =
      acer12_stylus_status_inrange(report->status) &&
      (acer12_stylus_status_tswitch(report->status) ||
       acer12_stylus_status_eraser(report->status));

  stylus_report->stylus->in_range =
      acer12_stylus_status_inrange(report->status);

  if (acer12_stylus_status_invert(report->status) ||
      acer12_stylus_status_eraser(report->status)) {
    stylus_report->stylus->is_inverted = true;
  }

  if (acer12_stylus_status_barrel(report->status)) {
    stylus_report->stylus->pressed_buttons |= fuchsia::ui::input::kStylusBarrel;
  }
  FXL_VLOG(2) << name() << " parsed: " << *stylus_report;

  return true;
}

bool Hardcoded::ParseSamsungTouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(samsung_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<samsung_touch_t*>(r));
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();

  size_t index = 0;
  touchscreen_report->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!samsung_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = samsung_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = report.fingers[i].width;
    touch.height = report.fingers[i].height;
    touchscreen_report->touchscreen->touches.resize(index + 1);
    touchscreen_report->touchscreen->touches.at(index++) = std::move(touch);
  }

  return true;
}

bool Hardcoded::ParseParadiseTouchscreenReportV1(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  return ParseParadiseTouchscreenReport<paradise_touch_t>(r, len,
                                                          touchscreen_report);
}

bool Hardcoded::ParseParadiseTouchscreenReportV2(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  return ParseParadiseTouchscreenReport<paradise_touch_v2_t>(
      r, len, touchscreen_report);
}

template <typename ReportT>
bool Hardcoded::ParseParadiseTouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(ReportT)) {
    FXL_LOG(INFO) << "paradise wrong size " << len;
    return false;
  }

  const auto& report = *(reinterpret_cast<ReportT*>(r));
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();

  size_t index = 0;
  touchscreen_report->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    if (!paradise_finger_flags_tswitch(report.fingers[i].flags))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = report.fingers[i].finger_id;
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = 5;  // TODO(cpu): Don't hardcode |width| or |height|.
    touch.height = 5;
    touchscreen_report->touchscreen->touches.resize(index + 1);
    touchscreen_report->touchscreen->touches.at(index++) = std::move(touch);
  }

  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report;
  return true;
}

bool Hardcoded::ParseEGalaxTouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(egalax_touch_t)) {
    FXL_LOG(INFO) << "egalax wrong size " << len << " expected "
                  << sizeof(egalax_touch_t);
    return false;
  }

  const auto& report = *(reinterpret_cast<egalax_touch_t*>(r));
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();
  if (egalax_pressed_flags(report.button_pad)) {
    fuchsia::ui::input::Touch touch;
    touch.finger_id = 0;
    touch.x = report.x;
    touch.y = report.y;
    touch.width = 5;
    touch.height = 5;
    touchscreen_report->touchscreen->touches.resize(1);
    touchscreen_report->touchscreen->touches.at(0) = std::move(touch);
  } else {
    // if the button isn't pressed, send an empty report, this will terminate
    // the finger session
    touchscreen_report->touchscreen->touches.resize(0);
  }

  FXL_VLOG(2) << name() << " parsed: " << *touchscreen_report;
  return true;
}

bool Hardcoded::ParseParadiseTouchpadReportV1(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* touchpad_report) {
  return ParseParadiseTouchpadReport<paradise_touchpad_v1_t>(r, len,
                                                             touchpad_report);
}

bool Hardcoded::ParseParadiseTouchpadReportV2(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* touchpad_report) {
  return ParseParadiseTouchpadReport<paradise_touchpad_v1_t>(r, len,
                                                             touchpad_report);
}

template <typename ReportT>
bool Hardcoded::ParseParadiseTouchpadReport(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* mouse_report) {
  if (len != sizeof(ReportT)) {
    FXL_LOG(INFO) << "paradise wrong size " << len;
    return false;
  }

  mouse_report->event_time = InputEventTimestampNow();
  mouse_report->trace_id = TRACE_NONCE();

  const auto& report = *(reinterpret_cast<ReportT*>(r));
  if (!report.fingers[0].tip_switch) {
    mouse_report->mouse->rel_x = 0;
    mouse_report->mouse->rel_y = 0;
    mouse_report->mouse->pressed_buttons = 0;

    mouse_abs_x_ = -1;
    return true;
  }

  // Each axis has a resolution of .00078125cm. 5/32 is a relatively arbitrary
  // coefficient that gives decent sensitivity and a nice resolution of .005cm.
  mouse_report->mouse->rel_x =
      mouse_abs_x_ != -1 ? 5 * (report.fingers[0].x - mouse_abs_x_) / 32 : 0;
  mouse_report->mouse->rel_y =
      mouse_abs_x_ != -1 ? 5 * (report.fingers[0].y - mouse_abs_y_) / 32 : 0;
  mouse_report->mouse->pressed_buttons =
      report.button ? fuchsia::ui::input::kMouseButtonPrimary : 0;

  // Don't update the abs position if there was no relative change, so that
  // we don't drop fractional relative deltas.
  if (mouse_report->mouse->rel_y || mouse_abs_x_ == -1) {
    mouse_abs_y_ = report.fingers[0].y;
  }
  if (mouse_report->mouse->rel_x || mouse_abs_x_ == -1) {
    mouse_abs_x_ = report.fingers[0].x;
  }

  return true;
}

bool Hardcoded::ParseParadiseStylusReport(
    uint8_t* r, size_t len, fuchsia::ui::input::InputReport* stylus_report) {
  if (len != sizeof(paradise_stylus_t)) {
    FXL_LOG(INFO) << "paradise wrong stylus report size " << len;
    return false;
  }

  auto report = reinterpret_cast<paradise_stylus_t*>(r);
  stylus_report->event_time = InputEventTimestampNow();
  stylus_report->trace_id = TRACE_NONCE();

  stylus_report->stylus->x = report->x;
  stylus_report->stylus->y = report->y;
  stylus_report->stylus->pressure = report->pressure;

  stylus_report->stylus->is_in_contact =
      paradise_stylus_status_inrange(report->status) &&
      (paradise_stylus_status_tswitch(report->status) ||
       paradise_stylus_status_eraser(report->status));

  stylus_report->stylus->in_range =
      paradise_stylus_status_inrange(report->status);

  if (paradise_stylus_status_invert(report->status) ||
      paradise_stylus_status_eraser(report->status)) {
    stylus_report->stylus->is_inverted = true;
  }

  if (paradise_stylus_status_barrel(report->status)) {
    stylus_report->stylus->pressed_buttons |= fuchsia::ui::input::kStylusBarrel;
  }
  FXL_VLOG(2) << name() << " parsed: " << *stylus_report;

  return true;
}

bool Hardcoded::ParseEyoyoTouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(eyoyo_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<eyoyo_touch_t*>(r));
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();

  size_t index = 0;
  touchscreen_report->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!eyoyo_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = eyoyo_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    // Panel does not support touch width/height.
    touch.width = 5;
    touch.height = 5;
    touchscreen_report->touchscreen->touches.resize(index + 1);
    touchscreen_report->touchscreen->touches.at(index++) = std::move(touch);
  }

  return true;
}

bool Hardcoded::ParseFt3x27TouchscreenReport(
    uint8_t* r, size_t len,
    fuchsia::ui::input::InputReport* touchscreen_report) {
  if (len != sizeof(ft3x27_touch_t)) {
    return false;
  }

  const auto& report = *(reinterpret_cast<ft3x27_touch_t*>(r));
  touchscreen_report->event_time = InputEventTimestampNow();
  touchscreen_report->trace_id = TRACE_NONCE();

  size_t index = 0;
  touchscreen_report->touchscreen->touches.resize(index);

  for (size_t i = 0; i < arraysize(report.fingers); ++i) {
    auto fid = report.fingers[i].finger_id;

    if (!ft3x27_finger_id_tswitch(fid))
      continue;

    fuchsia::ui::input::Touch touch;
    touch.finger_id = ft3x27_finger_id_contact(fid);
    touch.x = report.fingers[i].x;
    touch.y = report.fingers[i].y;
    touch.width = 5;
    touch.height = 5;
    touchscreen_report->touchscreen->touches.resize(index + 1);
    touchscreen_report->touchscreen->touches.at(index++) = std::move(touch);
    FXL_VLOG(2) << name()
                << " parsed (sensor=" << static_cast<uint16_t>(touch.finger_id)
                << ") x=" << touch.x << ", y=" << touch.y;
  }

  return true;
}

bool Hardcoded::ParseReport(const uint8_t* report, size_t len,
                            HidButtons* data) {
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

bool Hardcoded::ParseButtonsReport(
    const uint8_t* report, size_t len,
    fuchsia::ui::input::InputReport* buttons_report) {
  HidButtons data;
  if (!ParseReport(report, len, &data)) {
    FXL_LOG(ERROR) << " failed reading from buttons";
    return false;
  }
  buttons_report->media_buttons->volume = data.volume;
  buttons_report->media_buttons->mic_mute = data.mic_mute;
  buttons_report->event_time = InputEventTimestampNow();
  buttons_report->trace_id = TRACE_NONCE();

  FXL_VLOG(2) << name() << " parsed buttons: " << *buttons_report
              << " volume: " << static_cast<int32_t>(data.volume)
              << " mic mute: " << (data.mic_mute ? "yes" : "no");
  return true;
}

bool Hardcoded::ParseParadiseSensorReport(
    uint8_t* r, size_t len, uint8_t* sensor_idx,
    fuchsia::ui::input::InputReport* sensor_report) {
  if (len != sizeof(paradise_sensor_vector_data_t) &&
      len != sizeof(paradise_sensor_scalar_data_t)) {
    FXL_LOG(INFO) << "paradise sensor data: wrong size " << len << ", expected "
                  << sizeof(paradise_sensor_vector_data_t) << " or "
                  << sizeof(paradise_sensor_scalar_data_t);
    return false;
  }

  sensor_report->event_time = InputEventTimestampNow();
  sensor_report->trace_id = TRACE_NONCE();
  *sensor_idx = r[0];  // We know sensor structs start with sensor ID.
  switch (*sensor_idx) {
    case kParadiseAccLid:
    case kParadiseAccBase: {
      const auto& report =
          *(reinterpret_cast<paradise_sensor_vector_data_t*>(r));
      fidl::Array<int16_t, 3> data;
      data[0] = report.vector[0];
      data[1] = report.vector[1];
      data[2] = report.vector[2];
      sensor_report->sensor->set_vector(std::move(data));
    } break;
    case 2:
    case 3:
    case 4:
      // TODO(SCN-626): Expose other sensors.
      return false;
    default:
      FXL_LOG(ERROR) << "paradise sensor unrecognized: " << *sensor_idx;
      return false;
  }

  FXL_VLOG(3) << name()
              << " parsed (sensor=" << static_cast<uint16_t>(*sensor_idx)
              << "): " << *sensor_report;
  return true;
}

bool Hardcoded::ParseReport(const uint8_t* report, size_t len,
                            HidAmbientLightSimple* data) {
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

bool Hardcoded::ParseAmbientLightSensorReport(
    const uint8_t* report, size_t len, uint8_t* sensor_idx,
    fuchsia::ui::input::InputReport* sensor_report) {
  HidAmbientLightSimple data;
  if (!ParseReport(report, len, &data)) {
    FXL_LOG(ERROR) << " failed reading from ambient light sensor";
    return false;
  }
  sensor_report->sensor->set_scalar(data.illuminance);
  sensor_report->event_time = InputEventTimestampNow();
  sensor_report->trace_id = TRACE_NONCE();
  *sensor_idx = kAmbientLight;

  FXL_VLOG(2) << name()
              << " parsed (sensor=" << static_cast<uint16_t>(*sensor_idx)
              << "): " << *sensor_report;
  return true;
}

}  // namespace mozart
