// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::synthesizer::*,
    anyhow::Error,
    fidl_fuchsia_ui_input::{self, Touch},
    std::time::Duration,
};

pub mod inverse_keymap;
pub mod keymaps;
pub mod usages;

mod legacy_backend;
mod synthesizer;

/// Simulates a media button event.
pub async fn media_button_event_command(
    volume_up: bool,
    volume_down: bool,
    mic_mute: bool,
    reset: bool,
    pause: bool,
) -> Result<(), Error> {
    media_button_event(volume_up, volume_down, mic_mute, reset, pause, &mut RegistryServerConsumer)
}

/// Simulates a key press of specified `usage`.
///
/// `duration` is the time spent between key-press and key-release events.
pub async fn keyboard_event_command(usage: u32, duration: Duration) -> Result<(), Error> {
    keyboard_event(usage, duration, &mut RegistryServerConsumer)
}

/// Simulates `input` being typed on a [qwerty] keyboard by making use of [`InverseKeymap`].
///
/// `duration` is divided equally between all keyboard events.
///
/// [qwerty]: keymaps/constant.QWERTY_MAP.html
pub async fn text_command(input: String, duration: Duration) -> Result<(), Error> {
    text(input, duration, &mut RegistryServerConsumer)
}

/// Simulates `tap_event_count` taps at coordinates `(x, y)` for a touchscreen with horizontal
/// resolution `width` and vertical resolution `height`. `(x, y)` _should_ be specified in absolute
/// coordinations, with `x` normally in the range (0, `width`), `y` normally in the range
/// (0, `height`).
///
/// `duration` is divided equally between touch-down and touch-up event pairs, while the
/// transition between these pairs is immediate.
pub async fn tap_event_command(
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    tap_event(x, y, width, height, tap_event_count, duration, &mut RegistryServerConsumer)
}

/// Simulates `tap_event_count` times to repeat the multi-finger-taps, for touchscreen with
/// horizontal resolution `width` and vertical resolution `height`. Finger positions _should_
/// be specified in absolute coordinations, with `x` values normally in the range (0, `width`),
/// and `y` values normally in the range (0, `height`).
///
/// `duration` is divided equally between multi-touch-down and multi-touch-up
/// pairs, while the transition between these is immediate.
pub async fn multi_finger_tap_event_command(
    fingers: Vec<Touch>,
    width: u32,
    height: u32,
    tap_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    multi_finger_tap_event(
        fingers,
        width,
        height,
        tap_event_count,
        duration,
        &mut RegistryServerConsumer,
    )
}

/// Simulates swipe from coordinates `(x0, y0)` to `(x1, y1)` for a touchscreen with
/// horizontal resolution `width` and vertical resolution `height`, with `move_event_count`
/// touch-move events in between. Positions for move events are linearly interpolated.
///
/// Finger positions _should_ be specified in absolute coordinations, with `x` values normally in the
/// range (0, `width`), and `y` values normally in the range (0, `height`).
///
/// `duration` is the total time from the touch-down event to the touch-up event, inclusive
/// of all move events in between.
pub async fn swipe_command(
    x0: u32,
    y0: u32,
    x1: u32,
    y1: u32,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    swipe(x0, y0, x1, y1, width, height, move_event_count, duration, &mut RegistryServerConsumer)
}

/// Simulates swipe with fingers starting at `start_fingers`, and moving to `end_fingers`,
/// for a touchscreen for a touchscreen with horizontal resolution `width` and vertical resolution
/// `height`. Finger positions _should_ be specified in absolute coordinations, with `x` values
/// normally in the range (0, `width`), and `y` values normally in the range (0, `height`).
///
/// Linearly interpolates `move_event_count` touch-move events between the start positions
/// and end positions, over `duration` time. (`duration` is the total time from the touch-down
/// event to the touch-up event, inclusive of all move events in between.)
///
/// # Requirements
/// * `start_fingers` and `end_fingers` must have the same length
/// * `start_fingers.len()` and `end_finger.len()` must be representable within a `u32`
///
/// # Resolves to
/// * `Ok(())` if the arguments met the requirements above, and the events were successfully
///   injected.
/// * `Err(Error)` otherwise.
///
/// # Corner case handling
/// * `move_event_count` of zero is permitted, and will result in just the DOWN and UP events
///   being generated.
/// * `duration.as_nanos() < move_event_count` is allowed, and will result in all events having
///   the same timestamp.
/// * `width` and `height` are permitted to be zero; such values are left to the interpretation
///   of the system under test.
/// * finger positions _may_ exceed the expected bounds; such values are left to the interpretation
///   of the sytem under test.
pub async fn multi_finger_swipe_command(
    start_fingers: Vec<(u32, u32)>,
    end_fingers: Vec<(u32, u32)>,
    width: u32,
    height: u32,
    move_event_count: usize,
    duration: Duration,
) -> Result<(), Error> {
    multi_finger_swipe(
        start_fingers,
        end_fingers,
        width,
        height,
        move_event_count,
        duration,
        &mut RegistryServerConsumer,
    )
}

#[cfg(test)]
mod tests {
    // This module provides logic-less wrappers over the synthesis module.
    //
    // The wrappers need to bind to FIDL services in this component's environment to do their job,
    // but a component can't modify its own environment. Hence, we can't validate this module
    // with unit tests.
}
