// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct hid_boot_kbd_report {
  uint8_t modifier;
  uint8_t reserved;
  uint8_t usage[6];
} __attribute__((packed)) hid_boot_kbd_report_t;

typedef struct hid_boot_mouse_report {
  uint8_t buttons;
  int8_t rel_x;
  int8_t rel_y;
} __attribute__((packed)) hid_boot_mouse_report_t;

__END_CDECLS
