// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Keep all consts and type defs for completeness.
#![allow(dead_code)]

use zerocopy::{AsBytes, FromBytes, LittleEndian, U16, U32};

pub type LE16 = U16<LittleEndian>;
pub type LE32 = U32<LittleEndian>;

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
#[derive(Debug, Default, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioInputEvent {
    pub type_: LE16,
    pub code: LE16,
    pub value: LE32,
}
