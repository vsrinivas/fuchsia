// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//!
use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, utils::Position},
    async_trait::async_trait,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    std::{cell::RefCell, convert::From, num::FpCategory, option::Option, rc::Rc},
};

pub struct PointerSensorScaleHandler {
    mutable_state: RefCell<MutableState>,
}

struct MutableState {
    /// The time of the last processed mouse move event.
    last_move_timestamp: Option<zx::Time>,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for PointerSensorScaleHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            // TODO(https://fxbug.dev/98699) Disable scaling when in immersive mode.
            input_device::UnhandledInputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location:
                            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                                millimeters: raw_motion,
                                counts: _,
                            }),
                        wheel_delta_v,
                        wheel_delta_h,
                        // Only the `Move` phase carries non-zero motion.
                        phase: phase @ mouse_binding::MousePhase::Move,
                        affected_buttons,
                        pressed_buttons,
                    }),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
                        absolute_x_range,
                        absolute_y_range,
                        buttons,
                        counts_per_mm,
                        device_id,
                        wheel_h_range,
                        wheel_v_range,
                    }),
                event_time,
                trace_id: _,
            } => {
                let scaled_motion = self.scale_motion(raw_motion, event_time);
                let input_event = input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Mouse(
                        mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation {
                                    counts: scaled_motion * counts_per_mm,
                                    millimeters: scaled_motion,
                                },
                            ),
                            wheel_delta_v,
                            wheel_delta_h,
                            phase,
                            affected_buttons,
                            pressed_buttons,
                        },
                    ),
                    device_descriptor: input_device::InputDeviceDescriptor::Mouse(
                        mouse_binding::MouseDeviceDescriptor {
                            absolute_x_range,
                            absolute_y_range,
                            buttons,
                            counts_per_mm,
                            device_id,
                            wheel_h_range,
                            wheel_v_range,
                        },
                    ),
                    event_time,
                    handled: input_device::Handled::No,
                    trace_id: None,
                };
                vec![input_event]
            }
            _ => vec![input_device::InputEvent::from(unhandled_input_event)],
        }
    }
}

// The minimum reasonable delay between intentional mouse movements.
// This value
// * Is used to compensate for time compression if the driver gets
//   backlogged.
// * Is set to accommodate up to 10 kHZ event reporting.
//
// TODO(https://fxbug.dev/98920): Use the polling rate instead of event timestamps.
const MIN_PLAUSIBLE_EVENT_DELAY: zx::Duration = zx::Duration::from_micros(100);

// The maximum reasonable delay between intentional mouse movements.
// This value is used to compute speed for the first mouse motion after
// a long idle period.
//
// Alternatively:
// 1. The code could use the uncapped delay. However, this would lead to
//    very slow initial motion after a long idle period.
// 2. Wait until a second report comes in. However, older mice generate
//    reports at 125 HZ, which would mean an 8 msec delay.
//
// TODO(https://fxbug.dev/98920): Use the polling rate instead of event timestamps.
const MAX_PLAUSIBLE_EVENT_DELAY: zx::Duration = zx::Duration::from_millis(50);

const MAX_SENSOR_COUNTS_PER_INCH: f32 = 20_000.0; // From https://sensor.fyi/sensors
const MAX_SENSOR_COUNTS_PER_MM: f32 = MAX_SENSOR_COUNTS_PER_INCH / 12.7;
const MIN_MEASURABLE_DISTANCE_MM: f32 = 1.0 / MAX_SENSOR_COUNTS_PER_MM;
const MAX_PLAUSIBLE_EVENT_DELAY_SECS: f32 = MAX_PLAUSIBLE_EVENT_DELAY.into_nanos() as f32 / 1E9;
const MIN_MEASURABLE_VELOCITY_MM_PER_SEC: f32 =
    MIN_MEASURABLE_DISTANCE_MM / MAX_PLAUSIBLE_EVENT_DELAY_SECS;

// Define the buckets which determine which mapping to use.
// * Speeds below the beginning of the medium range use the low-speed mapping.
// * Speeds within the medium range use the medium-speed mapping.
// * Speeds above the end of the medium range use the high-speed mapping.
const MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC: f32 = 32.0;
const MEDIUM_SPEED_RANGE_END_MM_PER_SEC: f32 = 150.0;

// A linear factor affecting the responsiveness of the pointer to motion.
// A higher numbness indicates lower responsiveness.
const NUMBNESS: f32 = 37.5;

