// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_device, input_handler::UnhandledInputHandler, mouse_binding, utils::Position},
    async_trait::async_trait,
    fuchsia_syslog::{fx_log_debug, fx_log_warn},
    fuchsia_zircon as zx,
    std::{
        cell::{Cell, RefCell},
        collections::HashSet,
        convert::{From, TryFrom},
        rc::Rc,
    },
};

/// Represents an InputEvent that is relevant to this handler.
/// Notably excludes absolutely positioned pointers, and events
/// not associated with a mouse device.
#[derive(Debug)]
struct RelativeMouseEvent {
    displacement: Position, // Change in position since the previous event
    phase: mouse_binding::MousePhase,
    affected_buttons: HashSet<mouse_binding::MouseButton>,
    pressed_buttons: HashSet<mouse_binding::MouseButton>,
    mouse_descriptor: mouse_binding::MouseDeviceDescriptor,
    event_time: zx::Time,
    handled: input_device::Handled,
}

impl TryFrom<input_device::UnhandledInputEvent> for RelativeMouseEvent {
    type Error = input_device::UnhandledInputEvent;
    fn try_from(
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Result<RelativeMouseEvent, Self::Error> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event:
                    input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                        location: mouse_binding::MouseLocation::Relative(position),
                        phase,
                        affected_buttons,
                        pressed_buttons,
                    }),
                device_descriptor: input_device::InputDeviceDescriptor::Mouse(mouse_descriptor),
                event_time,
            } => Ok(RelativeMouseEvent {
                displacement: position,
                phase,
                affected_buttons,
                pressed_buttons,
                mouse_descriptor,
                event_time,
                handled: input_device::Handled::No,
            }),
            _ => Err(unhandled_input_event),
        }
    }
}

impl From<RelativeMouseEvent> for input_device::InputEvent {
    fn from(relative_mouse_event: RelativeMouseEvent) -> input_device::InputEvent {
        input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(relative_mouse_event.displacement),
                phase: relative_mouse_event.phase.into(),
                affected_buttons: relative_mouse_event.affected_buttons,
                pressed_buttons: relative_mouse_event.pressed_buttons,
            }),
            device_descriptor: input_device::InputDeviceDescriptor::Mouse(
                relative_mouse_event.mouse_descriptor,
            ),
            event_time: relative_mouse_event.event_time,
            handled: relative_mouse_event.handled,
        }
    }
}

/// States in the disambiguation process.
///
/// | Current state    | Description                         | Event            | Next state       |
/// | ---------------- | ----------------------------------- | ---------------- | ---------------- |
/// | NoActiveGesture  | Button not actuated                 | Down             | AmbiguousGesture |
/// | NoActiveGesture  | (as above)                          | Move             | NoActiveGesture  |
/// | NoActiveGesture  | (as above)                          | Up               | NoActiveGesture  |
/// | AmbiguousGesture | Button actuated, motion < threshold | Down             | AmbiguousGesture |
/// | AmbiguousGesture | (as above)                          | Move < threshold | AmbiguousGesture |
/// | AmbiguousGesture | (as above)                          | Move > threshold | DragGesture      |
/// | AmbiguousGesture | (as above)                          | Up               | ClickGesture     |
/// | DragGesture      | Button actuated, motion > threshold | Down             | DragGesture      |
/// | DragGesture      | (as above)                          | Move             | DragGesture      |
/// | DragGesture      | (as above)                          | Up               | NoActiveGesture  |
///
/// Notes
/// 1. "Move < threshold" is short-hand for "a Move event after which the total displacement
///    remains below the threshold for distinguishing a click from a drag". Similarly for
///    "Move > threshold". Said differently, the comparison against the threshold is for
///    the _total displacement_, not just the displacement in the current event.
/// 2. The `ClickGesture` state is omitted in the table above, as it is functionally
///    identical to the `NoActiveGesture` state; it (`ClickGesture`) exists just to make
///    state transition logs easy to read.
enum HandlerState {
    NoActiveGesture,
    AmbiguousGesture {
        delta_x: f32, // Net horizontal displacement since a button was depressed
        delta_y: f32, // Net vertical displacement since a button was depressed
        // Events buffered pending disambiguation of click vs. drag
        // TODO(fxbug.dev/89727): Limit the size of this buffer.
        buffered_events: Vec<RelativeMouseEvent>,
    },
    ClickGesture,
    DragGesture {
        // Net horizontal displacement since a button was depressed. Tracked
        // for debugging purposes.
        delta_x: f32,
        // Net vertical displacement since a button was depressed. Tracked
        // for debugging purposes.
        delta_y: f32,
    },
    LogicError,
}

/// Names for the `HandlerState`s. Exists to support the state machine debug log.
#[derive(Debug, PartialEq)]
enum HandlerStateName {
    NoActiveGesture,
    AmbiguousGesture,
    ClickGesture,
    DragGesture,
    LogicError,
}

