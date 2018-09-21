// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_input.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <iomanip>

#include <lib/fxl/logging.h>
#include <lib/ui/input/cpp/formatting.h>

#include "garnet/lib/machina/bits.h"

namespace machina {

static constexpr char kDeviceName[] = "machina-input";
static_assert(sizeof(kDeviceName) - 1 < sizeof(virtio_input_config_t::u),
              "Device name is too long");

static constexpr char kDeviceSerial[] = "serial-number";
static_assert(sizeof(kDeviceSerial) - 1 < sizeof(virtio_input_config_t::u),
              "Device serial is too long");

// HID usage -> evdev keycode.
static constexpr uint8_t kKeyMap[] = {
    0,    // Reserved
    0,    // Keyboard ErrorRollOver
    0,    // Keyboard POSTFail
    0,    // Keyboard ErrorUndefined
    30,   // A
    48,   // B
    46,   // C
    32,   // D
    18,   // E
    33,   // F
    34,   // G
    35,   // H
    23,   // I
    36,   // J
    37,   // K
    38,   // L
    50,   // M
    49,   // N
    24,   // O
    25,   // P
    16,   // Q
    19,   // R
    31,   // S
    20,   // T
    22,   // U
    47,   // V
    17,   // W
    45,   // X
    21,   // Y
    44,   // Z
    2,    // 1
    3,    // 2
    4,    // 3
    5,    // 4
    6,    // 5
    7,    // 6
    8,    // 7
    9,    // 8
    10,   // 9
    11,   // 0
    28,   // Enter
    1,    // Esc
    14,   // Backspace
    15,   // Tab
    57,   // Space
    12,   // -
    13,   // =
    26,   // [
    27,   // ]
    43,   // Backslash
    43,   // Non-US # and ~
    39,   // ;
    40,   // '
    41,   // `
    51,   // ,
    52,   // .
    53,   // /
    58,   // Caps Lock
    59,   // F1
    60,   // F2
    61,   // F3
    62,   // F4
    63,   // F5
    64,   // F6
    65,   // F7
    66,   // F8
    67,   // F9
    68,   // F10
    87,   // F11
    88,   // F12
    99,   // Print Screen
    70,   // ScrollLock
    119,  // Pause
    110,  // Insert
    102,  // Home
    104,  // PageUp
    111,  // Delete Forward
    107,  // End
    109,  // PageDown
    106,  // Right
    105,  // Left
    108,  // Down
    103,  // Up
    69,   // NumLock
    98,   // Keypad /
    55,   // Keypad *
    74,   // Keypad -
    78,   // Keypad +
    96,   // Keypad Enter
    79,   // Keypad 1
    80,   // Keypad 2
    81,   // Keypad 3
    75,   // Keypad 4
    76,   // Keypad 5
    77,   // Keypad 6
    71,   // Keypad 7
    72,   // Keypad 8
    73,   // Keypad 9
    82,   // Keypad 0
    83,   // Keypad .
    86,   // Non-US \ and |
    127,  // Keyboard Application
    116,  // Power
    117,  // Keypad =
    183,  // F13
    184,  // F14
    185,  // F15
    186,  // F16
    187,  // F17
    188,  // F18
    189,  // F19
    190,  // F20
    191,  // F21
    192,  // F22
    193,  // F23
    194,  // F24
    134,  // Execute
    138,  // Help
    130,  // Menu
    132,  // Select
    128,  // Stop
    129,  // Again
    131,  // Undo
    137,  // Cut
    133,  // Copy
    135,  // Paste
    136,  // Find
    113,  // Mute
    115,  // Volume Up
    114,  // Volume Down

    // Skip some more esoteric keys that have no obvious evdev counterparts.
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    29,   // Left Ctrl
    42,   // Left Shift
    56,   // Left Alt
    125,  // Left Meta
    97,   // Right Ctrl
    54,   // Right Shift
    100,  // Right Alt
    126,  // Right Meta
};

// Make sure to report only these event codes from keyboard.
// Reporting other keycodes may cause guest OS to recognize keyboard as
// touchpad, stylus or joystick.
constexpr uint32_t kATKeyboardFirstCode = 0;
constexpr uint32_t kATKeyboardLastCode = 255;
constexpr uint32_t kMediaKeyboardFirstCode = 0x160;
constexpr uint32_t kMediaKeyboardLastCode = 0x2bf;
constexpr uint32_t kButtonTouchCode = 0x14a;

static_assert(kATKeyboardFirstCode % 8 == 0,
              "First scan code must be byte aligned.");
static_assert((kATKeyboardLastCode + 1 - kATKeyboardFirstCode) % 8 == 0,
              "Scan code range must be byte aligned.");
static_assert(kMediaKeyboardFirstCode % 8 == 0,
              "First scan code must be byte aligned.");
static_assert((kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) % 8 == 0,
              "Scan code range must be byte aligned.");
static_assert((kATKeyboardLastCode + 7) / 8 <
                  sizeof(virtio_input_config_t().u.bitmap),
              "Last scan code cannot exceed allowed range.");
static_assert((kMediaKeyboardLastCode + 7) / 8 <
                  sizeof(virtio_input_config_t().u.bitmap),
              "Last scan code cannot exceed allowed range.");

// TODO(MAC-164): Use real touch/pen digitizer resolutions.
constexpr uint32_t kAbsMaxX = UINT16_MAX;
constexpr uint32_t kAbsMaxY = UINT16_MAX;

// Retrieves the position of a pointer event and translates it into the
// coordinate space expected in the VIRTIO_INPUT_EV_[REL/ABS] event payload.
// The incoming event coordinates are expected to be in the floating-point 0..1
// range, which are mapped to the nearest integer in 0..kAbsMax[X/Y].
static void GetPointerCoordinate(const fuchsia::ui::input::PointerEvent& event,
                                 uint32_t* x_out, uint32_t* y_out) {
  float x_in = event.x;
  float y_in = event.y;
  if (x_in < 0.0f || x_in > 1.0f) {
    FXL_LOG(WARNING) << "PointerEvent::x out of range (" << std::fixed
                     << std::setprecision(7) << x_in << ")";
    x_in = std::min(1.0f, std::max(0.0f, x_in));
  }
  if (y_in < 0.0f || y_in > 1.0f) {
    FXL_LOG(WARNING) << "PointerEvent::y out of range (" << std::fixed
                     << std::setprecision(7) << y_in << ")";
    y_in = std::min(1.0f, std::max(0.0f, y_in));
  }
  *x_out = static_cast<uint32_t>(x_in * kAbsMaxX + 0.5f);
  *y_out = static_cast<uint32_t>(y_in * kAbsMaxY + 0.5f);
}

VirtioInput::VirtioInput(InputEventQueue* event_queue, const PhysMem& phys_mem)
    : VirtioInprocessDevice(phys_mem, 0 /* device_features */,
                            fit::bind_member(this, &VirtioInput::UpdateConfig)),
      event_queue_(event_queue) {}

static void SetConfigBit(uint32_t event_code, virtio_input_config_t* config) {
  config->u.bitmap[event_code / 8] |= 1u << (event_code % 8);
}

static void configure_ev_bits(virtio_input_config_t* config) {
  // VIRTIO_INPUT_CFG_EV_BITS: subsel specifies the event type (EV_*).
  // If size is non-zero the event type is supported and a bitmap the of
  // supported event codes is returned in u.bitmap.
  memset(&config->u, 0, sizeof(config->u));
  switch (config->subsel) {
    case VIRTIO_INPUT_EV_KEY:
      memset(&config->u.bitmap[kATKeyboardFirstCode / 8], 0xff,
             (kATKeyboardLastCode + 1 - kATKeyboardFirstCode) / 8);
      memset(&config->u.bitmap[kMediaKeyboardFirstCode / 8], 0xff,
             (kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) / 8);
      SetConfigBit(kButtonTouchCode, config);
      config->size = sizeof(config->u);
      break;
    case VIRTIO_INPUT_EV_ABS:
      SetConfigBit(VIRTIO_INPUT_EV_ABS_X, config);
      SetConfigBit(VIRTIO_INPUT_EV_ABS_Y, config);
      config->size = 1;
      break;
    default:
      config->size = 0;
      break;
  }
}

static void configure_abs_info(virtio_input_config_t* config) {
  memset(&config->u, 0, sizeof(config->u));
  switch (config->subsel) {
    case VIRTIO_INPUT_EV_ABS_X:
      config->u.abs.min = 0;
      config->u.abs.max = kAbsMaxX;
      config->size = sizeof(config->u.abs);
      break;
    case VIRTIO_INPUT_EV_ABS_Y:
      config->u.abs.min = 0;
      config->u.abs.max = kAbsMaxY;
      config->size = sizeof(config->u.abs);
      break;
    default:
      config->size = 0;
      break;
  }
}

zx_status_t VirtioInput::UpdateConfig(uint64_t addr, const IoValue& value) {
  if (addr >= 2) {
    return ZX_OK;
  }

  //  A write to select or subselect modifies the contents of the config.u
  //  field.
  std::lock_guard<std::mutex> lock(device_config_.mutex);
  switch (config_.select) {
    case VIRTIO_INPUT_CFG_EV_BITS:
      configure_ev_bits(&config_);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ABS_INFO:
      configure_abs_info(&config_);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ID_NAME:
      config_.size = sizeof(kDeviceName) - 1;
      memcpy(&config_.u, kDeviceName, config_.size);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_ID_SERIAL:
      config_.size = sizeof(kDeviceSerial) - 1;
      memcpy(&config_.u, kDeviceSerial, config_.size);
      return ZX_OK;
    case VIRTIO_INPUT_CFG_UNSET:
    case VIRTIO_INPUT_CFG_ID_DEVIDS:
    case VIRTIO_INPUT_CFG_PROP_BITS:
      memset(&config_.u, 0, sizeof(config_.u));
      config_.size = 0;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unsupported select value " << config_.select;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t VirtioInput::SendKeyEvent(uint16_t code,
                                      virtio_input_key_event_value value) {
  virtio_input_event_t event{};
  event.type = VIRTIO_INPUT_EV_KEY;
  event.code = code;
  event.value = value;
  return SendVirtioEvent(event, VirtioQueue::SET_QUEUE);
}

zx_status_t VirtioInput::SendRepEvent(uint16_t code,
                                      virtio_input_key_event_value value) {
  virtio_input_event_t event{};
  event.type = VIRTIO_INPUT_EV_REP;
  event.code = code;
  event.value = value;
  return SendVirtioEvent(event, VirtioQueue::SET_QUEUE);
}

zx_status_t VirtioInput::SendAbsEvent(virtio_input_abs_event_code code,
                                      uint32_t value) {
  virtio_input_event_t event{};
  event.type = VIRTIO_INPUT_EV_ABS;
  event.code = code;
  event.value = value;
  return SendVirtioEvent(event, VirtioQueue::SET_QUEUE);
}

zx_status_t VirtioInput::SendSynEvent() {
  virtio_input_event_t event{};
  event.type = VIRTIO_INPUT_EV_SYN;
  return SendVirtioEvent(event,
                         VirtioQueue::SET_QUEUE | VirtioQueue::TRY_INTERRUPT);
}

zx_status_t VirtioInput::SendVirtioEvent(const virtio_input_event_t& event,
                                         uint8_t actions) {
  uint16_t head;
  event_queue()->Wait(&head);

  VirtioDescriptor desc;
  zx_status_t status = event_queue()->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    return status;
  }

  auto event_out = static_cast<virtio_input_event_t*>(desc.addr);
  memcpy(event_out, &event, sizeof(event));

  return event_queue()->Return(head, sizeof(event), actions);
}

zx_status_t VirtioInput::Start() {
  thrd_t thread;
  auto poll_thread = [](void* arg) {
    return reinterpret_cast<VirtioInput*>(arg)->PollEventQueue();
  };
  int ret = thrd_create_with_name(&thread, poll_thread, this, "virtio-input");
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t VirtioInput::PollEventQueue() {
  while (true) {
    auto event = event_queue_->Wait();
    zx_status_t status = HandleEvent(event);
    if (status == ZX_ERR_NOT_SUPPORTED) {
      FXL_LOG(INFO) << "Unsupported event received:\n" << event;
    } else if (status != ZX_OK) {
      return status;
    }
  }
}

zx_status_t VirtioInput::HandleKeyEvent(
    const fuchsia::ui::input::InputEvent& event) {
  uint32_t hid_usage = event.keyboard().hid_usage;
  if (hid_usage >= sizeof(kKeyMap) / sizeof(kKeyMap[0])) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint8_t code = kKeyMap[hid_usage];
  zx_status_t status;
  switch (event.keyboard().phase) {
    case fuchsia::ui::input::KeyboardEventPhase::PRESSED:
      status = SendKeyEvent(code, VIRTIO_INPUT_EV_KEY_PRESSED);
      if (status != ZX_OK) {
        return status;
      }
      return SendSynEvent();
    case fuchsia::ui::input::KeyboardEventPhase::REPEAT:
      status = SendRepEvent(code, VIRTIO_INPUT_EV_KEY_PRESSED);
      if (status != ZX_OK) {
        return status;
      }
      return SendSynEvent();
    case fuchsia::ui::input::KeyboardEventPhase::RELEASED:
    case fuchsia::ui::input::KeyboardEventPhase::CANCELLED:
      status = SendKeyEvent(code, VIRTIO_INPUT_EV_KEY_RELEASED);
      if (status != ZX_OK) {
        return status;
      }
      return SendSynEvent();
  }
}

zx_status_t VirtioInput::HandleAbsEvent(
    const fuchsia::ui::input::InputEvent& event) {
  auto phase = event.pointer().phase;
  if (phase != fuchsia::ui::input::PointerEventPhase::DOWN &&
      phase != fuchsia::ui::input::PointerEventPhase::MOVE &&
      phase != fuchsia::ui::input::PointerEventPhase::UP) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint32_t pos_x = 0;
  uint32_t pos_y = 0;
  GetPointerCoordinate(event.pointer(), &pos_x, &pos_y);
  // Send position events, then the key event.
  zx_status_t status = SendAbsEvent(VIRTIO_INPUT_EV_ABS_X, pos_x);
  if (status != ZX_OK) {
    return status;
  }
  status = SendAbsEvent(VIRTIO_INPUT_EV_ABS_Y, pos_y);
  if (status != ZX_OK) {
    return status;
  }
  if (phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
    status = SendKeyEvent(kButtonTouchCode, VIRTIO_INPUT_EV_KEY_PRESSED);
    if (status != ZX_OK) {
      return status;
    }
  } else if (phase == fuchsia::ui::input::PointerEventPhase::UP) {
    status = SendKeyEvent(kButtonTouchCode, VIRTIO_INPUT_EV_KEY_RELEASED);
    if (status != ZX_OK) {
      return status;
    }
  }
  return SendSynEvent();
}

zx_status_t VirtioInput::HandleEvent(
    const fuchsia::ui::input::InputEvent& event) {
  switch (event.Which()) {
    case fuchsia::ui::input::InputEvent::Tag::kKeyboard:
      return HandleKeyEvent(event);
    case fuchsia::ui::input::InputEvent::Tag::kPointer:
      return HandleAbsEvent(event);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace machina
