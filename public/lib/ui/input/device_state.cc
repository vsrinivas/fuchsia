// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/device_state.h"

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace {
int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

namespace mozart {

constexpr zx::duration kKeyRepeatSlow = zx::msec(250);
constexpr zx::duration kKeyRepeatFast = zx::msec(75);

KeyboardState::KeyboardState(DeviceState* device_state)
    : device_state_(device_state),
      keymap_(qwerty_map),
      weak_ptr_factory_(this) {
  char* keys = getenv("gfxconsole.keymap");
  if (keys && !strcmp(keys, "dvorak")) {
    keymap_ = dvorak_map;
  }
}

void KeyboardState::SendEvent(fuchsia::ui::input::KeyboardEventPhase phase,
                              uint32_t key, uint64_t modifiers,
                              uint64_t timestamp) {
  fuchsia::ui::input::InputEvent ev;
  fuchsia::ui::input::KeyboardEvent kb;
  kb.phase = phase;
  kb.event_time = timestamp;
  kb.device_id = device_state_->device_id();
  kb.hid_usage = key;
  kb.code_point =
      hid_map_key(key,
                  modifiers & (fuchsia::ui::input::kModifierShift |
                               fuchsia::ui::input::kModifierCapsLock),
                  keymap_);
  kb.modifiers = modifiers;
  ev.set_keyboard(std::move(kb));
  device_state_->callback()(std::move(ev));
}

void KeyboardState::Update(fuchsia::ui::input::InputReport input_report) {
  FXL_DCHECK(input_report.keyboard);

  uint64_t now = input_report.event_time;
  std::vector<uint32_t> old_keys = keys_;
  keys_.clear();
  repeat_keys_.clear();

  for (uint32_t key : *input_report.keyboard->pressed_keys) {
    keys_.push_back(key);
    auto it = std::find(old_keys.begin(), old_keys.end(), key);
    if (it != old_keys.end()) {
      old_keys.erase(it);
      continue;
    }

    SendEvent(fuchsia::ui::input::KeyboardEventPhase::PRESSED, key, modifiers_,
              now);

    uint64_t modifiers = modifiers_;
    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ |= fuchsia::ui::input::kModifierLeftShift;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= fuchsia::ui::input::kModifierRightShift;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= fuchsia::ui::input::kModifierLeftControl;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= fuchsia::ui::input::kModifierRightControl;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= fuchsia::ui::input::kModifierLeftAlt;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= fuchsia::ui::input::kModifierRightAlt;
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ |= fuchsia::ui::input::kModifierLeftSuper;
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ |= fuchsia::ui::input::kModifierRightSuper;
        break;
      default:
        break;
    }

    // Don't repeat modifier by themselves
    if (modifiers == modifiers_) {
      repeat_keys_.push_back(key);
    }
  }

  // If any key was released as well, do not repeat
  if (!old_keys.empty()) {
    repeat_keys_.clear();
  }

  for (uint32_t key : old_keys) {
    SendEvent(fuchsia::ui::input::KeyboardEventPhase::RELEASED, key, modifiers_,
              now);

    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~fuchsia::ui::input::kModifierLeftShift);
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~fuchsia::ui::input::kModifierRightShift);
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~fuchsia::ui::input::kModifierLeftControl);
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~fuchsia::ui::input::kModifierRightControl);
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~fuchsia::ui::input::kModifierLeftAlt);
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~fuchsia::ui::input::kModifierRightAlt);
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ &= (~fuchsia::ui::input::kModifierLeftSuper);
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ &= (~fuchsia::ui::input::kModifierRightSuper);
        break;
      case HID_USAGE_KEY_CAPSLOCK:
        if (modifiers_ & fuchsia::ui::input::kModifierCapsLock) {
          modifiers_ &= (~fuchsia::ui::input::kModifierCapsLock);
        } else {
          modifiers_ |= fuchsia::ui::input::kModifierCapsLock;
        }
        break;
      default:
        break;
    }
  }

  if (!repeat_keys_.empty()) {
    ScheduleRepeat(++repeat_sequence_, kKeyRepeatSlow);
  } else {
    ++repeat_sequence_;
  }
}