impl HandlerState {
    fn get_state_name(&self) -> HandlerStateName {
        match self {
            Self::NoActiveGesture => HandlerStateName::NoActiveGesture,
            Self::AmbiguousGesture { .. } => HandlerStateName::AmbiguousGesture,
            Self::ClickGesture => HandlerStateName::ClickGesture,
            Self::DragGesture { .. } => HandlerStateName::DragGesture,
            Self::LogicError { .. } => HandlerStateName::LogicError,
        }
    }
}

/// Improves the disambiguation of click vs. drag events at upper layers
/// by suppressing spurious motion that happens when clicking on a trackpad.
///
// TODO(fxbug.dev/89726) Define semantics with multiple mouse buttons.
// TODO(fxbug.dev/89724) Define semantics with multiple mouse/trackpad devices.
// TODO(fxbug.dev/89722) Process trackpad events instead of mouse events.
// TODO(fxbug.dev/90512) Switch to a gesture arena.
pub struct ClickDragHandler {
    state: RefCell<HandlerState>,
    // The time at which we entered the current state. Tracked
    // for debugging purposes.
    entry_time: Cell<std::time::Instant>,
    // The number of events processed in the the current state. Tracked
    // for debugging purposes.
    event_count: Cell<u64>,
    // Threshold Euclidean distance at which an ambiguous gesture
    // is resolved to be a drag.
    click_to_drag_threshold: f32,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for ClickDragHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match RelativeMouseEvent::try_from(unhandled_input_event) {
            Ok(converted_event) => self
                .process_event(converted_event)
                .into_iter()
                .map(input_device::InputEvent::from)
                .collect(),
            Err(original_event) => vec![input_device::InputEvent::from(original_event)],
        }
    }
}

impl ClickDragHandler {
    /// Creates a new [`ClickDragHandler`] that disambiguates clicks vs. drags.
    pub fn new(click_to_drag_threshold: f32) -> Rc<Self> {
        Rc::new(Self {
            state: RefCell::new(HandlerState::NoActiveGesture),
            entry_time: Cell::new(std::time::Instant::now()),
            event_count: Cell::new(0),
            click_to_drag_threshold,
        })
    }