impl PointerSensorScaleHandler {
    /// Creates a new [`PointerSensorScaleHandler`].
    ///
    /// Returns `Rc<Self>`.
    pub fn new() -> Rc<Self> {
        Rc::new(Self { mutable_state: RefCell::new(MutableState { last_move_timestamp: None }) })
    }

    // Linearly scales `movement_mm_per_sec`.
    //
    // Given the values of `MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC` and
    // `NUMBNESS` above, this results in downscaling the motion.
    fn scale_low_speed(movement_mm_per_sec: f32) -> f32 {
        const LINEAR_SCALE_FACTOR: f32 = MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC / NUMBNESS;
        LINEAR_SCALE_FACTOR * movement_mm_per_sec
    }

    // Quadratically scales `movement_mm_per_sec`.
    //
    // The scale factor is chosen so that the composite curve is
    // continuous as the speed transitions from the low-speed
    // bucket to the medium-speed bucket.
    //
    // Note that the composite curve is _not_ differentiable at the
    // transition from low-speed to medium-speed, since the
    // slope on the left side of the point
    // (MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC / NUMBNESS)
    // is different from the slope on the right side of the point
    // (2 * MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC / NUMBNESS).
    //
    // However, the transition works well enough in practice.
    fn scale_medium_speed(movement_mm_per_sec: f32) -> f32 {
        const QUARDRATIC_SCALE_FACTOR: f32 = 1.0 / NUMBNESS;
        QUARDRATIC_SCALE_FACTOR * movement_mm_per_sec * movement_mm_per_sec
    }

    // Linearly scales `movement_mm_per_sec`.
    //
    // The parameters are chosen so that
    // 1. The composite curve is continuous as the speed transitions
    //    from the medium-speed bucket to the high-speed bucket.
    // 2. The composite curve is differentiable.
    fn scale_high_speed(movement_mm_per_sec: f32) -> f32 {
        // Use linear scaling equal to the slope of `scale_medium_speed()`
        // at the transition point.
        const LINEAR_SCALE_FACTOR: f32 = 2.0 * (MEDIUM_SPEED_RANGE_END_MM_PER_SEC / NUMBNESS);

        // Compute offset so the composite curve is continuous.
        const Y_AT_MEDIUM_SPEED_RANGE_END_MM_PER_SEC: f32 =
            MEDIUM_SPEED_RANGE_END_MM_PER_SEC * MEDIUM_SPEED_RANGE_END_MM_PER_SEC / NUMBNESS;
        const OFFSET: f32 = Y_AT_MEDIUM_SPEED_RANGE_END_MM_PER_SEC
            - LINEAR_SCALE_FACTOR * MEDIUM_SPEED_RANGE_END_MM_PER_SEC;

        // Apply the computed transformation.
        LINEAR_SCALE_FACTOR * movement_mm_per_sec + OFFSET
    }

    // Scales Euclidean velocity by one of the scale_*_speed_motion() functions above,
    // choosing the function based on `MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC` and
    // `MEDIUM_SPEED_RANGE_END_MM_PER_SEC`.
    fn scale_euclidean_velocity(raw_velocity: f32) -> f32 {
        if (0.0..MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC).contains(&raw_velocity) {
            Self::scale_low_speed(raw_velocity)
        } else if (MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC..MEDIUM_SPEED_RANGE_END_MM_PER_SEC)
            .contains(&raw_velocity)
        {
            Self::scale_medium_speed(raw_velocity)
        } else {
            Self::scale_high_speed(raw_velocity)
        }
    }

