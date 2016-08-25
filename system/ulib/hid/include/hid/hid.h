// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

typedef struct {
    uint32_t keymask[8];
} hid_keys_t;

typedef struct keychar {
    uint8_t c;
    uint8_t shift_c;
} keychar_t;

extern keychar_t qwerty_map[];
extern keychar_t dvorak_map[];

void hid_kbd_parse_report(uint8_t buf[8], hid_keys_t* keys);
void hid_kbd_pressed_keys(const hid_keys_t* prev, const hid_keys_t* cur, hid_keys_t* pressed);
void hid_kbd_released_keys(const hid_keys_t* prev, const hid_keys_t* cur, hid_keys_t* released);
uint8_t hid_kbd_next_key(hid_keys_t* keys);
uint8_t hid_map_key(uint32_t usage, bool shift, keychar_t* keymap);

// iterates over keys in in the hid_keys_t structure.
// keys should be hid_keys_t*, keycode should be uint8_t
#define hid_for_every_key(keys, keycode) \
    for (keycode = hid_kbd_next_key(keys); keycode; keycode = hid_kbd_next_key(keys))
__END_CDECLS