    /// Interprets the `event` in the context of `self.state`.
    fn process_event(self: &Rc<Self>, event: RelativeMouseEvent) -> Vec<RelativeMouseEvent> {
        // The state machine update logic does not update total displacement
        // from Down or Up events, as those are expected to have zero displacement
        // (see MouseBinding::process_reports(), which generates those events).
        if event.displacement != Position::zero() && event.phase != mouse_binding::MousePhase::Move
        {
            fx_log_warn!("non-zero displacement in phase {:?}", event.phase)
        }

        // Take the current state out of `self.state`, and leave a placeholder in
        // `self.state`. This avoids the cost of `clone()`-ing `buffered_events`.
        let old_state = self.state.replace(HandlerState::LogicError);
        let old_state_name = old_state.get_state_name();
        let (
            new_state,
            events_to_propagate,
            // The net displacement in the x and y axes at the end of a click or
            // drag gesture. `None` in cases where the event did not terminate
            // a gesture. Tracked for debugging purposes.
            completed_gesture_displacement,
        ) = match (old_state, event.phase) {
            (
                HandlerState::NoActiveGesture | HandlerState::ClickGesture,
                mouse_binding::MousePhase::Down,
            ) => (
                HandlerState::AmbiguousGesture {
                    delta_x: 0.0,
                    delta_y: 0.0,
                    buffered_events: vec![],
                },
                vec![event],
                None,
            ),
            (
                HandlerState::NoActiveGesture | HandlerState::ClickGesture,
                mouse_binding::MousePhase::Move,
            ) => (HandlerState::NoActiveGesture, vec![event], None),
            (
                HandlerState::NoActiveGesture | HandlerState::ClickGesture,
                mouse_binding::MousePhase::Up,
            ) => {
                fx_log_warn!(
                    "mouse_binding::MousePhase::Up with no button pressed; remaining in NoActiveGesture state"
                );
                (HandlerState::NoActiveGesture, vec![event], None)
            }
            (state @ HandlerState::AmbiguousGesture { .. }, mouse_binding::MousePhase::Down) => {
                fx_log_warn!(
                    "mouse_binding::MousePhase::Down with gesture in progress; remaining in AmbiguousGesture state"
                );
                (state, vec![event], None)
            }
            (
                HandlerState::AmbiguousGesture { delta_x, delta_y, mut buffered_events },
                mouse_binding::MousePhase::Move,
            ) => {
                let (delta_x, delta_y) =
                    (delta_x + event.displacement.x, delta_y + event.displacement.y);

                if self.motion_is_in_click_threshold(delta_x, delta_y) {
                    // The gesture remains ambiguous.
                    //
                    // Buffer the event, to avoid having upper layers interpret accidental
                    // motion as a drag.
                    (
                        HandlerState::AmbiguousGesture {
                            delta_x,
                            delta_y,
                            buffered_events: {
                                // While this could be written in a functional style, doing
                                // so might make this a best-case O(n) operation. With
                                // the imperative style, this is amortized O(1).
                                //
                                // This is important, because some mice run at 8KHz. If a user
                                // with an 8KHz mouse held the button for 1 second, O(n) would
                                // mean copying 8000 * ((8000+1)/2) `RelativeMouseEvent`s.
                                //
                                // That works out to 8000 * 4000 * 128 = 4 GB/sec, which could
                                // make mouse bottlenecked on memory.
                                buffered_events.push(event);
                                buffered_events
                            },
                        },
                        vec![],
                        None,
                    )
                } else {
                    // The gesture isn't a click, so it must be a drag.
                    (
                        HandlerState::DragGesture { delta_x, delta_y },
                        // Send the buffered events now, so upper layers can start
                        // responding to the drag.
                        {
                            buffered_events.push(event);
                            buffered_events
                        },
                        None,
                    )
                }
            }
            (
                HandlerState::AmbiguousGesture { delta_x, delta_y, buffered_events },
                mouse_binding::MousePhase::Up,
            ) => (
                HandlerState::ClickGesture,
                buffered_events
                    .into_iter()
                    .map(|e| RelativeMouseEvent { handled: input_device::Handled::Yes, ..e })
                    .chain(std::iter::once(event))
                    .collect(),
                Some((delta_x, delta_y)),
            ),
            (state @ HandlerState::DragGesture { .. }, mouse_binding::MousePhase::Down) => {
                fx_log_warn!("mouse_binding::MousePhase::Down with drag in progress; remaining in DragGesture state");
                (state, vec![event], None)
            }
            (HandlerState::DragGesture { delta_x, delta_y }, mouse_binding::MousePhase::Move) => (
                HandlerState::DragGesture {
                    delta_x: delta_x + event.displacement.x,
                    delta_y: delta_y + event.displacement.y,
                },
                vec![event],
                None,
            ),
            (HandlerState::DragGesture { delta_x, delta_y }, mouse_binding::MousePhase::Up) => {
                (HandlerState::NoActiveGesture, vec![event], Some((delta_x, delta_y)))
            }
            (HandlerState::LogicError, _) => unreachable!(),
        };

        if new_state.get_state_name() != old_state_name {
            let elapsed_time = std::time::Instant::now() - self.entry_time.get();
            fx_log_debug!(
                "ClickDragHandler: {:?} -> {:?} after {}.{:03} secs, {} events, {:?} displacement",
                old_state_name,
                new_state.get_state_name(),
                elapsed_time.as_secs(),
                elapsed_time.subsec_millis(),
                self.event_count.get(),
                completed_gesture_displacement
            );
            self.entry_time.set(std::time::Instant::now());
            self.event_count.set(0);
        } else {
            self.event_count.set(self.event_count.get() + 1);
        }

        self.state.replace(new_state);
        events_to_propagate
    }
}

impl ClickDragHandler {
    /// Returns true iff the Euclidean distance for the displacement indicated
    /// by (delta_x, delta_y) is less than the drag threshold.
    fn motion_is_in_click_threshold(&self, delta_x: f32, delta_y: f32) -> bool {
        (delta_x * delta_x + delta_y * delta_y).sqrt() < self.click_to_drag_threshold
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_zircon as zx, maplit::hashset, pretty_assertions::assert_eq,
        test_case::test_case,
    };

