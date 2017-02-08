// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_reader/input_state.h"

#include "apps/mozart/src/input_reader/input_device.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace {
// The input event fidl is currently defined to expect some number
// of milliseconds.
int64_t InputEventTimestampNow() {
  return ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

namespace mozart {
namespace input {

constexpr ftl::TimeDelta kKeyRepeatSlow = ftl::TimeDelta::FromMilliseconds(250);
constexpr ftl::TimeDelta kKeyRepeatFast = ftl::TimeDelta::FromMilliseconds(75);

#pragma mark - KeyboardState

KeyboardState::KeyboardState(uint32_t device_id, OnEventCallback callback)
    : device_id_(device_id),
      callback_(callback),
      keymap_(qwerty_map),
      weak_ptr_factory_(this),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {
  char* keys = getenv("gfxconsole.keymap");
  if (keys && !strcmp(keys, "dvorak")) {
    keymap_ = dvorak_map;
  }
}

void KeyboardState::SendEvent(mozart::KeyboardEvent::Phase phase,
                              KeyUsage key,
                              uint64_t modifiers,
                              uint64_t timestamp) {
  auto ev = mozart::InputEvent::New();
  auto kb = mozart::KeyboardEvent::New();
  kb->phase = phase;
  kb->event_time = timestamp;
  kb->device_id = device_id_;
  kb->hid_usage = key;
  kb->code_point = hid_map_key(
      key, modifiers & (mozart::kModifierShift | mozart::kModifierCapsLock),
      keymap_);
  kb->modifiers = modifiers;
  ev->set_keyboard(std::move(kb));
  callback_(std::move(ev));
}

void KeyboardState::Update(const KeyboardReport& report,
                           const KeyboardDescriptor& descriptor) {
  uint64_t now = InputEventTimestampNow();
  std::vector<KeyUsage> old_keys = keys_;
  keys_.clear();
  repeat_keys_.clear();

  for (KeyUsage key : report.down) {
    keys_.push_back(key);
    auto it = std::find(old_keys.begin(), old_keys.end(), key);
    if (it != old_keys.end()) {
      old_keys.erase(it);
      continue;
    }

    SendEvent(mozart::KeyboardEvent::Phase::PRESSED, key, modifiers_, now);

    uint64_t modifiers = modifiers_;
    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ |= kModifierLeftShift;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= kModifierRightShift;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= kModifierLeftControl;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= kModifierRightControl;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= kModifierLeftAlt;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= kModifierRightAlt;
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ |= kModifierLeftSuper;
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ |= kModifierRightSuper;
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

  for (KeyUsage key : old_keys) {
    SendEvent(mozart::KeyboardEvent::Phase::RELEASED, key, modifiers_, now);

    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~kModifierLeftShift);
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~kModifierRightShift);
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~kModifierLeftControl);
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~kModifierRightControl);
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~kModifierLeftAlt);
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~kModifierRightAlt);
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ &= (~kModifierLeftSuper);
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ &= (~kModifierRightSuper);
        break;
      case HID_USAGE_KEY_CAPSLOCK:
        if (modifiers_ & kModifierCapsLock) {
          modifiers_ &= (~kModifierCapsLock);
        } else {
          modifiers_ |= kModifierCapsLock;
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
  for (KeyUsage key : repeat_keys_) {
    SendEvent(mozart::KeyboardEvent::Phase::REPEAT, key, modifiers_, now);
  }
  ScheduleRepeat(sequence, kKeyRepeatFast);
}

void KeyboardState::ScheduleRepeat(uint64_t sequence, ftl::TimeDelta delta) {
  task_runner_->PostDelayedTask(
      [ weak = weak_ptr_factory_.GetWeakPtr(), sequence ] {
        if (weak)
          weak->Repeat(sequence);
      },
      delta);
}

#pragma mark - MouseState

void MouseState::OnRegistered() {
  SendEvent(0, 0, InputEventTimestampNow(), mozart::PointerEvent::Phase::ADD,
            0);
}

void MouseState::OnUnregistered() {
  SendEvent(0, 0, InputEventTimestampNow(), mozart::PointerEvent::Phase::REMOVE,
            0);
}

