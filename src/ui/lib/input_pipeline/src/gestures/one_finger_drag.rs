// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;

use crate::mouse_binding::MouseButton;

use {
    super::gesture_arena::{
        self, DetailedReasonUint, EndGestureEvent, ExamineEventResult, MouseEvent,
        ProcessBufferedEventsResult, ProcessNewEventResult, Reason, RecognizedGesture,
        TouchpadEvent, VerifyEventResult,
    },
    crate::mouse_binding,
    crate::utils::{euclidean_distance, Position},
    fuchsia_zircon as zx,
    maplit::hashset,
};

/// The initial state of this recognizer, before a finger contact has been detected.
#[derive(Debug)]
pub(super) struct InitialContender {
    /// The minimum movement in millimeters on surface to recognize as a motion.
    pub(super) min_movement_in_mm: f32,
}

/// The state when this recognizer has detected a finger pressed buttondown, before finger
/// movement > threshold.
#[derive(Debug)]
struct ButtonDownContender {
    /// The minimum movement in millimeters on surface to recognize as a motion.
    min_movement_in_mm: f32,

    /// The initial contact position on touchpad surface.
    initial_position: Position,
}

/// The state when this recognizer has detected a finger contact and a movement > threshold, but the
/// gesture arena has not declared this recognizer the winner.
#[derive(Debug)]
struct MatchedContender {}

/// The state when this recognizer has won the contest.
#[derive(Debug)]
struct Winner {
    /// The last contact position on touchpad surface.
    last_position: Position,
}

impl InitialContender {
    fn into_button_down_contender(
        self: Box<Self>,
        initial_position: Position,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(ButtonDownContender {
            min_movement_in_mm: self.min_movement_in_mm,
            initial_position,
        })
    }
}

impl gesture_arena::Contender for InitialContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 1 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(1),
                max: Some(1),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 1 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(1),
                max: Some(1),
                actual: num_pressed_buttons,
            }));
        }

        ExamineEventResult::Contender(self.into_button_down_contender(event.contacts[0].position))
    }
}

impl ButtonDownContender {
    fn into_matched_contender(self: Box<Self>) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {})
    }
}

impl gesture_arena::Contender for ButtonDownContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 1 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(1),
                max: Some(1),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 1 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(1),
                max: Some(1),
                actual: num_pressed_buttons,
            }));
        }

        let distance = euclidean_distance(event.contacts[0].position, self.initial_position);
        if distance > self.min_movement_in_mm {
            return ExamineEventResult::MatchedContender(self.into_matched_contender());
        }

        ExamineEventResult::Contender(self)
    }
}

impl MatchedContender {
    fn into_winner(self: Box<Self>, last_position: Position) -> Box<dyn gesture_arena::Winner> {
        Box::new(Winner { last_position })
    }
}

impl gesture_arena::MatchedContender for MatchedContender {
    fn verify_event(self: Box<Self>, event: &TouchpadEvent) -> VerifyEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 1 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(1),
                max: Some(1),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 1 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(1),
                max: Some(1),
                actual: num_pressed_buttons,
            }));
        }

        VerifyEventResult::MatchedContender(self)
    }

    fn process_buffered_events(
        self: Box<Self>,
        events: Vec<TouchpadEvent>,
    ) -> ProcessBufferedEventsResult {
        let mut mouse_events: Vec<MouseEvent> = Vec::new();
        let last_position = events[events.len() - 1].contacts[0].position.clone();

        let mut first_event = true;
        for pair in events.windows(2) {
            if first_event {
                first_event = false;
                mouse_events.push(touchpad_event_to_mouse_down_event(
                    &pair[0].contacts[0].position,
                    &pair[1],
                ));
            } else {
                mouse_events.push(touchpad_event_to_mouse_drag_event(
                    &pair[0].contacts[0].position,
                    &pair[1],
                ));
            }
        }

        ProcessBufferedEventsResult {
            generated_events: mouse_events,
            winner: Some(self.into_winner(last_position)),
            recognized_gesture: RecognizedGesture::OneFingerDrag,
        }
    }
}

impl gesture_arena::Winner for Winner {
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> ProcessNewEventResult {
        match u8::try_from(event.contacts.len()).unwrap_or(u8::MAX) {
            0 => ProcessNewEventResult::EndGesture(
                EndGestureEvent::GeneratedEvent(touchpad_event_to_mouse_up_event(
                    &self.last_position,
                    &event,
                )),
                Reason::DetailedUint(DetailedReasonUint {
                    criterion: "num_contacts",
                    min: Some(1),
                    max: Some(1),
                    actual: 0,
                }),
            ),
            1 => match u8::try_from(event.pressed_buttons.len()).unwrap_or(u8::MAX) {
                0 => ProcessNewEventResult::EndGesture(
                    EndGestureEvent::GeneratedEvent(touchpad_event_to_mouse_up_event(
                        &self.last_position,
                        &event,
                    )),
                    Reason::DetailedUint(DetailedReasonUint {
                        criterion: "num_buttons",
                        min: Some(1),
                        max: Some(1),
                        actual: 0,
                    }),
                ),
                1 => {
                    let last_position = event.contacts[0].position.clone();
                    ProcessNewEventResult::ContinueGesture(
                        Some(touchpad_event_to_mouse_drag_event(&self.last_position, &event)),
                        Box::new(Winner { last_position }),
                    )
                }
                num_buttons @ 2.. => ProcessNewEventResult::EndGesture(
                    EndGestureEvent::UnconsumedEvent(event),
                    Reason::DetailedUint(DetailedReasonUint {
                        criterion: "num_buttons",
                        min: Some(1),
                        max: Some(1),
                        actual: usize::from(num_buttons),
                    }),
                ),
            },
            num_contacts @ 2.. => ProcessNewEventResult::EndGesture(
                EndGestureEvent::UnconsumedEvent(event),
                Reason::DetailedUint(DetailedReasonUint {
                    criterion: "num_contacts",
                    min: Some(1),
                    max: Some(1),
                    actual: usize::from(num_contacts),
                }),
            ),
        }
    }
}

