// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_ui_pointerinjector as pointerinjector, fuchsia_zircon::Time};

/// State tracking for touch events. Touch events follow a cycle of states:
/// Add -> Change -> Remove -> Add -> ...
///
/// This struct generates these states repeatedly, at random coordinates on the display.
/// Note that when a touch event hits an object on the scene graph, a pointer event is sent to the
/// corresponding scenic client.
pub struct PointerState {
    phase: pointerinjector::EventPhase,
    pointer_id: u32,
    x: u16,
    y: u16,
    display_width: u16,
    display_height: u16,
}

impl PointerState {
    pub fn new(display_width: u16, display_height: u16) -> Self {
        Self {
            // Initial phase is "Remove", so that the first call to next_event() yields "Add".
            phase: pointerinjector::EventPhase::Remove,
            pointer_id: 1,
            x: 0,
            y: 0,
            display_width,
            display_height,
        }
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
    /// The pointer coordinates move in a diagonal pattern across the display.
    pub fn next_event(&mut self) -> pointerinjector::Event {
        self.next_phase();

        let pointer_sample = pointerinjector::PointerSample {
            pointer_id: Some(self.pointer_id),
            phase: Some(self.phase),
            position_in_viewport: Some([self.x as f32, self.y as f32]),
            ..pointerinjector::PointerSample::EMPTY
        };

        // Update coordinates.
        self.y = (self.y + (self.x == self.display_width) as u16) % (self.display_height + 1);
        self.x = (self.x + 1) % (self.display_width + 1);

        pointerinjector::Event {
            timestamp: Some(Time::get_monotonic().into_nanos()),
            data: Some(pointerinjector::Data::PointerSample(pointer_sample)),
            ..pointerinjector::Event::EMPTY
        }
    }
}
