// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod pointer;

#[cfg(test)]
mod tests;

use {
    crate::pointer::PointerFusionState,
    fidl_fuchsia_ui_pointer as fptr, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self, UnboundedSender},
        stream, Stream, StreamExt,
    },
};

#[derive(Clone, Default, Debug, PartialEq)]
pub enum DeviceKind {
    #[default]
    Touch,
    Mouse,
    Stylus,
    InvertedStylus,
    Trackpad,
}

#[derive(Clone, Default, Debug, PartialEq)]
pub enum Phase {
    #[default]
    Cancel,
    Add,
    Remove,
    Hover,
    Down,
    Move,
    Up,
}

#[derive(Clone, Default, Debug, PartialEq)]
pub enum SignalKind {
    #[default]
    None,
    Scroll,
}

pub const POINTER_BUTTON_1: i64 = 1 << 0;
pub const POINTER_BUTTON_2: i64 = 1 << 1;
pub const POINTER_BUTTON_3: i64 = 1 << 2;
pub const POINTER_BUTTON_4: i64 = 1 << 3;
pub const POINTER_BUTTON_5: i64 = 1 << 4;

/// Information about the state of a pointer.
#[derive(Clone, Default, Debug)]
pub struct PointerEvent {
    /// The monotonically increasing identifier that is present only on 'Down' events and
    /// is 0 otherwise.
    pub id: i64,
    /// The kind of input device.
    pub kind: DeviceKind,
    /// The timestamp when the event originated. This is monotonically increasing for the same
    /// [DeviceKind]. Timestamp for synthesized events is same as event synthesized from.
    pub timestamp: zx::Time,
    /// The current [Phase] of pointer event.
    pub phase: Phase,
    /// The unique device identifier.
    pub device_id: u32,
    /// The identifier for the pointer that issued this event when [DeviceKind] is [Touch].
    pub pointer_id: Option<u32>,
    /// The x position of the device, in the viewport's coordinate system, as reported by the raw
    /// device event.
    pub physical_x: f32,
    /// The y position of the device, in the viewport's coordinate system, as reported by the raw
    /// device event.
    pub physical_y: f32,
    /// The relative change in x position of the device from previous event in sequence.
    pub physical_delta_x: f32,
    /// The relative change in y position of the device from previous event in sequence.
    pub physical_delta_y: f32,
    /// The buttons pressed on the device represented as bitflags.
    pub buttons: i64,
    /// The event [SignalKind] for scroll events.
    pub signal_kind: SignalKind,
    /// The amount of scroll in x direction, in physical pixels.
    pub scroll_delta_y: f64,
    /// The amount of scroll in y direction, in physical pixels.
    pub scroll_delta_x: f64,
    /// Set if this [PointerEvent] was synthesized for maintaining legal input sequence.
    pub synthesized: bool,
}

#[derive(Debug)]
pub enum InputEvent {
    MouseEvent(fptr::MouseEvent),
    TouchEvent(fptr::TouchEvent),
}

/// Provides a stream of [PointerEvent] fused from [InputEvent::MouseEvent] and
/// [InputEvent::TouchEvent].
///
/// * `pixel_ratio` -  The device pixel ratio used to convert from logical to physical coordinates.
///
/// Returns a tuple of [UnboundedSender] to send [InputEvent]s to and a [Stream] to read fused
/// [PointerEvent]s from.
pub fn pointer_fusion(
    pixel_ratio: f32,
) -> (UnboundedSender<InputEvent>, impl Stream<Item = PointerEvent>) {
    let mut state = PointerFusionState::new(pixel_ratio);
    let (input_sender, receiver) = mpsc::unbounded::<InputEvent>();
    let pointer_stream = receiver.map(move |input| stream::iter(state.fuse_input(input))).flatten();

    (input_sender, pointer_stream)
}