void KeyboardState::Repeat(uint64_t sequence) {
  if (sequence != repeat_sequence_) {
    return;
  }
  uint64_t now = InputEventTimestampNow();
  for (uint32_t key : repeat_keys_) {
    SendEvent(fuchsia::ui::input::KeyboardEventPhase::REPEAT, key, modifiers_,
              now);
  }
  ScheduleRepeat(sequence, kKeyRepeatFast);
}

void KeyboardState::ScheduleRepeat(uint64_t sequence, zx::duration delta) {
  async::PostDelayedTask(async_get_default_dispatcher(),
                         [weak = weak_ptr_factory_.GetWeakPtr(), sequence] {
                           if (weak)
                             weak->Repeat(sequence);
                         },
                         delta);
}

void MouseState::OnRegistered() {}

void MouseState::OnUnregistered() {}

void MouseState::SendEvent(float rel_x, float rel_y, int64_t timestamp,
                           fuchsia::ui::input::PointerEventPhase phase,
                           uint32_t buttons) {
  fuchsia::ui::input::InputEvent ev;
  fuchsia::ui::input::PointerEvent pt;
  pt.event_time = timestamp;
  pt.device_id = device_state_->device_id();
  pt.pointer_id = device_state_->device_id();
  pt.phase = phase;
  pt.buttons = buttons;
  pt.type = fuchsia::ui::input::PointerEventType::MOUSE;
  pt.x = rel_x;
  pt.y = rel_y;
  ev.set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void MouseState::Update(fuchsia::ui::input::InputReport input_report,
                        fuchsia::math::Size display_size) {
  FXL_DCHECK(input_report.mouse);
  uint64_t now = input_report.event_time;
  uint8_t pressed = (input_report.mouse->pressed_buttons ^ buttons_) &
                    input_report.mouse->pressed_buttons;
  uint8_t released =
      (input_report.mouse->pressed_buttons ^ buttons_) & buttons_;
  buttons_ = input_report.mouse->pressed_buttons;

  // TODO(jpoichet) Update once we have an API to capture mouse.
  // TODO(MZ-385): Quantize the mouse value to the range [0, display_width -
  // mouse_resolution]
  position_.x =
      std::max(0.0f, std::min(position_.x + input_report.mouse->rel_x,
                              static_cast<float>(display_size.width)));
  position_.y =
      std::max(0.0f, std::min(position_.y + input_report.mouse->rel_y,
                              static_cast<float>(display_size.height)));

  if (!pressed && !released) {
    SendEvent(position_.x, position_.y, now,
              fuchsia::ui::input::PointerEventPhase::MOVE, buttons_);
  } else {
    if (pressed) {
      SendEvent(position_.x, position_.y, now,
                fuchsia::ui::input::PointerEventPhase::DOWN, pressed);
    }
    if (released) {
      SendEvent(position_.x, position_.y, now,
                fuchsia::ui::input::PointerEventPhase::UP, released);
    }
  }
}

void StylusState::SendEvent(int64_t timestamp,
                            fuchsia::ui::input::PointerEventPhase phase,
                            fuchsia::ui::input::PointerEventType type, float x,
                            float y, uint32_t buttons) {
  fuchsia::ui::input::PointerEvent pt;
  pt.event_time = timestamp;
  pt.device_id = device_state_->device_id();
  pt.pointer_id = 1;
  pt.type = type;
  pt.phase = phase;
  pt.x = x;
  pt.y = y;
  pt.buttons = buttons;

  stylus_ = pt;

  fuchsia::ui::input::InputEvent ev;
  ev.set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void StylusState::Update(fuchsia::ui::input::InputReport input_report,
                         fuchsia::math::Size display_size) {
  FXL_DCHECK(input_report.stylus);

  fuchsia::ui::input::StylusDescriptor* descriptor =
      device_state_->stylus_descriptor();
  FXL_DCHECK(descriptor);

  const bool previous_stylus_down = stylus_down_;
  const bool previous_stylus_in_range = stylus_in_range_;
  stylus_down_ = input_report.stylus->is_in_contact;
  stylus_in_range_ = input_report.stylus->in_range;

  fuchsia::ui::input::PointerEventPhase phase =
      fuchsia::ui::input::PointerEventPhase::DOWN;
  if (stylus_down_) {
    if (previous_stylus_down) {
      phase = fuchsia::ui::input::PointerEventPhase::MOVE;
    }
  } else {
    if (previous_stylus_down) {
      phase = fuchsia::ui::input::PointerEventPhase::UP;
    } else {
      if (stylus_in_range_ && !previous_stylus_in_range) {
        inverted_stylus_ = input_report.stylus->is_inverted;
        phase = fuchsia::ui::input::PointerEventPhase::ADD;
      } else if (!stylus_in_range_ && previous_stylus_in_range) {
        phase = fuchsia::ui::input::PointerEventPhase::REMOVE;
      } else if (stylus_in_range_) {
        phase = fuchsia::ui::input::PointerEventPhase::HOVER;
      } else {
        return;
      }
    }
  }

  uint64_t now = input_report.event_time;

  if (phase == fuchsia::ui::input::PointerEventPhase::UP) {
    SendEvent(now, phase,
              inverted_stylus_
                  ? fuchsia::ui::input::PointerEventType::INVERTED_STYLUS
                  : fuchsia::ui::input::PointerEventType::STYLUS,
              stylus_.x, stylus_.y, stylus_.buttons);
  } else {
    // Quantize the value to [0, 1) based on the resolution.
    float x_denominator =
        (1 +
         static_cast<float>(descriptor->x.range.max - descriptor->x.range.min) /
             static_cast<float>(descriptor->x.resolution)) *
        static_cast<float>(descriptor->x.resolution);
    float x =
        static_cast<float>(display_size.width *
                           (input_report.stylus->x - descriptor->x.range.min)) /
        x_denominator;

    float y_denominator =
        (1 +
         static_cast<float>(descriptor->y.range.max - descriptor->y.range.min) /
             static_cast<float>(descriptor->y.resolution)) *
        static_cast<float>(descriptor->y.resolution);
    float y =
        static_cast<float>(display_size.height *
                           (input_report.stylus->y - descriptor->y.range.min)) /
        y_denominator;

    uint32_t buttons = 0;
    if (input_report.stylus->pressed_buttons &
        fuchsia::ui::input::kStylusBarrel) {
      buttons |= fuchsia::ui::input::kStylusPrimaryButton;
    }
    SendEvent(now, phase,
              inverted_stylus_
                  ? fuchsia::ui::input::PointerEventType::INVERTED_STYLUS
                  : fuchsia::ui::input::PointerEventType::STYLUS,
              x, y, buttons);
  }
}

void TouchscreenState::Update(fuchsia::ui::input::InputReport input_report,
                              fuchsia::math::Size display_size) {
  FXL_DCHECK(input_report.touchscreen);
  fuchsia::ui::input::TouchscreenDescriptor* descriptor =
      device_state_->touchscreen_descriptor();
  FXL_DCHECK(descriptor);

  std::vector<fuchsia::ui::input::PointerEvent> old_pointers = pointers_;
  pointers_.clear();

  uint64_t now = input_report.event_time;

  for (auto& touch : *input_report.touchscreen->touches) {
    fuchsia::ui::input::InputEvent ev;
    fuchsia::ui::input::PointerEvent pt;
    pt.event_time = now;
    pt.device_id = device_state_->device_id();
    pt.phase = fuchsia::ui::input::PointerEventPhase::DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      FXL_DCHECK(touch.finger_id >= 0);
      if (it->pointer_id == static_cast<uint32_t>(touch.finger_id)) {
        pt.phase = fuchsia::ui::input::PointerEventPhase::MOVE;
        old_pointers.erase(it);
        break;
      }
    }

    pt.pointer_id = touch.finger_id;
    pt.type = fuchsia::ui::input::PointerEventType::TOUCH;

    // Quantize the value to [0, 1) based on the resolution.
    float x_denominator =
        (1 +
         static_cast<float>(descriptor->x.range.max - descriptor->x.range.min) /
             static_cast<float>(descriptor->x.resolution)) *
        static_cast<float>(descriptor->x.resolution);
    float x = static_cast<float>(display_size.width *
                                 (touch.x - descriptor->x.range.min)) /
              x_denominator;

    float y_denominator =
        (1 +
         static_cast<float>(descriptor->y.range.max - descriptor->y.range.min) /
             static_cast<float>(descriptor->y.resolution)) *
        static_cast<float>(descriptor->y.resolution);
    float y = static_cast<float>(display_size.height *
                                 (touch.y - descriptor->y.range.min)) /
              y_denominator;

    uint32_t width = 2 * touch.width;
    uint32_t height = 2 * touch.height;

    pt.x = x;
    pt.y = y;
    pt.radius_major = width > height ? width : height;
    pt.radius_minor = width > height ? height : width;
    pointers_.push_back(pt);

    // For now when we get DOWN we need to fake trigger ADD first.
    if (pt.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
      fuchsia::ui::input::InputEvent add_ev = fidl::Clone(ev);
      fuchsia::ui::input::PointerEvent add_pt = fidl::Clone(pt);
      add_pt.phase = fuchsia::ui::input::PointerEventPhase::ADD;
      add_ev.set_pointer(std::move(add_pt));
      device_state_->callback()(std::move(add_ev));
    }

    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    fuchsia::ui::input::InputEvent ev;
    fuchsia::ui::input::PointerEvent pt = fidl::Clone(pointer);
    pt.phase = fuchsia::ui::input::PointerEventPhase::UP;
    pt.event_time = now;
    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));

    ev = fuchsia::ui::input::InputEvent();
    pt = fidl::Clone(pointer);
    pt.phase = fuchsia::ui::input::PointerEventPhase::REMOVE;
    pt.event_time = now;
    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }
}

