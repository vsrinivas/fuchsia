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
    view_parameters: Option<fptr::ViewParameters>,
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
            view_parameters: None,
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

    // Sanitizes the [PointerEvent] draft such that the resulting event stream is contextually
    // correct. It may drop events or synthesize new events to keep the event stream sensible.
    //
    // Note: It is still possible to craft an event stream that will cause an assert check to fail
    // on debug builds.
    pub(crate) fn sanitize_pointer(&mut self, mut event: PointerEvent) -> Vec<PointerEvent> {
        let mut converted_pointers = vec![];
        match event.signal_kind {
            SignalKind::None => match event.phase {
                // Drops the Cancel if the pointer is not previously added.
                Phase::Cancel => {
                    if let Some(state) = self.pointer_states.get_mut(&event.device_id) {
                        assert!(state.is_down);

                        event.id = state.id;
                        // Synthesize a move event if the location does not match.
                        if state.is_location_changed(&event) {
                            let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                            let move_event = PointerEvent {
                                physical_delta_x,
                                physical_delta_y,
                                phase: Phase::Move,
                                synthesized: true,
                                ..event.clone()
                            };

                            state.physical_x = move_event.physical_x;
                            state.physical_y = move_event.physical_y;

                            converted_pointers.push(move_event);
                        }
                        state.is_down = false;
                        converted_pointers.push(event);
                    }
                }
                Phase::Add => {
                    assert!(!self.pointer_states.contains_key(&event.device_id));
                    let state = PointerState::from_event(&event);
                    self.pointer_states.insert(event.device_id, state);

                    converted_pointers.push(event);
                }
                Phase::Remove => {
                    assert!(self.pointer_states.contains_key(&event.device_id));
                    if let Some(state) = self.pointer_states.get_mut(&event.device_id) {
                        // Synthesize a Cancel event if pointer is down.
                        if state.is_down {
                            let mut cancel_event = event.clone();
                            cancel_event.phase = Phase::Cancel;
                            cancel_event.synthesized = true;
                            cancel_event.id = state.id;

                            state.is_down = false;
                            converted_pointers.push(cancel_event);
                        }

                        // Synthesize a hover event if the location does not match.
                        if state.is_location_changed(&event) {
                            let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                            let hover_event = PointerEvent {
                                physical_delta_x,
                                physical_delta_y,
                                phase: Phase::Hover,
                                synthesized: true,
                                ..event.clone()
                            };

                            state.physical_x = hover_event.physical_x;
                            state.physical_y = hover_event.physical_y;

                            converted_pointers.push(hover_event);
                        }
                    }
                    self.pointer_states.remove(&event.device_id);
                    converted_pointers.push(event);
                }
                Phase::Hover => {
                    let mut state = match self.pointer_states.get_mut(&event.device_id) {
                        Some(state) => *state,
                        None => {
                            // Synthesize add event if the pointer is not previously added.
                            let mut add_event = event.clone();
                            add_event.phase = Phase::Add;
                            add_event.synthesized = true;
                            let state = PointerState::from_event(&add_event);
                            self.pointer_states.insert(add_event.device_id, state);

                            converted_pointers.push(add_event);
                            state
                        }
                    };

                    assert!(!state.is_down);
                    if state.is_location_changed(&event) {
                        let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                        event.physical_delta_x = physical_delta_x;
                        event.physical_delta_y = physical_delta_y;

                        state.physical_x = event.physical_x;
                        state.physical_y = event.physical_y;
                        converted_pointers.push(event);
                    }
                }
                Phase::Down => {
                    let mut state = match self.pointer_states.get_mut(&event.device_id) {
                        Some(state) => *state,
                        None => {
                            // Synthesize add event if the pointer is not previously added.
                            let mut add_event = event.clone();
                            add_event.phase = Phase::Add;
                            add_event.synthesized = true;
                            let state = PointerState::from_event(&add_event);
                            self.pointer_states.insert(add_event.device_id, state);

                            converted_pointers.push(add_event);
                            state
                        }
                    };

                    assert!(!state.is_down);
                    // Synthesize a hover event if the location does not match.
                    if state.is_location_changed(&event) {
                        let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                        let hover_event = PointerEvent {
                            physical_delta_x,
                            physical_delta_y,
                            phase: Phase::Hover,
                            synthesized: true,
                            ..event.clone()
                        };

                        state.physical_x = hover_event.physical_x;
                        state.physical_y = hover_event.physical_y;

                        converted_pointers.push(hover_event);
                    }
                    self.next_pointer_id += 1;
                    state.id = self.next_pointer_id;
                    state.is_down = true;
                    state.buttons = event.buttons;
                    self.pointer_states.insert(event.device_id, state);
                    converted_pointers.push(event);
                }
                Phase::Move => {
                    // Makes sure we have an existing pointer in down state
                    let mut state =
                        self.pointer_states.get_mut(&event.device_id).expect("State should exist");
                    assert!(state.is_down);
                    event.id = state.id;

                    // Skip this event if location does not change.
                    if state.is_location_changed(&event) || state.is_button_state_changed(&event) {
                        let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                        event.physical_delta_x = physical_delta_x;
                        event.physical_delta_y = physical_delta_y;

                        state.physical_x = event.physical_x;
                        state.physical_y = event.physical_y;
                        state.buttons = event.buttons;
                        converted_pointers.push(event);
                    }
                }
                Phase::Up => {
                    // Makes sure we have an existing pointer in down state
                    let mut state =
                        self.pointer_states.get_mut(&event.device_id).expect("State should exist");
                    assert!(state.is_down);
                    event.id = state.id;

                    // Up phase should include which buttons where released.
                    let new_buttons = event.buttons;
                    event.buttons = state.buttons;

                    // Synthesize a move event if the location does not match.
                    if state.is_location_changed(&event) {
                        let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                        let move_event = PointerEvent {
                            physical_delta_x,
                            physical_delta_y,
                            phase: Phase::Move,
                            synthesized: true,
                            ..event.clone()
                        };

                        state.physical_x = move_event.physical_x;
                        state.physical_y = move_event.physical_y;

                        converted_pointers.push(move_event);
                    }
                    state.is_down = false;
                    state.buttons = new_buttons;
                    converted_pointers.push(event);
                }
            },
            SignalKind::Scroll => {
                let mut state = match self.pointer_states.get_mut(&event.device_id) {
                    Some(state) => *state,
                    None => {
                        // Synthesize add event if the pointer is not previously added.
                        let mut add_event = event.clone();
                        add_event.phase = Phase::Add;
                        add_event.synthesized = true;
                        let state = PointerState::from_event(&add_event);
                        self.pointer_states.insert(add_event.device_id, state);

                        converted_pointers.push(add_event);
                        state
                    }
                };

                // Synthesize a move / hover event if the location does not match.
                if state.is_location_changed(&event) {
                    let phase = if state.is_down { Phase::Move } else { Phase::Hover };
                    let (physical_delta_x, physical_delta_y) = state.compute_delta(&event);
                    let hover_event = PointerEvent {
                        physical_delta_x,
                        physical_delta_y,
                        phase,
                        synthesized: true,
                        ..event.clone()
                    };

                    state.physical_x = hover_event.physical_x;
                    state.physical_y = hover_event.physical_y;

                    converted_pointers.push(hover_event);
                }

                if state.is_scroll_offset_changed(&event) {
                    state.scroll_delta_x = event.scroll_delta_x;
                    state.scroll_delta_y = event.scroll_delta_y;

                    converted_pointers.push(event);
                }
            }
        }
        converted_pointers
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
    pub scroll_delta_y: f64,
    pub scroll_delta_x: f64,
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

    pub(crate) fn is_scroll_offset_changed(&self, event: &PointerEvent) -> bool {
        self.scroll_delta_x != event.scroll_delta_x || self.scroll_delta_y != event.scroll_delta_y
    }

    pub(crate) fn compute_delta(&self, event: &PointerEvent) -> (f32, f32) {
        (event.physical_x - self.physical_x, event.physical_y - self.physical_y)
    }
}