    const DEVICE_DESCRIPTOR: input_device::InputDeviceDescriptor =
        input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
            device_id: 0,
            absolute_x_range: None,
            absolute_y_range: None,
            buttons: None,
        });
    const CLICK_TO_DRAG_THRESHOLD: f32 = 16.0;
    const SMALL_MOTION: f32 = CLICK_TO_DRAG_THRESHOLD * 0.1;
    const HALF_MOTION: f32 = CLICK_TO_DRAG_THRESHOLD / 2.0;
    const LARGE_MOTION: f32 = CLICK_TO_DRAG_THRESHOLD * 1.2;
    const DIAGONAL_LARGE_MOTION: f32 = LARGE_MOTION / 2.0 * std::f32::consts::SQRT_2;

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
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn button_down_is_passed_through_when_no_button_was_previously_clicked() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        assert_eq!(
            handler.handle_unhandled_input_event(event.clone()).await.as_slice(),
            [event.into()]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn move_event_is_passed_through_when_no_button_is_clicked() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position {
                x: SMALL_MOTION,
                y: SMALL_MOTION,
            }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {},
            pressed_buttons: hashset! {},
        });
        assert_eq!(
            handler.handle_unhandled_input_event(event.clone()).await.as_slice(),
            [event.into()]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn button_down_then_small_motion_yields_no_move_events() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: SMALL_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        assert_eq!(handler.clone().handle_unhandled_input_event(move_event).await.as_slice(), []);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn button_down_then_small_motion_then_button_up_yields_handled_move_and_unhandled_button_up(
    ) {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: SMALL_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let button_up_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Up,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        // * button_down_then_small_motion_yields_no_move_events()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        handler.clone().handle_unhandled_input_event(move_event.clone()).await;
        assert_eq!(
            handler.clone().handle_unhandled_input_event(button_up_event.clone()).await.as_slice(),
            [
                input_device::InputEvent {
                    handled: input_device::Handled::Yes,
                    ..move_event.into()
                },
                button_up_event.into()
            ]
        );
    }

    #[test_case(Position { x: -LARGE_MOTION, y: 0.0 }; "leftwards")]
    #[test_case(Position { x: LARGE_MOTION, y: 0.0 }; "rightwards")]
    #[test_case(Position { x: 0.0, y: -LARGE_MOTION }; "upwards")]
    #[test_case(Position { x: 0.0, y: LARGE_MOTION }; "downwards")]
    #[test_case(Position { x: DIAGONAL_LARGE_MOTION, y: DIAGONAL_LARGE_MOTION }; "diagonal")]
    #[fuchsia::test(allow_stalls = false)]
    async fn button_down_then_large_motion_yields_large_motion(position: Position) {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(position),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        pretty_assertions::assert_eq!(
            handler.clone().handle_unhandled_input_event(move_event.clone()).await.as_slice(),
            [move_event.into()]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn button_up_after_botton_down_and_large_motion_does_not_replay_motion() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: LARGE_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let button_up_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Up,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        // * button_down_then_large_motion_yields_large_motion()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        handler.clone().handle_unhandled_input_event(move_event.clone()).await;
        assert_eq!(
            handler.clone().handle_unhandled_input_event(button_up_event.clone()).await.as_slice(),
            [button_up_event.into()]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn button_down_then_two_motions_summing_past_drag_threshold_yields_motions() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let first_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: HALF_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let second_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: HALF_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        // * button_down_then_small_motion_yields_no_move_events()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        handler.clone().handle_unhandled_input_event(first_move_event.clone()).await;

        // In the future, it might be necessary to compress the events to save
        // memory. If that happens, the logic below will need to validate the total
        // displacement, instead of each event.
        assert_eq!(
            handler
                .clone()
                .handle_unhandled_input_event(second_move_event.clone())
                .await
                .as_slice(),
            [first_move_event.into(), second_move_event.into()]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn move_events_continue_after_drag_is_recognized() {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let first_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: HALF_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let second_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: HALF_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let third_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position { x: 0.0, y: HALF_MOTION }),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        // * button_down_then_two_motions_summing_past_drag_threshold_yields_motions()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        handler.clone().handle_unhandled_input_event(first_move_event.clone()).await;
        handler.clone().handle_unhandled_input_event(second_move_event.clone()).await;
        assert_eq!(
            handler.clone().handle_unhandled_input_event(third_move_event.clone()).await.as_slice(),
            [third_move_event.into()]
        );
    }

    #[test_case(Position { x: -2.0, y: 0.0 },
                    Position { x: CLICK_TO_DRAG_THRESHOLD + 1.0, y: 0.0}; "horizontal")]
    #[test_case(Position { x: 0.0, y: -2.0 },
                    Position { x: 0.0, y: CLICK_TO_DRAG_THRESHOLD + 1.0}; "vertical")]
    #[fuchsia::test(allow_stalls = false)]
    async fn back_and_forth_motion_does_not_spuriously_yield_move_events(
        first_motion: Position,
        second_motion: Position,
    ) {
        let handler = ClickDragHandler::new(CLICK_TO_DRAG_THRESHOLD);
        let button_down_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(Position::zero()),
            phase: mouse_binding::MousePhase::Down,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let first_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(first_motion),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });
        let second_move_event = make_unhandled_input_event(mouse_binding::MouseEvent {
            location: mouse_binding::MouseLocation::Relative(second_motion),
            phase: mouse_binding::MousePhase::Move,
            affected_buttons: hashset! {0},
            pressed_buttons: hashset! {0},
        });

        // Intermediate values verified by
        // * button_down_is_passed_through_when_no_button_was_previously_clicked()
        // * button_down_then_small_motion_yields_no_move_events()
        handler.clone().handle_unhandled_input_event(button_down_event).await;
        handler.clone().handle_unhandled_input_event(first_move_event.clone()).await;
        pretty_assertions::assert_eq!(
            handler.clone().handle_unhandled_input_event(second_move_event).await.as_slice(),
            []
        );
    }
}