void SensorState::Update(fuchsia::ui::input::InputReport input_report) {
  FXL_DCHECK(input_report.sensor);
  FXL_DCHECK(device_state_->sensor_descriptor());
  // Every sensor report gets routed via unique device_id.
  device_state_->sensor_callback()(device_state_->device_id(),
                                   std::move(input_report));
}

DeviceState::DeviceState(uint32_t device_id,
                         fuchsia::ui::input::DeviceDescriptor* descriptor,
                         OnEventCallback callback)
    : device_id_(device_id),
      descriptor_(descriptor),
      keyboard_(this),
      mouse_(this),
      stylus_(this),
      touchscreen_(this),
      callback_(std::move(callback)),
      sensor_(this),
      sensor_callback_(nullptr) {}

DeviceState::DeviceState(uint32_t device_id,
                         fuchsia::ui::input::DeviceDescriptor* descriptor,
                         OnSensorEventCallback callback)
    : device_id_(device_id),
      descriptor_(descriptor),
      keyboard_(this),
      mouse_(this),
      stylus_(this),
      touchscreen_(this),
      callback_(nullptr),
      sensor_(this),
      sensor_callback_(std::move(callback)) {}

DeviceState::~DeviceState() {}

void DeviceState::OnRegistered() {
  if (descriptor_->keyboard) {
    keyboard_.OnRegistered();
  }
  if (descriptor_->mouse) {
    mouse_.OnRegistered();
  }
  if (descriptor_->stylus) {
    stylus_.OnRegistered();
  }
  if (descriptor_->touchscreen) {
    touchscreen_.OnRegistered();
  }
  if (descriptor_->sensor) {
    sensor_.OnRegistered();
  }
}

void DeviceState::OnUnregistered() {
  if (descriptor_->keyboard) {
    keyboard_.OnUnregistered();
  }
  if (descriptor_->mouse) {
    mouse_.OnUnregistered();
  }
  if (descriptor_->stylus) {
    stylus_.OnUnregistered();
  }
  if (descriptor_->touchscreen) {
    touchscreen_.OnUnregistered();
  }
  if (descriptor_->sensor) {
    sensor_.OnUnregistered();
  }
}

void DeviceState::Update(fuchsia::ui::input::InputReport input_report,
                         fuchsia::math::Size display_size) {
  if (input_report.keyboard && descriptor_->keyboard) {
    keyboard_.Update(std::move(input_report));
  } else if (input_report.mouse && descriptor_->mouse) {
    mouse_.Update(std::move(input_report), display_size);
  } else if (input_report.stylus && descriptor_->stylus) {
    stylus_.Update(std::move(input_report), display_size);
  } else if (input_report.touchscreen && descriptor_->touchscreen) {
    touchscreen_.Update(std::move(input_report), display_size);
  } else if (input_report.sensor && descriptor_->sensor) {
    sensor_.Update(std::move(input_report));
  }
}

}  // namespace mozart
