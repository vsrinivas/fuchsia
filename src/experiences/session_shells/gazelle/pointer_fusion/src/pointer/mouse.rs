// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, fidl_fuchsia_ui_pointer as fptr, fuchsia_zircon as zx, std::collections::HashSet};

const SCROLL_OFFSET_MULTIPLIER: i64 = 20;

impl PointerFusionState {
    // Converts raw [fptr::MouseEvent]s to one or more [PointerEvent]s.
    pub(super) fn fuse_mouse(&mut self, event: fptr::MouseEvent) -> Vec<PointerEvent> {
        if let Some(ref device_info) = event.device_info {
            self.mouse_device_info.insert(device_info.id.unwrap_or(0), device_info.clone());
        }

        if event.view_parameters.is_some() {
            self.view_parameters = event.view_parameters;
        }

        if has_valid_mouse_sample(&event) && self.view_parameters.is_some() {
            let sample = event.pointer_sample.as_ref().unwrap();
            let id = sample.device_id.unwrap();
            if self.mouse_device_info.contains_key(&id) {
                let any_button_down = sample.pressed_buttons.is_some();
                let phase = compute_mouse_phase(any_button_down, &mut self.mouse_down, id);

                let pointer_event = create_mouse_draft(
                    &event,
                    phase,
                    self.view_parameters.as_ref().unwrap(),
                    self.mouse_device_info.get(&id).unwrap(),
                    self.pixel_ratio,
                );

                let sanitized_events = self.sanitize_pointer(pointer_event);
                return sanitized_events;
            }
        }

        vec![]
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

        pointer.signal_kind = SignalKind::Scroll;
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
