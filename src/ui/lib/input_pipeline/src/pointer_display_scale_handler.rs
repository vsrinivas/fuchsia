// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::fx_log_err;

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, utils::Position},
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fuchsia_syslog::fx_log_debug,
    std::{convert::From, rc::Rc},
};

// TODO(fxbug.dev/91272) Add trackpad support
#[derive(Debug, PartialEq)]
pub struct PointerDisplayScaleHandler {
    /// The amount by which motion will be scaled up. E.g., a `scale_factor`
    /// of 2 means that all motion will be multiplied by 2.
    scale_factor: f32,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for PointerDisplayScaleHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location:
                            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                                counts: raw_counts,
                                millimeters: raw_mm,
                            }),
                        wheel_delta_v,
                        wheel_delta_h,
                        // Only the `Move` phase carries non-zero motion.
                        phase: phase @ mouse_binding::MousePhase::Move,
                        affected_buttons,
                        pressed_buttons,
                        is_precision_scroll,
                    }),
                device_descriptor: device_descriptor @ input_device::InputDeviceDescriptor::Mouse(_),
                event_time,
                trace_id: _,
            } => {
                let scaled_counts = self.scale_motion(raw_counts);
                let scaled_mm = self.scale_motion(raw_mm);
                let input_event = input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Mouse(
                        mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation {
                                    counts: scaled_counts,
                                    millimeters: scaled_mm,
                                },
                            ),
                            wheel_delta_v,
                            wheel_delta_h,
                            phase,
                            affected_buttons,
                            pressed_buttons,
                            is_precision_scroll,
                        },
                    ),
                    device_descriptor,
                    event_time,
                    handled: input_device::Handled::No,
                    trace_id: None,
                };
                vec![input_event]
            }
            input_device::UnhandledInputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location,
                        wheel_delta_v,
                        wheel_delta_h,
                        phase: phase @ mouse_binding::MousePhase::Wheel,
                        affected_buttons,
                        pressed_buttons,
                        is_precision_scroll,
                    }),
                device_descriptor: device_descriptor @ input_device::InputDeviceDescriptor::Mouse(_),
                event_time,
                trace_id: _,
            } => {
                let scaled_wheel_delta_v = self.scale_wheel_delta(wheel_delta_v);
                let scaled_wheel_delta_h = self.scale_wheel_delta(wheel_delta_h);
                let input_event = input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Mouse(
                        mouse_binding::MouseEvent {
                            location,
                            wheel_delta_v: scaled_wheel_delta_v,
                            wheel_delta_h: scaled_wheel_delta_h,
                            phase,
                            affected_buttons,
                            pressed_buttons,
                            is_precision_scroll,
                        },
                    ),
                    device_descriptor,
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

impl PointerDisplayScaleHandler {
    /// Creates a new [`PointerMotionDisplayScaleHandler`].
    ///
    /// Returns
    /// * `Ok(Rc<Self>)` if `scale_factor` is finite and >= 1.0, and
    /// * `Err(Error)` otherwise.
    pub fn new(scale_factor: f32) -> Result<Rc<Self>, Error> {
        fx_log_debug!("scale_factor={}", scale_factor);
        use std::num::FpCategory;
        match scale_factor.classify() {
            FpCategory::Nan | FpCategory::Infinite | FpCategory::Zero | FpCategory::Subnormal => {
                Err(format_err!(
                    "scale_factor {} is not a `Normal` floating-point value",
                    scale_factor
                ))
            }
            FpCategory::Normal => {
                if scale_factor < 0.0 {
                    Err(format_err!("Inverting motion is not supported"))
                } else if scale_factor < 1.0 {
                    Err(format_err!("Down-scaling motion is not supported"))
                } else {
                    Ok(Rc::new(Self { scale_factor }))
                }
            }
        }
    }

    /// Scales `motion`, using the configuration in `self`.
    fn scale_motion(self: &Rc<Self>, motion: Position) -> Position {
        motion * self.scale_factor
    }

    /// Scales `wheel_delta`, using the configuration in `self`.
    fn scale_wheel_delta(
        self: &Rc<Self>,
        wheel_delta: Option<mouse_binding::WheelDelta>,
    ) -> Option<mouse_binding::WheelDelta> {
        match wheel_delta {
            None => None,
            Some(delta) => Some(mouse_binding::WheelDelta {
                raw_data: delta.raw_data,
                physical_pixel: match delta.physical_pixel {
                    None => {
                        // this should never reach as pointer_sensor_scale_handler should
                        // fill this field.
                        fx_log_err!("physical_pixel is none");
                        None
                    }
                    Some(pixel) => Some(self.scale_factor * pixel),
                },
            }),
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
        std::{cell::Cell, collections::HashSet},
        test_case::test_case,
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

    #[test_case(f32::NAN          => matches Err(_); "yields err for NaN scale")]
    #[test_case(f32::INFINITY     => matches Err(_); "yields err for pos infinite scale")]
    #[test_case(f32::NEG_INFINITY => matches Err(_); "yields err for neg infinite scale")]
    #[test_case(             -1.0 => matches Err(_); "yields err for neg scale")]
    #[test_case(              0.0 => matches Err(_); "yields err for pos zero scale")]
    #[test_case(             -0.0 => matches Err(_); "yields err for neg zero scale")]
    #[test_case(              0.5 => matches Err(_); "yields err for downscale")]
    #[test_case(              1.0 => matches Ok(_);  "yields handler for unit scale")]
    #[test_case(              1.5 => matches Ok(_);  "yields handler for upscale")]
    fn new(scale_factor: f32) -> Result<Rc<PointerDisplayScaleHandler>, Error> {
        PointerDisplayScaleHandler::new(scale_factor)
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn applies_scale_counts() {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 1.5, y: 4.5 },
                millimeters: Position::zero(),
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        });
        assert_matches!(
                   handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
                   [input_device::InputEvent {
                       device_event:
                           input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                               location:
                                   mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {counts: Position { x, y },   millimeters: _
        }),
                               ..
                           }),
                       ..
                   }] if *x == 3.0 && *y == 9.0
               );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn applies_scale_mm() {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position { x: 1.5, y: 4.5 },
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        });
        assert_matches!(
                   handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
                   [input_device::InputEvent {
                       device_event:
                           input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                               location:
                                   mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {millimeters: Position { x, y },   counts: _
        }),
                               ..
                           }),
                       ..
                   }] if *x == 3.0  && *y == 9.0
               );
    }

    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 1.5, y: 4.5 },
                millimeters: Position {
                    x: 1.5 / COUNTS_PER_MM,
                    y: 4.5 / COUNTS_PER_MM },
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "move event")]
    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "wheel event")]
    #[fuchsia::test(allow_stalls = false)]
    async fn does_not_consume(event: mouse_binding::MouseEvent) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(event);
        assert_matches!(
            handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::No, .. }]
        );
    }

    #[test_case(hashset! {       }; "empty buttons")]
    #[test_case(hashset! {      1}; "one button")]
    #[test_case(hashset! {1, 2, 3}; "multiple buttons")]
    #[fuchsia::test(allow_stalls = false)]
    async fn preserves_buttons_move_event(input_buttons: HashSet<u8>) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 1.5, y: 4.5 },
                millimeters: Position { x: 1.5 / COUNTS_PER_MM, y: 4.5 / COUNTS_PER_MM },
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: input_buttons.clone(),
            pressed_buttons: input_buttons.clone(),
            is_precision_scroll: None,
        });
        assert_matches!(
            handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
            [input_device::InputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent { affected_buttons, pressed_buttons, ..}),
                ..
            }] if *affected_buttons == input_buttons && *pressed_buttons == input_buttons
        );
    }

    #[test_case(hashset! {       }; "empty buttons")]
    #[test_case(hashset! {      1}; "one button")]
    #[test_case(hashset! {1, 2, 3}; "multiple buttons")]
    #[fuchsia::test(allow_stalls = false)]
    async fn preserves_buttons_wheel_event(input_buttons: HashSet<u8>) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: input_buttons.clone(),
            pressed_buttons: input_buttons.clone(),
            is_precision_scroll: None,
        });
        assert_matches!(
            handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
            [input_device::InputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent { affected_buttons, pressed_buttons, ..}),
                ..
            }] if *affected_buttons == input_buttons && *pressed_buttons == input_buttons
        );
    }

    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 1.5, y: 4.5 },
                millimeters: Position {
                    x: 1.5 / COUNTS_PER_MM,
                    y: 4.5 / COUNTS_PER_MM },
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "move event")]
    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "wheel event")]
    #[fuchsia::test(allow_stalls = false)]
    async fn preserves_descriptor(event: mouse_binding::MouseEvent) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(event);
        assert_matches!(
            handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
            [input_device::InputEvent { device_descriptor: DEVICE_DESCRIPTOR, .. }]
        );
    }

    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 1.5, y: 4.5 },
                millimeters: Position {
                    x: 1.5 / COUNTS_PER_MM,
                    y: 4.5 / COUNTS_PER_MM },
            }),
            wheel_delta_v: None,
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "move event")]
    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        }; "wheel event")]
    #[fuchsia::test(allow_stalls = false)]
    async fn preserves_event_time(event: mouse_binding::MouseEvent) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let mut input_event = make_unhandled_input_event(event);
        const EVENT_TIME: zx::Time = zx::Time::from_nanos(42);
        input_event.event_time = EVENT_TIME;
        assert_matches!(
            handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
            [input_device::InputEvent { event_time: EVENT_TIME, .. }]
        );
    }

    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: Some(mouse_binding::PrecisionScroll::No),
        } => matches input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                is_precision_scroll: Some(mouse_binding::PrecisionScroll::No),
                ..
            }),
            ..
        }; "no")]
    #[test_case(
        mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v: Some(mouse_binding::WheelDelta {
                raw_data: mouse_binding::RawWheelDelta::Ticks(1),
                physical_pixel: Some(1.0),
            }),
            wheel_delta_h: None,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: Some(mouse_binding::PrecisionScroll::Yes),
        } => matches input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                is_precision_scroll: Some(mouse_binding::PrecisionScroll::Yes),
                ..
            }),
            ..
        }; "yes")]
    #[fuchsia::test(allow_stalls = false)]
    async fn preserves_is_precision_scroll(
        event: mouse_binding::MouseEvent,
    ) -> input_device::InputEvent {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(event);

        handler.clone().handle_unhandled_input_event(input_event).await[0].clone()
    }

    #[test_case(
        Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Ticks(1),
            physical_pixel: Some(1.0),
        }),
        None => (Some(2.0), None); "v tick h none"
    )]
    #[test_case(
        None, Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Ticks(1),
            physical_pixel: Some(1.0),
        })  => (None, Some(2.0)); "v none h tick"
    )]
    #[test_case(
        Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Millimeters(1.0),
            physical_pixel: Some(1.0),
        }),
        None => (Some(2.0), None); "v mm h none"
    )]
    #[test_case(
        None, Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Millimeters(1.0),
            physical_pixel: Some(1.0),
        }) => (None, Some(2.0)); "v none h mm"
    )]
    #[fuchsia::test(allow_stalls = false)]
    async fn applied_scale_scroll_event(
        wheel_delta_v: Option<mouse_binding::WheelDelta>,
        wheel_delta_h: Option<mouse_binding::WheelDelta>,
    ) -> (Option<f32>, Option<f32>) {
        let handler = PointerDisplayScaleHandler::new(2.0).expect("failed to make handler");
        let input_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position::zero(),
                millimeters: Position::zero(),
            }),
            wheel_delta_v,
            wheel_delta_h,
            phase: mouse_binding::MousePhase::Wheel,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
            is_precision_scroll: None,
        });
        let events = handler.clone().handle_unhandled_input_event(input_event).await;
        assert_matches!(
            events.as_slice(),
            [input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(
                    mouse_binding::MouseEvent { .. }
                ),
                ..
            }]
        );
        if let input_device::InputEvent {
            device_event:
                input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                    wheel_delta_v,
                    wheel_delta_h,
                    ..
                }),
            ..
        } = events[0].clone()
        {
            match (wheel_delta_v, wheel_delta_h) {
                (None, None) => return (None, None),
                (None, Some(delta_h)) => return (None, delta_h.physical_pixel),
                (Some(delta_v), None) => return (delta_v.physical_pixel, None),
                (Some(delta_v), Some(delta_h)) => {
                    return (delta_v.physical_pixel, delta_h.physical_pixel)
                }
            }
        } else {
            unreachable!();
        }
    }
}
