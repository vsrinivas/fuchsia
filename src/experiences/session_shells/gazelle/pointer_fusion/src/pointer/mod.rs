// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mouse;
mod touch;

use {
    super::*,
    fidl_fuchsia_ui_pointer as fptr, num,
    std::collections::{HashMap, HashSet},
    std::f32::EPSILON,
};

pub struct PointerFusionState {
    pixel_ratio: f32,
    mouse_device_info: HashMap<u32, fptr::MouseDeviceInfo>,
    mouse_down: HashSet<u32>,
    mouse_view_parameters: Option<fptr::ViewParameters>,
    pointer_states: HashMap<u32, PointerState>,
    next_pointer_id: i64,
}

impl PointerFusionState {
    /// Constructs a [PointerFusionState] with a display [pixel_ratio] used to convert logical
    /// coordinates into physical coordinates.
    pub fn new(pixel_ratio: f32) -> Self {
        PointerFusionState {
            pixel_ratio,
            mouse_device_info: HashMap::new(),
            mouse_down: HashSet::new(),
            mouse_view_parameters: None,
            pointer_states: HashMap::new(),
            next_pointer_id: 0,
        }
    }

    pub fn fuse_input(&mut self, input: InputEvent) -> Vec<PointerEvent> {
        match input {
            InputEvent::MouseEvent(mouse_event) => self.fuse_mouse(mouse_event),
            InputEvent::TouchEvent(touch_event) => self.fuse_touch(touch_event),
        }
    }
}

// The current information about a pointer derived from previous [PointerEvent]s. This is used to
// sanitized the pointer stream and synthesize addition data like `physical_delta_x`.
#[derive(Copy, Clone, Default)]
struct PointerState {
    id: i64,
    is_down: bool,
    physical_x: f32,
    physical_y: f32,
    buttons: i64,
}

impl PointerState {
    pub(crate) fn from_event(event: &PointerEvent) -> Self {
        let mut state = PointerState::default();
        state.physical_x = event.physical_x;
        state.physical_y = event.physical_y;
        state
    }

    pub(crate) fn is_location_changed(&self, event: &PointerEvent) -> bool {
        self.physical_x != event.physical_x || self.physical_y != event.physical_y
    }

    pub(crate) fn is_button_state_changed(&self, event: &PointerEvent) -> bool {
        self.buttons != event.buttons
    }

    pub(crate) fn compute_delta(&self, event: &PointerEvent) -> (f32, f32) {
        (event.physical_x - self.physical_x, event.physical_y - self.physical_y)
    }
}