void MouseState::SendEvent(float rel_x,
                           float rel_y,
                           int64_t timestamp,
                           mozart::PointerEvent::Phase phase,
                           uint32_t buttons) {
  mozart::InputEventPtr ev = mozart::InputEvent::New();
  mozart::PointerEventPtr pt = mozart::PointerEvent::New();
  pt->event_time = timestamp;
  pt->device_id = device_id_;
  pt->phase = phase;
  pt->buttons = buttons;
  pt->type = mozart::PointerEvent::Type::MOUSE;
  pt->x = rel_x;
  pt->y = rel_y;
  ev->set_pointer(std::move(pt));
  callback_(std::move(ev));
}

void MouseState::Update(const MouseReport& report,
                        const MouseDescriptor& descriptor,
                        mozart::Size display_size) {
  uint64_t now = InputEventTimestampNow();
  uint8_t pressed = (report.buttons ^ buttons_) & report.buttons;
  uint8_t released = (report.buttons ^ buttons_) & buttons_;
  buttons_ = report.buttons;

  // TODO(jpoichet) coordinate interpretation should move in dispatcher
  // and be dependent on whether the mouse is locked or not
  position_.x =
      std::max(0.0f, std::min(position_.x + report.rel_x,
                              static_cast<float>(display_size.width)));
  position_.y =
      std::max(0.0f, std::min(position_.y + report.rel_y,
                              static_cast<float>(display_size.height)));

  if (!pressed && !released) {
    SendEvent(position_.x, position_.y, now, mozart::PointerEvent::Phase::MOVE,
              buttons_);
  } else {
    if (pressed) {
      SendEvent(position_.x, position_.y, now,
                mozart::PointerEvent::Phase::DOWN, pressed);
    }
    if (released) {
      SendEvent(position_.x, position_.y, now, mozart::PointerEvent::Phase::UP,
                released);
    }
  }
}

#pragma mark - StylusState

void StylusState::SendEvent(int64_t timestamp,
                            mozart::PointerEvent::Phase phase,
                            mozart::PointerEvent::Type type,
                            float x,
                            float y,
                            uint32_t buttons) {
  auto pt = mozart::PointerEvent::New();
  pt->event_time = timestamp;
  pt->device_id = device_id_;
  pt->pointer_id = 1;
  pt->type = type;
  pt->phase = phase;
  pt->x = x;
  pt->y = y;
  pt->buttons = buttons;

  stylus_ = *pt;

  auto ev = mozart::InputEvent::New();
  ev->set_pointer(std::move(pt));
  callback_(std::move(ev));
}

void StylusState::Update(const StylusReport& report,
                         const StylusDescriptor& descriptor,
                         mozart::Size display_size) {
  const bool previous_stylus_down = stylus_down_;
  const bool previous_stylus_in_range = stylus_in_range_;
  stylus_down_ = report.is_down;
  stylus_in_range_ = report.in_range;

  mozart::PointerEvent::Phase phase = mozart::PointerEvent::Phase::DOWN;
  if (stylus_down_) {
    if (previous_stylus_down) {
      phase = mozart::PointerEvent::Phase::MOVE;
    }
  } else {
    if (previous_stylus_down) {
      phase = mozart::PointerEvent::Phase::UP;
    } else {
      if (stylus_in_range_ && !previous_stylus_in_range) {
        inverted_stylus_ = report.pressed(INPUT_USAGE_STYLUS_INVERT) ||
                           report.pressed(INPUT_USAGE_STYLUS_ERASER);
        phase = mozart::PointerEvent::Phase::ADD;
      } else if (!stylus_in_range_ && previous_stylus_in_range) {
        phase = mozart::PointerEvent::Phase::REMOVE;
      } else if (stylus_in_range_) {
        phase = mozart::PointerEvent::Phase::HOVER;
      } else {
        return;
      }
    }
  }

  int64_t now = InputEventTimestampNow();
  if (phase == mozart::PointerEvent::Phase::UP) {
    SendEvent(now, phase,
              inverted_stylus_ ? mozart::PointerEvent::Type::INVERTED_STYLUS
                               : mozart::PointerEvent::Type::STYLUS,
              stylus_.x, stylus_.y, stylus_.buttons);
  } else {
    // TODO(jpoichet) coordinate interpretation should move in dispatcher
    float x =
        static_cast<float>(display_size.width *
                           (report.x - descriptor.x.range.min)) /
        static_cast<float>(descriptor.x.range.max - descriptor.x.range.min);
    float y =
        static_cast<float>(display_size.height *
                           (report.y - descriptor.y.range.min)) /
        static_cast<float>(descriptor.y.range.max - descriptor.y.range.min);

    uint32_t buttons = 0;
    if (report.pressed(INPUT_USAGE_STYLUS_TIP)) {
      buttons |= kStylusPrimaryButton;
    }
    if (report.pressed(INPUT_USAGE_STYLUS_BARREL)) {
      buttons |= kStylusSecondaryButton;
    }
    if (report.pressed(INPUT_USAGE_STYLUS_ERASER)) {
      // Eraser is when stylus is inverted and down
      FTL_DCHECK(inverted_stylus_);
      buttons |= kStylusPrimaryButton;
    }

    SendEvent(now, phase,
              inverted_stylus_ ? mozart::PointerEvent::Type::INVERTED_STYLUS
                               : mozart::PointerEvent::Type::STYLUS,
              x, y, stylus_.buttons);
  }
}

