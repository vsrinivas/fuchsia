// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/device_state.h"

#include "lib/ui/input/fidl/input_event_constants.fidl.h"
#include "lib/ui/input/fidl/usages.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace {
int64_t InputEventTimestampNow() {
  return ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

namespace mozart {

constexpr ftl::TimeDelta kKeyRepeatSlow = ftl::TimeDelta::FromMilliseconds(250);
constexpr ftl::TimeDelta kKeyRepeatFast = ftl::TimeDelta::FromMilliseconds(75);

#pragma mark - KeyboardState

KeyboardState::KeyboardState(DeviceState* device_state)
    : device_state_(device_state),
      keymap_(qwerty_map),
      weak_ptr_factory_(this),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {
  char* keys = getenv("gfxconsole.keymap");
  if (keys && !strcmp(keys, "dvorak")) {
    keymap_ = dvorak_map;
  }
}

void KeyboardState::SendEvent(mozart::KeyboardEvent::Phase phase,
                              uint32_t key,
                              uint64_t modifiers,
                              uint64_t timestamp) {
  auto ev = mozart::InputEvent::New();
  auto kb = mozart::KeyboardEvent::New();
  kb->phase = phase;
  kb->event_time = timestamp;
  kb->device_id = device_state_->device_id();
  kb->hid_usage = key;
  kb->code_point = hid_map_key(
      key, modifiers & (mozart::kModifierShift | mozart::kModifierCapsLock),
      keymap_);
  kb->modifiers = modifiers;
  ev->set_keyboard(std::move(kb));
  device_state_->callback()(std::move(ev));
}

void KeyboardState::Update(mozart::InputReportPtr input_report) {
  FTL_DCHECK(input_report->keyboard);

  uint64_t now = input_report->event_time;
  std::vector<uint32_t> old_keys = keys_;
  keys_.clear();
  repeat_keys_.clear();

  for (uint32_t key : input_report->keyboard->pressed_keys) {
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
        modifiers_ |= mozart::kModifierLeftShift;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= mozart::kModifierRightShift;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= mozart::kModifierLeftControl;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= mozart::kModifierRightControl;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= mozart::kModifierLeftAlt;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= mozart::kModifierRightAlt;
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ |= mozart::kModifierLeftSuper;
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ |= mozart::kModifierRightSuper;
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
    SendEvent(mozart::KeyboardEvent::Phase::RELEASED, key, modifiers_, now);

    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~mozart::kModifierLeftShift);
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~mozart::kModifierRightShift);
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~mozart::kModifierLeftControl);
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~mozart::kModifierRightControl);
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~mozart::kModifierLeftAlt);
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~mozart::kModifierRightAlt);
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ &= (~mozart::kModifierLeftSuper);
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ &= (~mozart::kModifierRightSuper);
        break;
      case HID_USAGE_KEY_CAPSLOCK:
        if (modifiers_ & mozart::kModifierCapsLock) {
          modifiers_ &= (~mozart::kModifierCapsLock);
        } else {
          modifiers_ |= mozart::kModifierCapsLock;
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

void MouseState::OnRegistered() {}

void MouseState::OnUnregistered() {}

void MouseState::SendEvent(float rel_x,
                           float rel_y,
                           int64_t timestamp,
                           mozart::PointerEvent::Phase phase,
                           uint32_t buttons) {
  mozart::InputEventPtr ev = mozart::InputEvent::New();
  mozart::PointerEventPtr pt = mozart::PointerEvent::New();
  pt->event_time = timestamp;
  pt->device_id = device_state_->device_id();
  pt->pointer_id = device_state_->device_id();
  pt->phase = phase;
  pt->buttons = buttons;
  pt->type = mozart::PointerEvent::Type::MOUSE;
  pt->x = rel_x;
  pt->y = rel_y;
  ev->set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void MouseState::Update(mozart::InputReportPtr input_report,
                        mozart::Size display_size) {
  FTL_DCHECK(input_report->mouse);
  uint64_t now = input_report->event_time;
  uint8_t pressed = (input_report->mouse->pressed_buttons ^ buttons_) &
                    input_report->mouse->pressed_buttons;
  uint8_t released =
      (input_report->mouse->pressed_buttons ^ buttons_) & buttons_;
  buttons_ = input_report->mouse->pressed_buttons;

  // TODO(jpoichet) Update once we have an API to capture mouse.
  position_.x =
      std::max(0.0f, std::min(position_.x + input_report->mouse->rel_x,
                              static_cast<float>(display_size.width)));
  position_.y =
      std::max(0.0f, std::min(position_.y + input_report->mouse->rel_y,
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
  pt->device_id = device_state_->device_id();
  pt->pointer_id = 1;
  pt->type = type;
  pt->phase = phase;
  pt->x = x;
  pt->y = y;
  pt->buttons = buttons;

  stylus_ = *pt;

  auto ev = mozart::InputEvent::New();
  ev->set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void StylusState::Update(mozart::InputReportPtr input_report,
                         mozart::Size display_size) {
  FTL_DCHECK(input_report->stylus);

  mozart::StylusDescriptor* descriptor = device_state_->stylus_descriptor();
  FTL_DCHECK(descriptor);

  const bool previous_stylus_down = stylus_down_;
  const bool previous_stylus_in_range = stylus_in_range_;
  stylus_down_ = input_report->stylus->is_in_contact;
  stylus_in_range_ = input_report->stylus->in_range;

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
        inverted_stylus_ = input_report->stylus->is_inverted;
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

  uint64_t now = input_report->event_time;

  if (phase == mozart::PointerEvent::Phase::UP) {
    SendEvent(now, phase,
              inverted_stylus_ ? mozart::PointerEvent::Type::INVERTED_STYLUS
                               : mozart::PointerEvent::Type::STYLUS,
              stylus_.x, stylus_.y, stylus_.buttons);
  } else {
    float x =
        static_cast<float>(display_size.width * (input_report->stylus->x -
                                                 descriptor->x->range->min)) /
        static_cast<float>(descriptor->x->range->max -
                           descriptor->x->range->min);
    float y =
        static_cast<float>(display_size.height * (input_report->stylus->y -
                                                  descriptor->y->range->min)) /
        static_cast<float>(descriptor->y->range->max -
                           descriptor->y->range->min);

    uint32_t buttons = 0;
    if (input_report->stylus->pressed_buttons & kStylusBarrel) {
      buttons |= mozart::kStylusPrimaryButton;
    }
    SendEvent(now, phase,
              inverted_stylus_ ? mozart::PointerEvent::Type::INVERTED_STYLUS
                               : mozart::PointerEvent::Type::STYLUS,
              x, y, buttons);
  }
}

#pragma mark - TouchscreenState

void TouchscreenState::Update(mozart::InputReportPtr input_report,
                              mozart::Size display_size) {
  FTL_DCHECK(input_report->touchscreen);
  mozart::TouchscreenDescriptor* descriptor =
      device_state_->touchscreen_descriptor();
  FTL_DCHECK(descriptor);

  std::vector<mozart::PointerEvent> old_pointers = pointers_;
  pointers_.clear();

  uint64_t now = input_report->event_time;

  for (auto& touch : input_report->touchscreen->touches) {
    auto ev = mozart::InputEvent::New();
    auto pt = mozart::PointerEvent::New();
    pt->event_time = now;
    pt->device_id = device_state_->device_id();
    pt->phase = mozart::PointerEvent::Phase::DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      FTL_DCHECK(touch->finger_id >= 0);
      if (it->pointer_id == static_cast<uint32_t>(touch->finger_id)) {
        pt->phase = mozart::PointerEvent::Phase::MOVE;
        old_pointers.erase(it);
        break;
      }
    }

    pt->pointer_id = touch->finger_id;
    pt->type = mozart::PointerEvent::Type::TOUCH;

    float x = static_cast<float>(display_size.width *
                                 (touch->x - descriptor->x->range->min)) /
              static_cast<float>(descriptor->x->range->max -
                                 descriptor->x->range->min);
    float y = static_cast<float>(display_size.height *
                                 (touch->y - descriptor->y->range->min)) /
              static_cast<float>(descriptor->y->range->max -
                                 descriptor->y->range->min);

    uint32_t width = 2 * touch->width;
    uint32_t height = 2 * touch->height;

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
      device_state_->callback()(std::move(add_ev));
    }

    ev->set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    auto ev = mozart::InputEvent::New();
    auto pt = pointer.Clone();
    pt->phase = mozart::PointerEvent::Phase::UP;
    pt->event_time = now;
    ev->set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));

    ev = mozart::InputEvent::New();
    pt = pointer.Clone();
    pt->phase = mozart::PointerEvent::Phase::REMOVE;
    pt->event_time = now;
    ev->set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }
}

#pragma mark - DeviceState

DeviceState::DeviceState(uint32_t device_id,
                         mozart::DeviceDescriptor* descriptor,
                         OnEventCallback callback)
    : device_id_(device_id),
      descriptor_(descriptor),
      callback_(callback),
      keyboard_(this),
      mouse_(this),
      stylus_(this),
      touchscreen_(this) {}

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
}

void DeviceState::Update(mozart::InputReportPtr input_report,
                         mozart::Size display_size) {
  if (input_report->keyboard && descriptor_->keyboard) {
    keyboard_.Update(std::move(input_report));
  } else if (input_report->mouse && descriptor_->mouse) {
    mouse_.Update(std::move(input_report), display_size);
  } else if (input_report->stylus && descriptor_->stylus) {
    stylus_.Update(std::move(input_report), display_size);
  } else if (input_report->touchscreen && descriptor_->touchscreen) {
    touchscreen_.Update(std::move(input_report), display_size);
  }
}

}  // namespace mozart
