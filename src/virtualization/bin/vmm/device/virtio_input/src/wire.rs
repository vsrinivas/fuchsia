// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Keep all consts and type defs for completeness.
#![allow(dead_code)]

use {
    lazy_static::lazy_static,
    zerocopy::{AsBytes, FromBytes},
};

pub use zerocopy::byteorder::little_endian::{U16 as LE16, U32 as LE32};

//
// 5.8.2 Virtqueues
//
pub const EVENTQ: u16 = 0;
pub const STATUSQ: u16 = 1;

// Event types.
pub const VIRTIO_INPUT_EV_SYN: u16 = 0x00;
pub const VIRTIO_INPUT_EV_KEY: u16 = 0x01;
pub const VIRTIO_INPUT_EV_REL: u16 = 0x02;
pub const VIRTIO_INPUT_EV_ABS: u16 = 0x03;
pub const VIRTIO_INPUT_EV_MSC: u16 = 0x04;
pub const VIRTIO_INPUT_EV_SW: u16 = 0x05;
pub const VIRTIO_INPUT_EV_LED: u16 = 0x11;
pub const VIRTIO_INPUT_EV_SND: u16 = 0x12;
pub const VIRTIO_INPUT_EV_REP: u16 = 0x14;
pub const VIRTIO_INPUT_EV_FF: u16 = 0x15;
pub const VIRTIO_INPUT_EV_PWR: u16 = 0x16;
pub const VIRTIO_INPUT_EV_FF_STATUS: u16 = 0x17;

// To populate 'value' in an EV_KEY event.
pub const VIRTIO_INPUT_EV_KEY_RELEASED: u32 = 0;
pub const VIRTIO_INPUT_EV_KEY_PRESSED: u32 = 1;

// To populate 'code' in an EV_REL event.
pub const VIRTIO_INPUT_EV_REL_X: u16 = 0;
pub const VIRTIO_INPUT_EV_REL_Y: u16 = 1;
pub const VIRTIO_INPUT_EV_REL_Z: u16 = 2;
pub const VIRTIO_INPUT_EV_REL_RX: u16 = 3;
pub const VIRTIO_INPUT_EV_REL_RY: u16 = 4;
pub const VIRTIO_INPUT_EV_REL_RZ: u16 = 5;
pub const VIRTIO_INPUT_EV_REL_HWHEEL: u16 = 6;
pub const VIRTIO_INPUT_EV_REL_DIAL: u16 = 7;
pub const VIRTIO_INPUT_EV_REL_WHEEL: u16 = 8;
pub const VIRTIO_INPUT_EV_REL_MISC: u16 = 9;

// To populate 'code' in an EV_ABS event.
pub const VIRTIO_INPUT_EV_ABS_X: u16 = 0;
pub const VIRTIO_INPUT_EV_ABS_Y: u16 = 1;
pub const VIRTIO_INPUT_EV_ABS_Z: u16 = 2;
pub const VIRTIO_INPUT_EV_ABS_RX: u16 = 3;
pub const VIRTIO_INPUT_EV_ABS_RY: u16 = 4;
pub const VIRTIO_INPUT_EV_ABS_RZ: u16 = 5;
pub const VIRTIO_INPUT_EV_MT_SLOT: u16 = 0x2f;
pub const VIRTIO_INPUT_EV_MT_POSITION_X: u16 = 0x35;
pub const VIRTIO_INPUT_EV_MT_POSITION_Y: u16 = 0x36;
pub const VIRTIO_INPUT_EV_MT_TRACKING_ID: u16 = 0x39;

//
// 5.8.6: Device Operation.
//
#[derive(Debug, Default, Copy, Clone, PartialEq, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioInputEvent {
    pub type_: LE16,
    pub code: LE16,
    pub value: LE32,
}

lazy_static! {
    pub static ref SYNC_EVENT: VirtioInputEvent =
        VirtioInputEvent { type_: LE16::new(VIRTIO_INPUT_EV_SYN), ..Default::default() };
}