fn touchpad_event_to_mouse_down_event(
    last_position: &Position,
    event: &TouchpadEvent,
) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        Position {
            x: event.contacts[0].position.x - last_position.x,
            y: event.contacts[0].position.y - last_position.y,
        },
        mouse_binding::MousePhase::Down,
        hashset! {1},
        hashset! {1},
    )
}

fn touchpad_event_to_mouse_up_event(last_position: &Position, event: &TouchpadEvent) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        Position {
            x: event.contacts[0].position.x - last_position.x,
            y: event.contacts[0].position.y - last_position.y,
        },
        mouse_binding::MousePhase::Up,
        hashset! {1},
        hashset! {},
    )
}

fn touchpad_event_to_mouse_drag_event(
    last_position: &Position,
    event: &TouchpadEvent,
) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        Position {
            x: event.contacts[0].position.x - last_position.x,
            y: event.contacts[0].position.y - last_position.y,
        },
        mouse_binding::MousePhase::Move,
        hashset! {},
        hashset! {1},
    )
}

fn make_mouse_event(
    timestamp: zx::Time,
    position_in_mm: Position,
    phase: mouse_binding::MousePhase,
    affected_buttons: HashSet<MouseButton>,
    pressed_buttons: HashSet<MouseButton>,
) -> MouseEvent {
    MouseEvent {
        timestamp,
        mouse_data: mouse_binding::MouseEvent::new(
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: position_in_mm,
            }),
            /* wheel_delta_v= */ None,
            /* wheel_delta_h= */ None,
            phase,
            affected_buttons,
            pressed_buttons,
            /* is_precision_scroll= */ None,
        ),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::touch_binding, assert_matches::assert_matches,
        pretty_assertions::assert_eq, test_case::test_case,
    };

    fn make_touch_contact(id: u32, position: Position) -> touch_binding::TouchContact {
        touch_binding::TouchContact { id, position, pressure: None, contact_size: None }
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
    };"1 finger, no button down")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![],
    };"0 fingers")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0}),
        ],
    };"2 fingers")]
    #[fuchsia::test]
    fn initial_contender_examine_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> =
            Box::new(InitialContender { min_movement_in_mm: 10.0 });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[fuchsia::test]
    fn initial_contender_examine_event_button_down_contender() {
        let contender: Box<dyn gesture_arena::Contender> =
            Box::new(InitialContender { min_movement_in_mm: 10.0 });
        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
        };

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
    };"1 finger, no button down")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![],
    };"0 fingers")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0}),
            ],
    };"2 fingers")]
    #[fuchsia::test]
    fn button_down_contender_examine_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(ButtonDownContender {
            min_movement_in_mm: 10.0,
            initial_position: Position { x: 1.0, y: 1.0 },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[test_case(TouchpadEvent{timestamp: zx::Time::ZERO,
         pressed_buttons: vec![1],
        contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
    };"finger hold")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![1],
        contacts: vec![make_touch_contact(1, Position{x: 5.0, y: 5.0})],
    };"finger move less than threshold")]
    #[fuchsia::test]
    fn button_down_contender_examine_event_button_down_contender(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(ButtonDownContender {
            min_movement_in_mm: 10.0,
            initial_position: Position { x: 1.0, y: 1.0 },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[fuchsia::test]
    fn button_down_contender_examine_event_matched_contender() {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(ButtonDownContender {
            min_movement_in_mm: 10.0,
            initial_position: Position { x: 1.0, y: 1.0 },
        });
        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position { x: 11.0, y: 12.0 })],
        };
        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::MatchedContender(_));
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
    };"1 finger, but no button down")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![],
    };"0 fingers")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0}),
        ],
    };"2 fingers")]
    #[fuchsia::test]
    fn matched_contender_verify_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {});

        let got = contender.verify_event(&event);
        assert_matches!(got, VerifyEventResult::Mismatch(_));
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![1],
        contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
    };"finger hold")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![1],
        contacts: vec![make_touch_contact(1, Position{x: 5.0, y: 5.0})],
    };"finger move")]
    #[fuchsia::test]
    fn matched_contender_verify_event_matched_contender(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {});

        let got = contender.verify_event(&event);
        assert_matches!(got, VerifyEventResult::MatchedContender(_));
    }

    #[fuchsia::test]
    fn matched_contender_process_buffered_events() {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {});

        let got = contender.process_buffered_events(vec![
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(1),
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(2),
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 5.0, y: 6.0 })],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(3),
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 10.0, y: 12.0 })],
            },
        ]);

        assert_eq!(
            got.generated_events,
            vec![
                MouseEvent {
                    timestamp: zx::Time::from_nanos(2),
                    mouse_data: mouse_binding::MouseEvent::new(
                        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 4.0, y: 5.0 },
                        }),
                        /* wheel_delta_v= */ None,
                        /* wheel_delta_h= */ None,
                        mouse_binding::MousePhase::Down,
                        /* affected_buttons= */ hashset! {1},
                        /* pressed_buttons= */ hashset! {1},
                        /* is_precision_scroll= */ None,
                    ),
                },
                MouseEvent {
                    timestamp: zx::Time::from_nanos(3),
                    mouse_data: mouse_binding::MouseEvent::new(
                        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 5.0, y: 6.0 },
                        }),
                        /* wheel_delta_v= */ None,
                        /* wheel_delta_h= */ None,
                        mouse_binding::MousePhase::Move,
                        /* affected_buttons= */ hashset! {},
                        /* pressed_buttons= */ hashset! {1},
                        /* is_precision_scroll= */ None,
                    ),
                },
            ]
        );
        assert_eq!(got.recognized_gesture, RecognizedGesture::OneFingerDrag);
    }

    #[fuchsia::test]
    fn winner_process_new_event_end_gesture_generated_event() {
        let winner: Box<dyn gesture_arena::Winner> =
            Box::new(Winner { last_position: Position { x: 1.0, y: 1.0 } });
        let event = TouchpadEvent {
            timestamp: zx::Time::from_nanos(2),
            pressed_buttons: vec![],
            contacts: vec![make_touch_contact(1, Position { x: 5.0, y: 5.0 })],
        };
        let got = winner.process_new_event(event);

        match got {
            ProcessNewEventResult::EndGesture(
                EndGestureEvent::GeneratedEvent(got_mouse_event),
                _reason,
            ) => {
                assert_eq!(
                    got_mouse_event,
                    MouseEvent {
                        timestamp: zx::Time::from_nanos(2),
                        mouse_data: mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation {
                                    counts: Position { x: 0.0, y: 0.0 },
                                    millimeters: Position { x: 4.0, y: 4.0 },
                                }
                            ),
                            wheel_delta_v: None,
                            wheel_delta_h: None,
                            phase: mouse_binding::MousePhase::Up,
                            affected_buttons: hashset! {1},
                            pressed_buttons: hashset! {},
                            is_precision_scroll: None,
                        },
                    }
                );
            }
            _ => panic!("want EndGesture(GeneratedEvent), got {:?}", got),
        }
    }

    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1, 2],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 1.0}),
                make_touch_contact(2, Position{x: 5.0, y: 5.0}),
            ],
        };"2 button down")]
    #[fuchsia::test]
    fn winner_process_new_event_end_gesture_unconsumed_event(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> =
            Box::new(Winner { last_position: Position { x: 1.0, y: 1.0 } });
        let got = winner.process_new_event(event);

        assert_matches!(
            got,
            ProcessNewEventResult::EndGesture(EndGestureEvent::UnconsumedEvent(_), _reason)
        );
    }

    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::from_nanos(2),
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})]
        },
        Position {x:0.0, y:0.0}; "finger hold")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::from_nanos(2),
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position{x: 5.0, y: 6.0})]
        },
        Position {x:4.0, y:5.0};"finger moved")]
    #[fuchsia::test]
    fn winner_process_new_event_continue_gesture(event: TouchpadEvent, want_position: Position) {
        let winner: Box<dyn gesture_arena::Winner> =
            Box::new(Winner { last_position: Position { x: 1.0, y: 1.0 } });
        let got = winner.process_new_event(event);

        // This not able to use `assert_eq` or `assert_matches` because:
        // - assert_matches: floating point is not allow in match.
        // - assert_eq: `ContinueGesture` has Box dyn type.
        match got {
            ProcessNewEventResult::EndGesture(..) => {
                panic!("Got {:?}, want ContinueGesture()", got)
            }
            ProcessNewEventResult::ContinueGesture(got_mouse_event, _) => {
                pretty_assertions::assert_eq!(
                    got_mouse_event.unwrap(),
                    MouseEvent {
                        timestamp: zx::Time::from_nanos(2),
                        mouse_data: mouse_binding::MouseEvent {
                            location: mouse_binding::MouseLocation::Relative(
                                mouse_binding::RelativeLocation {
                                    counts: Position { x: 0.0, y: 0.0 },
                                    millimeters: want_position,
                                }
                            ),
                            wheel_delta_v: None,
                            wheel_delta_h: None,
                            phase: mouse_binding::MousePhase::Move,
                            affected_buttons: hashset! {},
                            pressed_buttons: hashset! {1},
                            is_precision_scroll: None,
                        },
                    }
                );
            }
        }
    }
}