/// The transform matrix is a FIDL array with matrix data in column-major
/// order. For a matrix with data [a b c d e f g h i], and with the viewport
/// coordinates expressed as homogeneous coordinates, the logical view
/// coordinates are obtained with the following formula:
///   |a d g|   |x|   |x'|
///   |b e h| * |y| = |y'|
///   |c f i|   |1|   |w'|
/// which we then normalize based on the w component:
///   if z' not zero: (x'/w', y'/w')
///   else (x', y')
pub(crate) fn viewport_to_view_coordinates(
    viewport_coordinates: [f32; 2],
    view_parameters: &fptr::ViewParameters,
) -> [f32; 2] {
    let viewport_to_view_transform = view_parameters.viewport_to_view_transform;

    let m = viewport_to_view_transform;
    let x = viewport_coordinates[0];
    let y = viewport_coordinates[1];
    let xp = m[0] * x + m[3] * y + m[6];
    let yp = m[1] * x + m[4] * y + m[7];
    let wp = m[2] * x + m[5] * y + m[8];
    let [x, y] = if wp > EPSILON { [xp / wp, yp / wp] } else { [xp, yp] };

    clamp_to_view_space(x, y, view_parameters)
}

/// Clamp coordinates to view space, with min inclusive and max exclusive.
fn clamp_to_view_space(x: f32, y: f32, p: &fptr::ViewParameters) -> [f32; 2] {
    let min_x = p.view.min[0];
    let min_y = p.view.min[1];
    let max_x = p.view.max[0];
    let max_y = p.view.max[1];
    if min_x <= x && x < max_x && min_y <= y && y < max_y {
        return [x, y]; // No clamping to perform.
    }

    // TODO(fxb/111781): Use floats_extra crate to safely subtract epsilon.
    // View boundary is [min_x, max_x) x [min_y, max_y). Note that min is
    // inclusive, but max is exclusive - so we subtract epsilon.
    let max_x_inclusive = max_x - EPSILON;
    let max_y_inclusive = max_y - EPSILON;
    let clamped_x = num::clamp(x, min_x, max_x_inclusive);
    let clamped_y = num::clamp(y, min_y, max_y_inclusive);
    return [clamped_x, clamped_y];
}