//
// virtio-input uses evdev keycodes for key events. The full set of these symbols is found in
// uapi/linux/input-event-codes.h
//
pub const KEY_ESC: u16 = 1;
pub const KEY_1: u16 = 2;
pub const KEY_2: u16 = 3;
pub const KEY_3: u16 = 4;
pub const KEY_4: u16 = 5;
pub const KEY_5: u16 = 6;
pub const KEY_6: u16 = 7;
pub const KEY_7: u16 = 8;
pub const KEY_8: u16 = 9;
pub const KEY_9: u16 = 10;
pub const KEY_0: u16 = 11;
pub const KEY_MINUS: u16 = 12;
pub const KEY_EQUAL: u16 = 13;
pub const KEY_BACKSPACE: u16 = 14;
pub const KEY_TAB: u16 = 15;
pub const KEY_Q: u16 = 16;
pub const KEY_W: u16 = 17;
pub const KEY_E: u16 = 18;
pub const KEY_R: u16 = 19;
pub const KEY_T: u16 = 20;
pub const KEY_Y: u16 = 21;
pub const KEY_U: u16 = 22;
pub const KEY_I: u16 = 23;
pub const KEY_O: u16 = 24;
pub const KEY_P: u16 = 25;
pub const KEY_LEFTBRACE: u16 = 26;
pub const KEY_RIGHTBRACE: u16 = 27;
pub const KEY_ENTER: u16 = 28;
pub const KEY_LEFTCTRL: u16 = 29;
pub const KEY_A: u16 = 30;
pub const KEY_S: u16 = 31;
pub const KEY_D: u16 = 32;
pub const KEY_F: u16 = 33;
pub const KEY_G: u16 = 34;
pub const KEY_H: u16 = 35;
pub const KEY_J: u16 = 36;
pub const KEY_K: u16 = 37;
pub const KEY_L: u16 = 38;
pub const KEY_SEMICOLON: u16 = 39;
pub const KEY_APOSTROPHE: u16 = 40;
pub const KEY_GRAVE: u16 = 41;
pub const KEY_LEFTSHIFT: u16 = 42;
pub const KEY_BACKSLASH: u16 = 43;
pub const KEY_Z: u16 = 44;
pub const KEY_X: u16 = 45;
pub const KEY_C: u16 = 46;
pub const KEY_V: u16 = 47;
pub const KEY_B: u16 = 48;
pub const KEY_N: u16 = 49;
pub const KEY_M: u16 = 50;
pub const KEY_COMMA: u16 = 51;
pub const KEY_DOT: u16 = 52;
pub const KEY_SLASH: u16 = 53;
pub const KEY_RIGHTSHIFT: u16 = 54;
pub const KEY_KPASTERISK: u16 = 55;
pub const KEY_LEFTALT: u16 = 56;
pub const KEY_SPACE: u16 = 57;
pub const KEY_CAPSLOCK: u16 = 58;
pub const KEY_F1: u16 = 59;
pub const KEY_F2: u16 = 60;
pub const KEY_F3: u16 = 61;
pub const KEY_F4: u16 = 62;
pub const KEY_F5: u16 = 63;
pub const KEY_F6: u16 = 64;
pub const KEY_F7: u16 = 65;
pub const KEY_F8: u16 = 66;
pub const KEY_F9: u16 = 67;
pub const KEY_F10: u16 = 68;
pub const KEY_NUMLOCK: u16 = 69;
pub const KEY_SCROLLLOCK: u16 = 70;
pub const KEY_KP7: u16 = 71;
pub const KEY_KP8: u16 = 72;
pub const KEY_KP9: u16 = 73;
pub const KEY_KPMINUS: u16 = 74;
pub const KEY_KP4: u16 = 75;
pub const KEY_KP5: u16 = 76;
pub const KEY_KP6: u16 = 77;
pub const KEY_KPPLUS: u16 = 78;
pub const KEY_KP1: u16 = 79;
pub const KEY_KP2: u16 = 80;
pub const KEY_KP3: u16 = 81;
pub const KEY_KP0: u16 = 82;
pub const KEY_KPDOT: u16 = 83;
pub const KEY_102ND: u16 = 86;
pub const KEY_F11: u16 = 87;
pub const KEY_F12: u16 = 88;
pub const KEY_KPENTER: u16 = 96;
pub const KEY_RIGHTCTRL: u16 = 97;
pub const KEY_KPSLASH: u16 = 98;
pub const KEY_SYSRQ: u16 = 99;
pub const KEY_RIGHTALT: u16 = 100;
pub const KEY_HOME: u16 = 102;
pub const KEY_UP: u16 = 103;
pub const KEY_PAGEUP: u16 = 104;
pub const KEY_LEFT: u16 = 105;
pub const KEY_RIGHT: u16 = 106;
pub const KEY_END: u16 = 107;
pub const KEY_DOWN: u16 = 108;
pub const KEY_PAGEDOWN: u16 = 109;
pub const KEY_INSERT: u16 = 110;
pub const KEY_DELETE: u16 = 111;
pub const KEY_MUTE: u16 = 113;
pub const KEY_VOLUMEDOWN: u16 = 114;
pub const KEY_VOLUMEUP: u16 = 115;
pub const KEY_POWER: u16 = 116;
pub const KEY_KPEQUAL: u16 = 117;
pub const KEY_PAUSE: u16 = 119;
pub const KEY_LEFTMETA: u16 = 125;
pub const KEY_RIGHTMETA: u16 = 126;
pub const KEY_COMPOSE: u16 = 127;
pub const KEY_STOP: u16 = 128;
pub const KEY_AGAIN: u16 = 129;
pub const KEY_PROPS: u16 = 130;
pub const KEY_UNDO: u16 = 131;
pub const KEY_FRONT: u16 = 132;
pub const KEY_COPY: u16 = 133;
pub const KEY_OPEN: u16 = 134;
pub const KEY_PASTE: u16 = 135;
pub const KEY_FIND: u16 = 136;
pub const KEY_CUT: u16 = 137;
pub const KEY_HELP: u16 = 138;
pub const KEY_F13: u16 = 183;
pub const KEY_F14: u16 = 184;
pub const KEY_F15: u16 = 185;
pub const KEY_F16: u16 = 186;
pub const KEY_F17: u16 = 187;
pub const KEY_F18: u16 = 188;
pub const KEY_F19: u16 = 189;
pub const KEY_F20: u16 = 190;
pub const KEY_F21: u16 = 191;
pub const KEY_F22: u16 = 192;
pub const KEY_F23: u16 = 193;
pub const KEY_F24: u16 = 194;
