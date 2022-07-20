// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::session::{DISPLAY_HEIGHT, DISPLAY_WIDTH},
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fuchsia_zircon::Time,
};

/// State tracking for touch events. Touch events follow a cycle of states:
/// Add -> Down -> Move -> Up -> Remove -> Add -> ...
///
/// This struct generates these states repeatedly, at random coordinates on the display.
/// Note that when a touch event hits an object on the scene graph, a pointer event is sent to the
/// corresponding session's listener.
pub struct PointerState {
    phase: pointerinjector::EventPhase,
    pointer_id: u32,
    x: u16,
    y: u16,
}

impl PointerState {
    pub fn new() -> Self {
        Self { phase: pointerinjector::EventPhase::Remove, pointer_id: 1, x: 0, y: 0 }
    }

    /// Transition from one phase to the next.
    fn next_phase(&mut self) {
        self.phase = match self.phase {
            pointerinjector::EventPhase::Add => pointerinjector::EventPhase::Change,
            pointerinjector::EventPhase::Change => pointerinjector::EventPhase::Remove,
            pointerinjector::EventPhase::Remove => pointerinjector::EventPhase::Add,
            _ => unreachable!("Unsupported event phase"),
        }
    }

    /// Generate a pointer event for the current state.
    /// The pointer is placed at random coordinates on the display.
    pub fn next_event(&mut self) -> pointerinjector::Event {
        self.next_phase();

        let pointer_sample = pointerinjector::PointerSample {
            pointer_id: Some(self.pointer_id),
            phase: Some(self.phase),
            position_in_viewport: Some([self.x as f32, self.y as f32]),
            ..pointerinjector::PointerSample::EMPTY
        };

        // Update coordinates.
        self.y = (self.y + (self.x == DISPLAY_WIDTH) as u16) % (DISPLAY_HEIGHT + 1);
        self.x = (self.x + 1) % (DISPLAY_WIDTH + 1);

        pointerinjector::Event {
            timestamp: Some(Time::get_monotonic().into_nanos()),
            data: Some(pointerinjector::Data::PointerSample(pointer_sample)),
            ..pointerinjector::Event::EMPTY
        }
    }
}
