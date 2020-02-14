// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <ddk/debug.h>
#include <fbl/algorithm.h>

#include "input.h"
#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

// clang-format off

const uint8_t kEventCodeMap[] = {
    0,                                  // KEY_RESERVED (0)
    41,                                 // KEY_ESC (1)
    30,                                 // KEY_1 (2)
    31,                                 // KEY_2 (3)
    32,                                 // KEY_3 (4)
    33,                                 // KEY_4 (5)
    34,                                 // KEY_5 (6)
    35,                                 // KEY_6 (7)
    36,                                 // KEY_7 (8)
    37,                                 // KEY_8 (9)
    38,                                 // KEY_9 (10)
    39,                                 // KEY_0 (11)
    45,                                 // KEY_MINUS (12)
    46,                                 // KEY_EQUAL (13)
    42,                                 // KEY_BACKSPACE (14)
    43,                                 // KEY_TAB (15)
    20,                                 // KEY_Q (16)
    26,                                 // KEY_W (17)
    8,                                  // KEY_E (18)
    21,                                 // KEY_R (19)
    23,                                 // KEY_T (20)
    28,                                 // KEY_Y (21)
    24,                                 // KEY_U (22)
    12,                                 // KEY_I (23)
    18,                                 // KEY_O (24)
    19,                                 // KEY_P (25)
    47,                                 // KEY_LEFTBRACE (26)
    48,                                 // KEY_RIGHTBRACE (27)
    40,                                 // KEY_ENTER (28)
    224,                                // KEY_LEFTCTRL (29)
    4,                                  // KEY_A (30)
    22,                                 // KEY_S (31)
    7,                                  // KEY_D (32)
    9,                                  // KEY_F (33)
    10,                                 // KEY_G (34)
    11,                                 // KEY_H (35)
    13,                                 // KEY_J (36)
    14,                                 // KEY_K (37)
    15,                                 // KEY_L (38)
    51,                                 // KEY_SEMICOLON (39)
    52,                                 // KEY_APOSTROPHE (40)
    53,                                 // KEY_GRAVE (41)
    225,                                // KEY_LEFTSHIFT (42)
    49,                                 // KEY_BACKSLASH (43)
    29,                                 // KEY_Z (44)
    27,                                 // KEY_X (45)
    6,                                  // KEY_C (46)
    25,                                 // KEY_V (47)
    5,                                  // KEY_B (48)
    17,                                 // KEY_N (49)
    16,                                 // KEY_M (50)
    54,                                 // KEY_COMMA (51)
    55,                                 // KEY_DOT (52)
    56,                                 // KEY_SLASH (53)
    229,                                // KEY_RIGHTSHIFT (54)
    85,                                 // KEY_KPASTERISK (55)
    226,                                // KEY_LEFTALT (56)
    44,                                 // KEY_SPACE (57)
    57,                                 // KEY_CAPSLOCK (58)
    58,                                 // KEY_F1 (59)
    59,                                 // KEY_F2 (60)
    60,                                 // KEY_F3 (61)
    61,                                 // KEY_F4 (62)
    62,                                 // KEY_F5 (63)
    63,                                 // KEY_F6 (64)
    64,                                 // KEY_F7 (65)
    65,                                 // KEY_F8 (66)
    66,                                 // KEY_F9 (67)
    67,                                 // KEY_F10 (68)
    83,                                 // KEY_NUMLOCK (69)
    71,                                 // KEY_SCROLLLOCK (70)
    95,                                 // KEY_KP7 (71)
    96,                                 // KEY_KP8 (72)
    97,                                 // KEY_KP9 (73)
    86,                                 // KEY_KPMINUS (74)
    92,                                 // KEY_KP4 (75)
    93,                                 // KEY_KP5 (76)
    94,                                 // KEY_KP6 (77)
    87,                                 // KEY_KPPLUS (78)
    89,                                 // KEY_KP1 (79)
    90,                                 // KEY_KP2 (80)
    91,                                 // KEY_KP3 (81)
    98,                                 // KEY_KP0 (82)
    99,                                 // KEY_KPDOT (83)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Unsupported
    228,                                // KEY_RIGHTCTRL (97)
    0, 0,                               // Unsupported
    230,                                // KEY_RIGHTALT (100)
};

static const uint8_t kbd_hid_report_desc[] = {
    0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0, //   Usage Minimum (0xE0)
    0x29, 0xE7, //   Usage Maximum (0xE7)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x01, //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05, //   Report Count (5)
    0x75, 0x01, //   Report Size (1)
    0x05, 0x08, //   Usage Page (LEDs)
    0x19, 0x01, //   Usage Minimum (Num Lock)
    0x29, 0x05, //   Usage Maximum (Kana)
    0x91, 0x02, //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,
                //   Non-volatile)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x03, //   Report Size (3)
    0x91, 0x01, //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,
                //   Non-volatile)
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x65, //   Logical Maximum (101)
    0x05, 0x07, //   Usage Page (Kbrd/Keypad)
    0x19, 0x00, //   Usage Minimum (0x00)
    0x29, 0x65, //   Usage Maximum (0x65)
    0x81, 0x00, //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,       // End Collection
};

// clang-format on

zx_status_t HidKeyboard::GetDescriptor(uint8_t desc_type, void* out_data_buffer, size_t data_size,
                                       size_t* out_data_actual) {
  if (out_data_buffer == nullptr || out_data_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  if (data_size < sizeof(kbd_hid_report_desc)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, kbd_hid_report_desc, sizeof(kbd_hid_report_desc));
  *out_data_actual = sizeof(kbd_hid_report_desc);
  return ZX_OK;
}

void HidKeyboard::AddKeypressToReport(uint16_t event_code) {
  uint8_t hid_code = kEventCodeMap[event_code];
  for (size_t i = 0; i != 6; ++i) {
    if (report_.usage[i] == hid_code) {
      // The key already exists in the report so we ignore it.
      return;
    }
    if (report_.usage[i] == 0) {
      report_.usage[i] = hid_code;
      return;
    }
  }

  // There's no free slot in the report.
  // TODO: Record a rollover status.
}

void HidKeyboard::RemoveKeypressFromReport(uint16_t event_code) {
  uint8_t hid_code = kEventCodeMap[event_code];
  int id = -1;
  for (int i = 0; i != 6; ++i) {
    if (report_.usage[i] == hid_code) {
      id = i;
      break;
    }
  }

  if (id == -1) {
    // They key is not in the report so we ignore it.
    return;
  }

  for (size_t i = id; i != 5; ++i) {
    report_.usage[i] = report_.usage[i + 1];
  }
  report_.usage[5] = 0;
}

void HidKeyboard::ReceiveEvent(virtio_input_event_t* event) {
  if (event->type != VIRTIO_INPUT_EV_KEY) {
    return;
  }
  if (event->code == 0) {
    return;
  }
  if (event->code >= fbl::count_of(kEventCodeMap)) {
    LTRACEF("unknown key\n");
    return;
  }
  if (event->value == VIRTIO_INPUT_EV_KEY_PRESSED) {
    AddKeypressToReport(event->code);
  } else if (event->value == VIRTIO_INPUT_EV_KEY_RELEASED) {
    RemoveKeypressFromReport(event->code);
  }
}

const uint8_t* HidKeyboard::GetReport(size_t* size) {
  *size = sizeof(report_);
  return reinterpret_cast<const uint8_t*>(&report_);
}

}  // namespace virtio
