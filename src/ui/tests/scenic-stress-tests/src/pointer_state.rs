// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::session::{DISPLAY_HEIGHT, DISPLAY_WIDTH},
    fidl_fuchsia_ui_input as finput, fidl_fuchsia_ui_scenic as fscenic,
    fuchsia_zircon::Time,
    rand::rngs::SmallRng,
    rand::Rng,
};

/// State tracking for touch events. Touch events follow a cycle of states:
/// Add -> Down -> Move -> Up -> Remove -> Add -> ...
///
/// This struct generates these states repeatedly, at random coordinates on the display.
/// Note that when a touch event hits an object on the scene graph, a pointer event is sent to the
/// corresponding session's listener.
pub struct PointerState {
    phase: finput::PointerEventPhase,
    compositor_id: u32,
    device_id: u32,
    pointer_id: u32,
}

impl PointerState {
    pub fn new(compositor_id: u32) -> Self {
        Self {
            phase: finput::PointerEventPhase::Remove,
            compositor_id,
            device_id: 1,
            pointer_id: 1,
        }
    }

    /// Transition from one phase to the next.
    pub fn next_phase(&mut self) {
        self.phase = match self.phase {
            finput::PointerEventPhase::Add => finput::PointerEventPhase::Down,
            finput::PointerEventPhase::Down => finput::PointerEventPhase::Move,
            finput::PointerEventPhase::Move => finput::PointerEventPhase::Up,
            finput::PointerEventPhase::Up => finput::PointerEventPhase::Remove,
            finput::PointerEventPhase::Remove => finput::PointerEventPhase::Add,
            _ => unreachable!("Unsupported event phase"),
        }
    }

    /// Generate a pointer event command for the current state.
    /// The pointer is placed at random coordinates on the display.
    pub fn command(&mut self, rng: &mut SmallRng) -> fscenic::Command {
        let event_time = Time::get_monotonic().into_nanos().unsigned_abs();

        // TODO(xbhatnag): Migrate to the fuchsia.ui.pointerinjector protocol
        // instead of using this code path. This path will be deprecated soon.
        let pointer_event = finput::PointerEvent {
            event_time,
            device_id: self.device_id,
            pointer_id: self.pointer_id,
            type_: finput::PointerEventType::Touch,
            phase: self.phase,
            x: rng.gen_range(0..DISPLAY_WIDTH) as f32,
            y: rng.gen_range(0..DISPLAY_HEIGHT) as f32,
            radius_major: 0.0,
            radius_minor: 0.0,
            buttons: 0,
        };

        fscenic::Command::Input(finput::Command::SendPointerInput(finput::SendPointerInputCmd {
            compositor_id: self.compositor_id,
            pointer_event,
        }))
    }
}
