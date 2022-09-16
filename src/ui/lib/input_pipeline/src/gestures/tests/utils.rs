// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{gestures::gesture_arena, input_device, mouse_binding, touch_binding, Position},
    fidl_fuchsia_input_report as fidl_input_report, fuchsia_zircon as zx,
};

/// Takes a sequence of InputEvents and return a sequence of InputEvents the
/// gesture_arena converted to.
pub(super) async fn run_gesture_arena_test(
    inputs: Vec<input_device::InputEvent>,
) -> Vec<Vec<input_device::InputEvent>> {
    let handler = gesture_arena::make_input_handler(fuchsia_inspect::Inspector::new().root());

    let mut output: Vec<Vec<input_device::InputEvent>> = vec![];

    for e in inputs.into_iter() {
        let generated_events = handler.clone().handle_input_event(e).await;
        output.push(generated_events);
    }

    let arena =
        handler.as_rc_any().downcast::<gesture_arena::GestureArena>().expect("not a gesture arena");

    // This check ensure all incoming events have been forwarded or dropped.
    assert!(
        !arena.clone().has_buffered_events(),
        "has buffered events: {:?}",
        arena.mutable_state_to_str()
    );

    output
}

fn make_touchpad_descriptor() -> input_device::InputDeviceDescriptor {
    input_device::InputDeviceDescriptor::Touchpad(touch_binding::TouchpadDeviceDescriptor {
        device_id: 1,
        contacts: vec![touch_binding::ContactDeviceDescriptor {
            x_range: fidl_input_report::Range { min: 0, max: 10_000 },
            y_range: fidl_input_report::Range { min: 0, max: 10_000 },
            x_unit: fidl_input_report::Unit {
                type_: fidl_input_report::UnitType::Meters,
                exponent: -6,
            },
            y_unit: fidl_input_report::Unit {
                type_: fidl_input_report::UnitType::Meters,
                exponent: -6,
            },
            pressure_range: None,
            width_range: Some(fidl_input_report::Range { min: 0, max: 10_000 }),
            height_range: Some(fidl_input_report::Range { min: 0, max: 10_000 }),
        }],
    })
}

/// make a unhandled touchpad event for testing.
pub(super) fn make_touchpad_event(
    touchpad_event: touch_binding::TouchpadEvent,
) -> input_device::InputEvent {
    input_device::InputEvent {
        device_event: input_device::InputDeviceEvent::Touchpad(touchpad_event),
        device_descriptor: make_touchpad_descriptor(),
        event_time: zx::Time::ZERO,
        trace_id: None,
        handled: input_device::Handled::No,
    }
}

pub(super) const EPSILON: f32 = 1.0 / 100_000.0;

pub(super) const NO_MOVEMENT_LOCATION: mouse_binding::RelativeLocation =
    mouse_binding::RelativeLocation {
        counts: Position { x: 0.0, y: 0.0 },
        millimeters: Position { x: 0.0, y: 0.0 },
    };

/// [`expect_mouse_event`] is a helper marco extracts mouse information.
/// - motion event:
///   ```rust
///   expect_mouse_event(phase: phase, location: loc)
///   ```
/// - button event:
///   ```rust
///   expect_mouse_event(
///        phase: phase,
///        pressed_buttons: pb,
///        affected_buttons: ab,
///        location: loc)
///   ```
/// - wheel event:
///   ```rust
///   expect_mouse_event(
///        phase: phase,
///        delta_v: dv,
///        delta_h: dh,
///        location: loc)
///   ```
macro_rules! expect_mouse_event {
    (phase: $phase : ident, location: $location: ident) => {
        input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                phase: $phase,
                location: mouse_binding::MouseLocation::Relative($location),
                ..
            }),
            ..
        }
    };
    (phase: $phase : ident, pressed_buttons: $pressed_buttons: ident, affected_buttons: $affected_buttons: ident, location: $location: ident) => {
        input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                phase: $phase,
                location: mouse_binding::MouseLocation::Relative($location),
                pressed_buttons: $pressed_buttons,
                affected_buttons: $affected_buttons,
                ..
            }),
            ..
        }
    };
    (phase: $phase : ident, delta_v: $delta_v: ident, delta_h: $delta_h: ident,location: $location: ident) => {
        input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                phase: $phase,
                location: mouse_binding::MouseLocation::Relative($location),
                wheel_delta_v: $delta_v,
                wheel_delta_h: $delta_h,
                ..
            }),
            ..
        }
    };
}

pub(super) use expect_mouse_event;

macro_rules! extract_wheel_delta {
    ($delta: ident) => {
        Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Millimeters($delta),
            ..
        })
    };
}

pub(super) use extract_wheel_delta;
