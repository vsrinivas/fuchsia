// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_input.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "garnet/lib/machina/bits.h"
#include "lib/fxl/logging.h"

namespace machina {

// HID usage -> evdev keycode.
const uint8_t kKeyMap[] = {
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
constexpr uint32_t kButtonMousePrimaryCode = 0x110;
constexpr uint32_t kButtonMouseSecondaryCode = 0x111;
constexpr uint32_t kButtonMouseTertiaryCode = 0x112;

VirtioInput::VirtioInput(InputEventQueue* event_queue, const PhysMem& phys_mem,
                         const char* device_name, const char* device_serial)
    : VirtioDeviceBase(phys_mem),
      device_name_(device_name),
      device_serial_(device_serial),
      event_queue_(event_queue) {}

static void SetConfigBit(uint32_t event_code, virtio_input_config_t* config) {
  config->u.bitmap[event_code / 8] |= 1u << (event_code % 8);
}

zx_status_t VirtioInput::WriteConfig(uint64_t addr, const IoValue& value) {
  zx_status_t status = VirtioDeviceBase::WriteConfig(addr, value);
  if (status != ZX_OK) {
    return status;
  }
  if (addr >= 2) {
    return ZX_OK;
  }

  //  A write to select or subselect modifies the contents of the config.u
  //  field.
  fbl::AutoLock lock(&config_mutex_);
  switch (config_.select) {
    case VIRTIO_INPUT_CFG_ID_NAME: {
      size_t len = strlen(device_name_);
      memcpy(&config_.u, device_name_, len);
      config_.size = static_cast<uint8_t>(
          len > sizeof(config_.u) ? sizeof(config_.u) : len);
      return ZX_OK;
    }
    case VIRTIO_INPUT_CFG_ID_SERIAL: {
      size_t len = strlen(device_serial_);
      config_.size = static_cast<uint8_t>(
          len > sizeof(config_.u) ? sizeof(config_.u) : len);
      memcpy(&config_.u, device_serial_, len);
      return ZX_OK;
    }
    case VIRTIO_INPUT_CFG_EV_BITS:
    case VIRTIO_INPUT_CFG_UNSET:
    case VIRTIO_INPUT_CFG_ID_DEVIDS:
    case VIRTIO_INPUT_CFG_PROP_BITS:
    case VIRTIO_INPUT_CFG_ABS_INFO:
      memset(&config_.u, 0, sizeof(config_.u));
      config_.size = 0;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unsupported select value " << config_.select;
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t VirtioKeyboard::WriteConfig(uint64_t addr, const IoValue& value) {
  zx_status_t status = VirtioInput::WriteConfig(addr, value);
  if (status != ZX_OK) {
    return status;
  }
  fbl::AutoLock lock(&config_mutex_);
  if (config_.select != VIRTIO_INPUT_CFG_EV_BITS) {
    return ZX_OK;
  }

  // VIRTIO_INPUT_CFG_EV_BITS: subsel specifies the event type (EV_*).
  // If size is non-zero the event type is supported and a bitmap the of
  // supported event codes is returned in u.bitmap.
  if (config_.subsel == VIRTIO_INPUT_EV_KEY) {
    static_assert(kATKeyboardFirstCode % 8 == 0,
                  "First scan code must be byte aligned.");
    static_assert((kATKeyboardLastCode + 1 - kATKeyboardFirstCode) % 8 == 0,
                  "Scan code range must be byte aligned.");
    static_assert(kMediaKeyboardFirstCode % 8 == 0,
                  "First scan code must be byte aligned.");
    static_assert(
        (kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) % 8 == 0,
        "Scan code range must be byte aligned.");
    static_assert((kATKeyboardLastCode + 7) / 8 <
                      sizeof(virtio_input_config_t().u.bitmap),
                  "Last scan code cannot exceed allowed range.");
    static_assert((kMediaKeyboardLastCode + 7) / 8 <
                      sizeof(virtio_input_config_t().u.bitmap),
                  "Last scan code cannot exceed allowed range.");

    memset(&config_.u, 0, sizeof(config_.u));
    memset(&config_.u.bitmap[kATKeyboardFirstCode / 8], 0xff,
           (kATKeyboardLastCode + 1 - kATKeyboardFirstCode) / 8);
    memset(&config_.u.bitmap[kMediaKeyboardFirstCode / 8], 0xff,
           (kMediaKeyboardLastCode + 1 - kMediaKeyboardFirstCode) / 8);
    config_.size = sizeof(config_.u);
  }

  return ZX_OK;
}

zx_status_t VirtioRelativePointer::WriteConfig(uint64_t addr,
                                               const IoValue& value) {
  zx_status_t status = VirtioInput::WriteConfig(addr, value);
  if (status != ZX_OK) {
    return status;
  }
  fbl::AutoLock lock(&config_mutex_);
  if (config_.select != VIRTIO_INPUT_CFG_EV_BITS) {
    return ZX_OK;
  }

  // VIRTIO_INPUT_CFG_EV_BITS: subsel specifies the event type (EV_*).
  // If size is non-zero the event type is supported and a bitmap the of
  // supported event codes is returned in u.bitmap.
  if (config_.subsel == VIRTIO_INPUT_EV_KEY) {
    SetConfigBit(kButtonMousePrimaryCode, &config_);
    SetConfigBit(kButtonMouseSecondaryCode, &config_);
    SetConfigBit(kButtonMouseTertiaryCode, &config_);
    config_.size = sizeof(config_.u);
  } else if (config_.subsel == VIRTIO_INPUT_EV_REL) {
    memset(&config_.u, 0, sizeof(config_.u));
    SetConfigBit(VIRTIO_INPUT_EV_REL_X, &config_);
    SetConfigBit(VIRTIO_INPUT_EV_REL_Y, &config_);
    config_.size = 1;
  }

  return ZX_OK;
}

zx_status_t VirtioAbsolutePointer::WriteConfig(uint64_t addr,
                                               const IoValue& value) {
  zx_status_t status = VirtioInput::WriteConfig(addr, value);
  if (status != ZX_OK) {
    return status;
  }
  fbl::AutoLock lock(&config_mutex_);
  if (config_.select == VIRTIO_INPUT_CFG_EV_BITS) {
    if (config_.subsel == VIRTIO_INPUT_EV_KEY) {
      SetConfigBit(kButtonMousePrimaryCode, &config_);
      SetConfigBit(kButtonMouseSecondaryCode, &config_);
      SetConfigBit(kButtonMouseTertiaryCode, &config_);
      config_.size = sizeof(config_.u);
    } else if (config_.subsel == VIRTIO_INPUT_EV_ABS) {
      memset(&config_.u, 0, sizeof(config_.u));
      SetConfigBit(VIRTIO_INPUT_EV_ABS_X, &config_);
      SetConfigBit(VIRTIO_INPUT_EV_ABS_Y, &config_);
      config_.size = 1;
    }
  } else if (config_.select == VIRTIO_INPUT_CFG_ABS_INFO) {
    if (config_.subsel == VIRTIO_INPUT_EV_ABS_X) {
      config_.u.abs.min = 0;
      config_.u.abs.max = max_width_;
      config_.size = sizeof(config_.u.abs);
    } else if (config_.subsel == VIRTIO_INPUT_EV_ABS_Y) {
      config_.u.abs.min = 0;
      config_.u.abs.max = max_height_;
      config_.size = sizeof(config_.u.abs);
    }
  }
  return ZX_OK;
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
    InputEvent event = event_queue_->Wait();
    zx_status_t status = OnInputEvent(event);
    if (status != ZX_OK) {
      return status;
    }
  }
}

zx_status_t VirtioInput::OnInputEvent(const InputEvent& event) {
  switch (event.type) {
    case InputEventType::BARRIER:
      return OnBarrierEvent();
    case InputEventType::KEYBOARD:
      return OnKeyEvent(event.key);
    case InputEventType::POINTER:
      return OnPointerEvent(event.pointer);
    case InputEventType::BUTTON:
      return OnButtonEvent(event.button);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t VirtioInput::OnKeyEvent(const KeyEvent& key_event) {
  if (key_event.hid_usage >= (sizeof(kKeyMap) / sizeof(kKeyMap[0]))) {
    return ZX_OK;
  }

  virtio_input_event_t virtio_event;
  virtio_event.type = VIRTIO_INPUT_EV_KEY;
  virtio_event.code = kKeyMap[key_event.hid_usage];
  virtio_event.value = key_event.state == KeyState::PRESSED
                           ? VIRTIO_INPUT_EV_KEY_PRESSED
                           : VIRTIO_INPUT_EV_KEY_RELEASED;
  return SendVirtioEvent(virtio_event);
}

zx_status_t VirtioInput::OnPointerEvent(const PointerEvent& pointer_event) {
  virtio_input_event_t x_event, y_event;
  switch (pointer_event.type) {
    case PointerType::RELATIVE:
      x_event.type = VIRTIO_INPUT_EV_REL;
      x_event.code = VIRTIO_INPUT_EV_REL_X;
      y_event.type = VIRTIO_INPUT_EV_REL;
      y_event.code = VIRTIO_INPUT_EV_REL_Y;
      break;
    case PointerType::ABSOLUTE:
      x_event.type = VIRTIO_INPUT_EV_ABS;
      x_event.code = VIRTIO_INPUT_EV_ABS_X;
      y_event.type = VIRTIO_INPUT_EV_ABS;
      y_event.code = VIRTIO_INPUT_EV_ABS_Y;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  x_event.value = static_cast<int32_t>(pointer_event.x);
  y_event.value = static_cast<int32_t>(pointer_event.y);
  zx_status_t status = SendVirtioEvent(x_event);
  if (status != ZX_OK) {
    return status;
  }
  return SendVirtioEvent(y_event);
}

zx_status_t VirtioInput::OnButtonEvent(const ButtonEvent& button_event) {
  virtio_input_event_t virtio_event;
  switch (button_event.button) {
    case machina::Button::BTN_MOUSE_PRIMARY:
      virtio_event.code = kButtonMousePrimaryCode;
      break;
    case machina::Button::BTN_MOUSE_SECONDARY:
      virtio_event.code = kButtonMouseSecondaryCode;
      break;
    case machina::Button::BTN_MOUSE_TERTIARY:
      virtio_event.code = kButtonMouseSecondaryCode;
      break;
    default:
      return ZX_OK;
  }
  virtio_event.type = VIRTIO_INPUT_EV_KEY;
  virtio_event.value = button_event.state == KeyState::PRESSED
                           ? VIRTIO_INPUT_EV_KEY_PRESSED
                           : VIRTIO_INPUT_EV_KEY_RELEASED;
  return SendVirtioEvent(virtio_event);
}

zx_status_t VirtioInput::OnBarrierEvent() {
  virtio_input_event_t virtio_event = {};
  virtio_event.type = VIRTIO_INPUT_EV_SYN;
  zx_status_t status = SendVirtioEvent(virtio_event);
  if (status != ZX_OK) {
    return status;
  }
  return NotifyGuest();
}

zx_status_t VirtioInput::SendVirtioEvent(const virtio_input_event_t& event) {
  uint16_t head;
  event_queue()->Wait(&head);

  virtio_desc_t desc;
  zx_status_t status = event_queue()->ReadDesc(head, &desc);
  if (status != ZX_OK) {
    return status;
  }

  auto event_out = static_cast<virtio_input_event_t*>(desc.addr);
  memcpy(event_out, &event, sizeof(event));

  // To be less chatty, we'll only send interrupts on barrier events.
  event_queue()->Return(head, sizeof(event),
                        VirtioQueue::InterruptAction::SET_FLAGS);
  return ZX_OK;
}

}  // namespace machina