    /// Scales `movement_mm`.
    fn scale_motion(&self, movement_mm: Position, event_time: zx::Time) -> Position {
        // Determine the duration of this `movement`.
        let elapsed_time_secs =
            match self.mutable_state.borrow_mut().last_move_timestamp.replace(event_time) {
                Some(last_event_time) => (event_time - last_event_time)
                    .clamp(MIN_PLAUSIBLE_EVENT_DELAY, MAX_PLAUSIBLE_EVENT_DELAY),
                None => MAX_PLAUSIBLE_EVENT_DELAY,
            }
            .into_nanos() as f32
                / 1E9;

        // Compute the velocity in each dimension.
        let x_mm_per_sec = movement_mm.x / elapsed_time_secs;
        let y_mm_per_sec = movement_mm.y / elapsed_time_secs;

        let euclidean_velocity =
            f32::sqrt(x_mm_per_sec * x_mm_per_sec + y_mm_per_sec * y_mm_per_sec);
        if euclidean_velocity < MIN_MEASURABLE_VELOCITY_MM_PER_SEC {
            // Avoid division by zero that would come from computing `scale_factor` below.
            return movement_mm;
        }

        // Compute the scaling factor to be applied to each dimension.
        //
        // Geometrically, this is a bit dodgy when there's movement along both
        // dimensions. Specifically: the `OFFSET` for high-speed motion should be
        // constant, but the way its used here scales the offset based on velocity.
        //
        // Nonetheless, this works well enough in practice.
        let scale_factor = Self::scale_euclidean_velocity(euclidean_velocity) / euclidean_velocity;

        // Apply the scale factor and return the result.
        let scaled_movement_mm = scale_factor * movement_mm;

        match (scaled_movement_mm.x.classify(), scaled_movement_mm.y.classify()) {
            (FpCategory::Infinite | FpCategory::Nan, _)
            | (_, FpCategory::Infinite | FpCategory::Nan) => {
                // Backstop, in case the code above missed some cases of bad arithmetic.
                // Avoid sending `Infinite` or `Nan` values, since such values will
                // poison the `current_position` in `MouseInjectorHandlerInner`.
                // That manifests as the pointer becoming invisible, and never
                // moving again.
                //
                // TODO(https://fxbug.dev/98995) Add a triage rule to highlight the
                // implications of this message.
                fx_log_err!(
                    "skipped motion; scaled movement of {:?} is infinite or NaN; x is {:?}, and y is {:?}",
                    scaled_movement_mm,
                    scaled_movement_mm.x.classify(),
                    scaled_movement_mm.y.classify(),
                );
                Position { x: 0.0, y: 0.0 }
            }
            _ => scaled_movement_mm,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_zircon as zx,
        maplit::hashset,
        std::cell::Cell,
        test_util::{assert_gt, assert_lt, assert_near},
    };

    const COUNTS_PER_MM: f32 = 12.0;
    const DEVICE_DESCRIPTOR: input_device::InputDeviceDescriptor =
        input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
            device_id: 0,
            absolute_x_range: None,
            absolute_y_range: None,
            wheel_v_range: None,
            wheel_h_range: None,
            buttons: None,
            counts_per_mm: COUNTS_PER_MM as i64,
        });

    // Maximum tolerable difference between "equal" scale factors. This is
    // likely higher than FP rounding error can explain, but still small
    // enough that there would be no user-perceptible difference.
    //
    // Rationale for not being user-perceptible: this requires the raw
    // movement to have a count of 100,000, before there's a unit change
    // in the scaled motion.
    //
    // On even the highest resolution sensor (per https://sensor.fyi/sensors),
    // that would require 127mm (5 inches) of motion within one sampling
    // interval.
    //
    // In the unlikely case that the high resolution sensor is paired
    // with a low polling rate, that works out to 127mm/8msec, or _at least_
    // 57 km/hr.
    const SCALE_EPSILON: f32 = 1.0 / 100_000.0;

    std::thread_local! {static NEXT_EVENT_TIME: Cell<i64> = Cell::new(0)}

    fn make_unhandled_input_event(
        mouse_event: mouse_binding::MouseEvent,
    ) -> input_device::UnhandledInputEvent {
        let event_time = NEXT_EVENT_TIME.with(|t| {
            let old = t.get();
            t.set(old + 1);
            old
        });
        input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_event),
            device_descriptor: DEVICE_DESCRIPTOR.clone(),
            event_time: zx::Time::from_nanos(event_time),
            trace_id: None,
        }
    }

    // While its generally preferred to write tests against the public API of
    // a module, these tests
    // 1. Can't be written against the public API (since that API doesn't
    //    provide a way to control which curve is used for scaling), and
    // 2. Validate important properties of the module.
    mod internal_computations {
        use super::*;

        #[fuchsia::test]
        fn transition_from_low_to_medium_is_continuous() {
            assert_near!(
                PointerSensorScaleHandler::scale_low_speed(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC),
                PointerSensorScaleHandler::scale_medium_speed(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC),
                SCALE_EPSILON
            );
        }

        // As noted in `scale_motion()`, the offset will be applied imperfectly,
        // so the externally visible transition may not be continuous.
        //
        // However, it's still valuable to verify that the internal building block
        // works as intended.
        #[fuchsia::test]
        fn transition_from_medium_to_high_is_continuous() {
            assert_near!(
                PointerSensorScaleHandler::scale_medium_speed(MEDIUM_SPEED_RANGE_END_MM_PER_SEC),
                PointerSensorScaleHandler::scale_high_speed(MEDIUM_SPEED_RANGE_END_MM_PER_SEC),
                SCALE_EPSILON
            );
        }
    }

    mod motion_scaling {
        use super::*;

        #[ignore]
        #[fuchsia::test(allow_stalls = false)]
        async fn plot_example_curve() {
            let duration = zx::Duration::from_millis(8);
            for count in 1..1000 {
                let scaled_count =
                    get_scaled_motion(Position { x: count as f32, y: 0.0 }, duration).await;
                fx_log_err!("{}, {}", count, scaled_count.x);
            }
        }

        async fn get_scaled_motion(movement_counts: Position, duration: zx::Duration) -> Position {
            let movement_mm = movement_counts / COUNTS_PER_MM;
            let handler = PointerSensorScaleHandler::new();

            // Send a don't-care value through to seed the last timestamp.
            let input_event = input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    location: mouse_binding::MouseLocation::Relative(Default::default()),
                    wheel_delta_v: None,
                    wheel_delta_h: None,
                    phase: mouse_binding::MousePhase::Move,
                    affected_buttons: hashset! {},
                    pressed_buttons: hashset! {},
                }),
                device_descriptor: DEVICE_DESCRIPTOR.clone(),
                event_time: zx::Time::from_nanos(0),
                trace_id: None,
            };
            handler.clone().handle_unhandled_input_event(input_event).await;

            // Send in the requested motion.
            let input_event = input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    location: mouse_binding::MouseLocation::Relative(
                        mouse_binding::RelativeLocation {
                            counts: Position::zero(),
                            millimeters: movement_mm,
                        },
                    ),
                    wheel_delta_v: None,
                    wheel_delta_h: None,
                    phase: mouse_binding::MousePhase::Move,
                    affected_buttons: hashset! {},
                    pressed_buttons: hashset! {},
                }),
                device_descriptor: DEVICE_DESCRIPTOR.clone(),
                event_time: zx::Time::from_nanos(duration.into_nanos()),
                trace_id: None,
            };
            let transformed_events =
                handler.clone().handle_unhandled_input_event(input_event).await;

            // Provide a useful debug message if the transformed event doesn't have the expected
            // overall structure.
            assert_matches!(
                transformed_events.as_slice(),
                [input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Mouse(
                        mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation { .. }
                            ),
                            ..
                        }
                    ),
                    ..
                }]
            );

            // Return the transformed motion.
            if let input_device::InputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location:
                            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                                counts: movement_counts,
                                millimeters: _,
                            }),
                        ..
                    }),
                ..
            } = transformed_events[0]
            {
                movement_counts
            } else {
                unreachable!()
            }
        }

        fn velocity_to_count(velocity_mm_per_sec: f32, duration: zx::Duration) -> f32 {
            velocity_mm_per_sec * (duration.into_nanos() as f32 / 1E9) * COUNTS_PER_MM
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_horizontal_motion_scales_linearly() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_COUNTS: f32 = 1.0;
            const MOTION_B_COUNTS: f32 = 2.0;
            assert_lt!(
                MOTION_B_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion(Position { x: MOTION_A_COUNTS, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion(Position { x: MOTION_B_COUNTS, y: 0.0 }, TICK_DURATION).await;
            assert_near!(scaled_b.x / scaled_a.x, 2.0, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_vertical_motion_scales_linearly() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_COUNTS: f32 = 1.0;
            const MOTION_B_COUNTS: f32 = 2.0;
            assert_lt!(
                MOTION_B_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion(Position { x: 0.0, y: MOTION_A_COUNTS }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion(Position { x: 0.0, y: MOTION_B_COUNTS }, TICK_DURATION).await;
            assert_near!(scaled_b.y / scaled_a.y, 2.0, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_45degree_motion_scales_dimensions_equally() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_COUNTS: f32 = 1.0;
            assert_lt!(
                MOTION_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled =
                get_scaled_motion(Position { x: MOTION_COUNTS, y: MOTION_COUNTS }, TICK_DURATION)
                    .await;
            assert_near!(scaled.x, scaled.y, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn medium_speed_motion_scales_quadratically() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_COUNTS: f32 = 7.0;
            const MOTION_B_COUNTS: f32 = 14.0;
            assert_gt!(
                MOTION_A_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );
            assert_lt!(
                MOTION_B_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_END_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion(Position { x: MOTION_A_COUNTS, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion(Position { x: MOTION_B_COUNTS, y: 0.0 }, TICK_DURATION).await;
            assert_near!(scaled_b.x / scaled_a.x, 4.0, SCALE_EPSILON);
        }

        // Given the handling of `OFFSET` for high-speed motion, (see comment
        // in `scale_motion()`), high speed motion scaling is _not_ linear for
        // the range of values of practical interest.
        //
        // Thus, this tests verifies a weaker property.
        #[fuchsia::test(allow_stalls = false)]
        async fn high_speed_motion_scaling_is_increasing() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_COUNTS: f32 = 16.0;
            const MOTION_B_COUNTS: f32 = 20.0;
            assert_gt!(
                MOTION_A_COUNTS,
                velocity_to_count(MEDIUM_SPEED_RANGE_END_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion(Position { x: MOTION_A_COUNTS, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion(Position { x: MOTION_B_COUNTS, y: 0.0 }, TICK_DURATION).await;
            assert_gt!(scaled_b.x, scaled_a.x)
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn zero_motion_maps_to_zero_motion() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            let scaled = get_scaled_motion(Position { x: 0.0, y: 0.0 }, TICK_DURATION).await;
            assert_eq!(scaled, Position::zero())
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn zero_duration_does_not_crash() {
            get_scaled_motion(Position { x: 1.0, y: 0.0 }, zx::Duration::from_millis(0)).await;
        }
    }

    mod motion_scaling_mm {
        use super::*;

        #[ignore]
        #[fuchsia::test(allow_stalls = false)]
        async fn plot_example_curve() {
            let duration = zx::Duration::from_millis(8);
            for count in 1..1000 {
                let scaled_count = get_scaled_motion_mm(
                    Position { x: count as f32 / COUNTS_PER_MM, y: 0.0 },
                    duration,
                )
                .await;
                fx_log_err!("{}, {}", count, scaled_count.x);
            }
        }

        async fn get_scaled_motion_mm(movement_mm: Position, duration: zx::Duration) -> Position {
            let handler = PointerSensorScaleHandler::new();

            // Send a don't-care value through to seed the last timestamp.
            let input_event = input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    location: mouse_binding::MouseLocation::Relative(Default::default()),
                    wheel_delta_v: None,
                    wheel_delta_h: None,
                    phase: mouse_binding::MousePhase::Move,
                    affected_buttons: hashset! {},
                    pressed_buttons: hashset! {},
                }),
                device_descriptor: DEVICE_DESCRIPTOR.clone(),
                event_time: zx::Time::from_nanos(0),
                trace_id: None,
            };
            handler.clone().handle_unhandled_input_event(input_event).await;

            // Send in the requested motion.
            let input_event = input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    location: mouse_binding::MouseLocation::Relative(
                        mouse_binding::RelativeLocation {
                            counts: Position::zero(),
                            millimeters: movement_mm,
                        },
                    ),
                    wheel_delta_v: None,
                    wheel_delta_h: None,
                    phase: mouse_binding::MousePhase::Move,
                    affected_buttons: hashset! {},
                    pressed_buttons: hashset! {},
                }),
                device_descriptor: DEVICE_DESCRIPTOR.clone(),
                event_time: zx::Time::from_nanos(duration.into_nanos()),
                trace_id: None,
            };
            let transformed_events =
                handler.clone().handle_unhandled_input_event(input_event).await;

            // Provide a useful debug message if the transformed event doesn't have the expected
            // overall structure.
            assert_matches!(
                transformed_events.as_slice(),
                [input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Mouse(
                        mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation { .. }
                            ),
                            ..
                        }
                    ),
                    ..
                }]
            );

            // Return the transformed motion.
            if let input_device::InputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location:
                            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                                counts: _,
                                millimeters: movement_mm,
                            }),
                        ..
                    }),
                ..
            } = transformed_events[0]
            {
                movement_mm
            } else {
                unreachable!()
            }
        }

        fn velocity_to_mm(velocity_mm_per_sec: f32, duration: zx::Duration) -> f32 {
            velocity_mm_per_sec * (duration.into_nanos() as f32 / 1E9)
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_horizontal_motion_scales_linearly() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_MM: f32 = 1.0 / COUNTS_PER_MM;
            const MOTION_B_MM: f32 = 2.0 / COUNTS_PER_MM;
            assert_lt!(
                MOTION_B_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion_mm(Position { x: MOTION_A_MM, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion_mm(Position { x: MOTION_B_MM, y: 0.0 }, TICK_DURATION).await;
            assert_near!(scaled_b.x / scaled_a.x, 2.0, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_vertical_motion_scales_linearly() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_MM: f32 = 1.0 / COUNTS_PER_MM;
            const MOTION_B_MM: f32 = 2.0 / COUNTS_PER_MM;
            assert_lt!(
                MOTION_B_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion_mm(Position { x: 0.0, y: MOTION_A_MM }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion_mm(Position { x: 0.0, y: MOTION_B_MM }, TICK_DURATION).await;
            assert_near!(scaled_b.y / scaled_a.y, 2.0, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn low_speed_45degree_motion_scales_dimensions_equally() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_MM: f32 = 1.0 / COUNTS_PER_MM;
            assert_lt!(
                MOTION_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );

            let scaled =
                get_scaled_motion_mm(Position { x: MOTION_MM, y: MOTION_MM }, TICK_DURATION).await;
            assert_near!(scaled.x, scaled.y, SCALE_EPSILON);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn medium_speed_motion_scales_quadratically() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_MM: f32 = 7.0 / COUNTS_PER_MM;
            const MOTION_B_MM: f32 = 14.0 / COUNTS_PER_MM;
            assert_gt!(
                MOTION_A_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_BEGIN_MM_PER_SEC, TICK_DURATION)
            );
            assert_lt!(
                MOTION_B_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_END_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion_mm(Position { x: MOTION_A_MM, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion_mm(Position { x: MOTION_B_MM, y: 0.0 }, TICK_DURATION).await;
            assert_near!(scaled_b.x / scaled_a.x, 4.0, SCALE_EPSILON);
        }

        // Given the handling of `OFFSET` for high-speed motion, (see comment
        // in `scale_motion()`), high speed motion scaling is _not_ linear for
        // the range of values of practical interest.
        //
        // Thus, this tests verifies a weaker property.
        #[fuchsia::test(allow_stalls = false)]
        async fn high_speed_motion_scaling_is_increasing() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            const MOTION_A_MM: f32 = 16.0 / COUNTS_PER_MM;
            const MOTION_B_MM: f32 = 20.0 / COUNTS_PER_MM;
            assert_gt!(
                MOTION_A_MM,
                velocity_to_mm(MEDIUM_SPEED_RANGE_END_MM_PER_SEC, TICK_DURATION)
            );

            let scaled_a =
                get_scaled_motion_mm(Position { x: MOTION_A_MM, y: 0.0 }, TICK_DURATION).await;
            let scaled_b =
                get_scaled_motion_mm(Position { x: MOTION_B_MM, y: 0.0 }, TICK_DURATION).await;
            assert_gt!(scaled_b.x, scaled_a.x)
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn zero_motion_maps_to_zero_motion() {
            const TICK_DURATION: zx::Duration = zx::Duration::from_millis(8);
            let scaled = get_scaled_motion_mm(Position { x: 0.0, y: 0.0 }, TICK_DURATION).await;
            assert_eq!(scaled, Position::zero())
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn zero_duration_does_not_crash() {
            get_scaled_motion_mm(
                Position { x: 1.0 / COUNTS_PER_MM, y: 0.0 },
                zx::Duration::from_millis(0),
            )
            .await;
        }
    }

    mod metadata_preservation {
        use super::*;

        #[fuchsia::test(allow_stalls = false)]
        async fn does_not_consume_event() {
            let handler = PointerSensorScaleHandler::new();
            let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 1.5, y: 4.5 },
                    millimeters: Position { x: 1.5 / COUNTS_PER_MM, y: 4.5 / COUNTS_PER_MM },
                }),
                wheel_delta_v: None,
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Move,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
            });
            assert_matches!(
                handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
                [input_device::InputEvent { handled: input_device::Handled::No, .. }]
            );
        }

        // Downstream handlers, and components consuming the `MouseEvent`, may be
        // sensitive to the speed of motion. So it's important to preserve timestamps.
        #[fuchsia::test(allow_stalls = false)]
        async fn preserves_event_time() {
            let handler = PointerSensorScaleHandler::new();
            let mut input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 1.5, y: 4.5 },
                    millimeters: Position { x: 1.5 / COUNTS_PER_MM, y: 4.5 / COUNTS_PER_MM },
                }),
                wheel_delta_v: None,
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Move,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
            });
            const EVENT_TIME: zx::Time = zx::Time::from_nanos(42);
            input_event.event_time = EVENT_TIME;
            assert_matches!(
                handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
                [input_device::InputEvent { event_time: EVENT_TIME, .. }]
            );
        }
    }
}