#pragma mark - TouchscreenState

void TouchscreenState::Update(const TouchReport& report,
                              const TouchscreenDescriptor& descriptor,
                              mozart::Size display_size) {
  std::vector<mozart::PointerEvent> old_pointers = pointers_;
  pointers_.clear();

  int64_t now = InputEventTimestampNow();

  for (auto touch : report.touches) {
    auto ev = mozart::InputEvent::New();
    auto pt = mozart::PointerEvent::New();
    pt->event_time = now;
    pt->device_id = device_id_;
    pt->phase = mozart::PointerEvent::Phase::DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      FTL_DCHECK(touch.finger_id >= 0);
      if (it->pointer_id == static_cast<uint32_t>(touch.finger_id)) {
        pt->phase = mozart::PointerEvent::Phase::MOVE;
        old_pointers.erase(it);
        break;
      }
    }

    pt->pointer_id = touch.finger_id;
    pt->type = mozart::PointerEvent::Type::TOUCH;

    float x =
        static_cast<float>(display_size.width *
                           (touch.x - descriptor.x.range.min)) /
        static_cast<float>(descriptor.x.range.max - descriptor.x.range.min);
    float y =
        static_cast<float>(display_size.height *
                           (touch.y - descriptor.y.range.min)) /
        static_cast<float>(descriptor.y.range.max - descriptor.y.range.min);

    uint32_t width = 2 * touch.width;
    uint32_t height = 2 * touch.height;

    pt->x = x;
    pt->y = y;
    pt->radius_major = width > height ? width : height;
    pt->radius_minor = width > height ? height : width;
    pointers_.push_back(*pt);

    // For now when we get DOWN we need to fake trigger ADD first.
    if (pt->phase == mozart::PointerEvent::Phase::DOWN) {
      auto add_ev = ev.Clone();
      auto add_pt = pt.Clone();
      add_pt->phase = mozart::PointerEvent::Phase::ADD;
      add_ev->set_pointer(std::move(add_pt));
      callback_(std::move(add_ev));
    }

    ev->set_pointer(std::move(pt));
    callback_(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    auto ev = mozart::InputEvent::New();
    auto pt = pointer.Clone();
    pt->phase = mozart::PointerEvent::Phase::UP;
    pt->event_time = now;
    ev->set_pointer(std::move(pt));
    callback_(std::move(ev));

    ev = mozart::InputEvent::New();
    pt = pointer.Clone();
    pt->phase = mozart::PointerEvent::Phase::REMOVE;
    pt->event_time = now;
    ev->set_pointer(std::move(pt));
    callback_(std::move(ev));
  }
}

#pragma mark - DeviceState

DeviceState::DeviceState(const InputDevice* device, OnEventCallback callback)
    : keyboard(device->id(), callback),
      mouse(device->id(), callback),
      stylus(device->id(), callback),
      touchscreen(device->id(), callback),
      device_(device) {
  if (device_->has_keyboard()) {
    keyboard.OnRegistered();
  }
  if (device_->has_mouse()) {
    mouse.OnRegistered();
  }
  if (device_->has_stylus()) {
    stylus.OnRegistered();
  }
  if (device_->has_touchscreen()) {
    touchscreen.OnRegistered();
  }
}

DeviceState::~DeviceState() {
  if (device_->has_keyboard()) {
    keyboard.OnUnregistered();
  }
  if (device_->has_mouse()) {
    mouse.OnUnregistered();
  }
  if (device_->has_stylus()) {
    stylus.OnUnregistered();
  }
  if (device_->has_touchscreen()) {
    touchscreen.OnUnregistered();
  }
}

}  // namespace input
}  // namespace mozart
