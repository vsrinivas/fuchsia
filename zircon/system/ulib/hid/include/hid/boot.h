// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_BOOT_H_
#define HID_BOOT_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define HID_KBD_MODIFIER_LEFT_CTL (1 << 0)
#define HID_KBD_MODIFIER_LEFT_SHIFT (1 << 1)
#define HID_KBD_MODIFIER_LEFT_ALT (1 << 2)
#define HID_KBD_MODIFIER_LEFT_GUI (1 << 3)
#define HID_KBD_MODIFIER_RIGHT_CTL (1 << 4)
#define HID_KBD_MODIFIER_RIGHT_SHIFT (1 << 5)
#define HID_KBD_MODIFIER_RIGHT_ALT (1 << 6)
#define HID_KBD_MODIFIER_RIGHT_GUI (1 << 7)

typedef struct hid_boot_kbd_report {
  uint8_t modifier;
  uint8_t reserved;
  uint8_t usage[6];
} __attribute__((packed)) hid_boot_kbd_report_t;

const uint8_t* get_boot_kbd_report_desc(size_t* size);

typedef struct hid_boot_mouse_report {
  uint8_t buttons;
  int8_t rel_x;
  int8_t rel_y;
} __attribute__((packed)) hid_boot_mouse_report_t;

const uint8_t* get_boot_mouse_report_desc(size_t* size);

__END_CDECLS

#endif  // HID_BOOT_H_
