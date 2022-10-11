// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*, fidl_fuchsia_ui_pointer as fptr, fuchsia_zircon as zx, num, std::collections::HashSet,
};

const SCROLL_OFFSET_MULTIPLIER: i64 = 20;

impl PointerFusionState {
    // Converts raw [fptr::MouseEvent]s to one or more [PointerEvent]s.
    pub(super) fn fuse_mouse(&mut self, event: fptr::MouseEvent) -> Vec<PointerEvent> {
        if let Some(ref device_info) = event.device_info {
            self.mouse_device_info.insert(device_info.id.unwrap_or(0), device_info.clone());
        }

        if event.view_parameters.is_some() {
            self.mouse_view_parameters = event.view_parameters;
        }

        if has_valid_mouse_sample(&event) && self.mouse_view_parameters.is_some() {
            let sample = event.pointer_sample.as_ref().unwrap();
            let id = sample.device_id.unwrap();
            if self.mouse_device_info.contains_key(&id) {
                let any_button_down = sample.pressed_buttons.is_some();
                let phase = compute_mouse_phase(any_button_down, &mut self.mouse_down, id);

                let pointer_event = create_mouse_draft(
                    &event,
                    phase,
                    self.mouse_view_parameters.as_ref().unwrap(),
                    self.mouse_device_info.get(&id).unwrap(),
                    self.pixel_ratio,
                );

                let sanitized_events = self.sanitize_pointer(pointer_event);
                return sanitized_events;
            }
        }

        vec![]
    }

    // Sanitizes the [PointerEvent] draft such that the resulting event stream is contextually
    // correct. It may drop events or synthesize new events to keep the event stream sane.
    //
    // Note: It is still possible to craft an event stream that will cause an assert check to fail
    // on debug builds.
    fn sanitize_pointer(&mut self, mut event: PointerEvent) -> Vec<PointerEvent> {
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
            // Handle scroll events.
            _ => {}
        }
        converted_pointers
    }
}

fn compute_mouse_phase(any_button_down: bool, mouse_down: &mut HashSet<u32>, id: u32) -> Phase {
    if !mouse_down.contains(&id) && !any_button_down {
        return Phase::Hover;
    } else if !mouse_down.contains(&id) && any_button_down {
        mouse_down.insert(id);
        return Phase::Down;
    } else if mouse_down.contains(&id) && any_button_down {
        return Phase::Move;
    } else if mouse_down.contains(&id) && !any_button_down {
        mouse_down.remove(&id);
        return Phase::Up;
    } else {
        return Phase::Cancel;
    }
}

fn create_mouse_draft(
    event: &fptr::MouseEvent,
    phase: Phase,
    view_parameters: &fptr::ViewParameters,
    device_info: &fptr::MouseDeviceInfo,
    pixel_ratio: f32,
) -> PointerEvent {
    assert!(has_valid_mouse_sample(event));

    let sample = event.pointer_sample.as_ref().unwrap();

    let mut pointer = PointerEvent::default();
    pointer.timestamp = zx::Time::from_nanos(event.timestamp.unwrap_or(0));
    pointer.phase = phase;
    pointer.kind = DeviceKind::Mouse;
    pointer.device_id = sample.device_id.unwrap_or(0);

    let [logical_x, logical_y] =
        viewport_to_view_coordinates(sample.position_in_viewport.unwrap(), view_parameters);
    pointer.physical_x = logical_x * pixel_ratio;
    pointer.physical_y = logical_y * pixel_ratio;

    if sample.pressed_buttons.is_some() && device_info.buttons.is_some() {
        let mut pointer_buttons: i64 = 0;
        let pressed = sample.pressed_buttons.as_ref().unwrap();
        let device_buttons = device_info.buttons.as_ref().unwrap();
        for button_id in pressed {
            if let Some(index) = device_buttons.iter().position(|&r| r == *button_id) {
                pointer_buttons |= 1 << index;
            }
        }
        pointer.buttons = pointer_buttons;
    }

    if sample.scroll_h.is_some()
        || sample.scroll_v.is_some()
        || sample.scroll_h_physical_pixel.is_some()
        || sample.scroll_v_physical_pixel.is_some()
    {
        let tick_x_20ths = sample.scroll_h.unwrap_or(0) * SCROLL_OFFSET_MULTIPLIER;
        let tick_y_20ths = sample.scroll_v.unwrap_or(0) * SCROLL_OFFSET_MULTIPLIER;
        let offset_x = sample.scroll_h_physical_pixel.unwrap_or(tick_x_20ths as f64);
        let offset_y = sample.scroll_v_physical_pixel.unwrap_or(tick_y_20ths as f64);

        pointer.scroll_delta_x = offset_x;
        pointer.scroll_delta_y = offset_y;
    }

    pointer
}

fn has_valid_mouse_sample(event: &fptr::MouseEvent) -> bool {
    if event.pointer_sample.is_none() {
        return false;
    }
    let sample = event.pointer_sample.as_ref().unwrap();
    sample.device_id.is_some()
        && sample.position_in_viewport.is_some()
        && (sample.pressed_buttons.is_none()
            || !sample.pressed_buttons.as_ref().unwrap().is_empty())
}

fn viewport_to_view_coordinates(
    viewport_coordinates: [f32; 2],
    view_parameters: &fptr::ViewParameters,
) -> [f32; 2] {
    let viewport_to_view_transform = view_parameters.viewport_to_view_transform;
    // The transform matrix is a FIDL array with matrix data in column-major
    // order. For a matrix with data [a b c d e f g h i], and with the viewport
    // coordinates expressed as homogeneous coordinates, the logical view
    // coordinates are obtained with the following formula:
    //   |a d g|   |x|   |x'|
    //   |b e h| * |y| = |y'|
    //   |c f i|   |1|   |w'|
    // which we then normalize based on the w component:
    //   if z' not zero: (x'/w', y'/w')
    //   else (x', y')
    let m = viewport_to_view_transform;
    let x = viewport_coordinates[0];
    let y = viewport_coordinates[1];
    let xp = m[0] * x + m[3] * y + m[6];
    let yp = m[1] * x + m[4] * y + m[7];
    let wp = m[2] * x + m[5] * y + m[8];
    let [x, y] = if wp > EPSILON { [xp / wp, yp / wp] } else { [xp, yp] };

    clamp_to_view_space(x, y, view_parameters)
}

fn clamp_to_view_space(x: f32, y: f32, p: &fptr::ViewParameters) -> [f32; 2] {
    let min_x = p.view.min[0];
    let min_y = p.view.min[1];
    let max_x = p.view.max[0];
    let max_y = p.view.max[1];
    if min_x <= x && x < max_x && min_y <= y && y < max_y {
        return [x, y]; // No clamping to perform.
    }

    // View boundary is [min_x, max_x) x [min_y, max_y). Note that min is
    // inclusive, but max is exclusive - so we subtract epsilon.
    let max_x_inclusive = max_x - EPSILON;
    let max_y_inclusive = max_y - EPSILON;
    let clamped_x = num::clamp(x, min_x, max_x_inclusive);
    let clamped_y = num::clamp(y, min_y, max_y_inclusive);
    return [clamped_x, clamped_y];
}
