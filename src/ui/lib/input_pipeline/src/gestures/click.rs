// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::gesture_arena::{
        self, DetailedReasonFloat, DetailedReasonUint, ExamineEventResult,
        ProcessBufferedEventsResult, Reason, RecognizedGesture, TouchpadEvent, VerifyEventResult,
    },
    crate::mouse_binding::{MouseEvent, MouseLocation, MousePhase, RelativeLocation},
    crate::utils::{euclidean_distance, Position},
    std::collections::HashSet,
};

/// The initial state of this recognizer, before a click has been detected.
#[derive(Debug)]
pub(super) struct InitialContender {
    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a click. Measured in millimeters.
    pub(super) max_finger_displacement_in_mm: f32,
}

impl InitialContender {
    fn into_unpressed_contender(
        self: Box<Self>,
        initial_position: Position,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(UnpressedContender {
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            initial_position,
        })
    }

    fn into_pressed_contender(
        self: Box<Self>,
        pressed_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(PressedContender {
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            pressed_event,
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
        match num_pressed_buttons {
            0 => ExamineEventResult::Contender(
                self.into_unpressed_contender(position_from_event(event)),
            ),
            1 => ExamineEventResult::Contender(self.into_pressed_contender(event.clone())),
            // More than one button is a Mismatch for now, but in practice
            // we do not expect to receive more than one button from the
            // touchpad driver.
            _ => ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(1),
                actual: num_pressed_buttons,
            })),
        }
    }
}

/// The state when this recognizer has detected a single finger, but not a
/// button press or release.
#[derive(Debug)]
struct UnpressedContender {
    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a click. Measured in millimeters.
    max_finger_displacement_in_mm: f32,

    /// The position of the initial single touch contact.
    initial_position: Position,
}

impl UnpressedContender {
    fn into_pressed_contender(
        self: Box<Self>,
        pressed_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(PressedContender {
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            pressed_event,
        })
    }
}

impl gesture_arena::Contender for UnpressedContender {
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

        let displacement_mm = euclidean_distance(position_from_event(event), self.initial_position);
        if displacement_mm >= self.max_finger_displacement_in_mm {
            return ExamineEventResult::Mismatch(Reason::DetailedFloat(DetailedReasonFloat {
                criterion: "displacement_mm",
                min: None,
                max: Some(self.max_finger_displacement_in_mm),
                actual: displacement_mm,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        match num_pressed_buttons {
            0 => ExamineEventResult::Contender(self),
            1 => ExamineEventResult::Contender(self.into_pressed_contender(event.clone())),
            _ => ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(1),
                actual: num_pressed_buttons,
            })),
        }
    }
}

/// The state when this recognizer has detected a single finger and button
/// press, but not a button release.
#[derive(Debug)]
struct PressedContender {
    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a click. Measured in millimeters.
    max_finger_displacement_in_mm: f32,

    /// The TouchpadEvent when a button was first pressed.
    pressed_event: TouchpadEvent,
}

impl PressedContender {
    fn into_matched_contender(
        self: Box<Self>,
        released_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender { pressed_event: self.pressed_event, released_event })
    }
}

impl gesture_arena::Contender for PressedContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_contacts = event.contacts.len();
        match num_contacts {
            0 => ExamineEventResult::MatchedContender(self.into_matched_contender(event.clone())),
            1 => {
                let displacement_mm = euclidean_distance(
                    position_from_event(event),
                    position_from_event(&self.pressed_event),
                );
                if displacement_mm >= self.max_finger_displacement_in_mm {
                    return ExamineEventResult::Mismatch(Reason::DetailedFloat(
                        DetailedReasonFloat {
                            criterion: "displacement_mm",
                            min: None,
                            max: Some(self.max_finger_displacement_in_mm),
                            actual: displacement_mm,
                        },
                    ));
                }
                let num_pressed_buttons = event.pressed_buttons.len();
                match num_pressed_buttons {
                    0 => ExamineEventResult::MatchedContender(
                        self.into_matched_contender(event.clone()),
                    ),
                    1 => ExamineEventResult::Contender(self),
                    _ => ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                        criterion: "num_pressed_buttons",
                        min: Some(0),
                        max: Some(1),
                        actual: num_pressed_buttons,
                    })),
                }
            }
            _ => ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(0),
                max: Some(1),
                actual: num_contacts,
            })),
        }
    }
}

/// The state when this recognizer has detected a click, but the gesture arena
/// has not declared this recognizer the winner.
#[derive(Debug)]
struct MatchedContender {
    /// The TouchpadEvent when a button was first pressed.
    pressed_event: TouchpadEvent,

    /// The Touchpadevent when a button was released.
    released_event: TouchpadEvent,
}

impl gesture_arena::MatchedContender for MatchedContender {
    fn verify_event(self: Box<Self>, event: &TouchpadEvent) -> VerifyEventResult {
        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons > 0 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: None,
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        // Note: One potential pitfall is that this MatchedContender
        // may verify tap-like events which alternate reporting 0 and 1 touch
        // contacts.
        let num_contacts = event.contacts.len();
        if num_contacts > 1 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: None,
                max: Some(1),
                actual: num_contacts,
            }));
        }

        VerifyEventResult::MatchedContender(self)
    }

    fn process_buffered_events(
        self: Box<Self>,
        _events: Vec<TouchpadEvent>,
    ) -> ProcessBufferedEventsResult {
        ProcessBufferedEventsResult {
            generated_events: vec![
                gesture_arena::MouseEvent {
                    timestamp: self.pressed_event.timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position::zero(),
                            millimeters: Position::zero(),
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Down,
                        affected_buttons: HashSet::from_iter(
                            self.pressed_event.pressed_buttons.clone(),
                        ),
                        pressed_buttons: HashSet::from_iter(
                            self.pressed_event.pressed_buttons.clone(),
                        ),
                        is_precision_scroll: None,
                    },
                },
                gesture_arena::MouseEvent {
                    timestamp: self.released_event.timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position::zero(),
                            millimeters: Position::zero(),
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Up,
                        affected_buttons: HashSet::from_iter(
                            self.pressed_event.pressed_buttons.clone(),
                        ),
                        pressed_buttons: HashSet::from_iter(
                            self.released_event.pressed_buttons.clone(),
                        ),
                        is_precision_scroll: None,
                    },
                },
            ],
            winner: None,
            recognized_gesture: RecognizedGesture::Click,
        }
    }
}

/// This function returns the position associated with a TouchpadEvent that is
/// assumed to have a single associated TouchContact.
fn position_from_event(event: &TouchpadEvent) -> Position {
    event.contacts[0].position
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::gestures::gesture_arena::{Contender, MatchedContender as _, TouchpadEvent},
        crate::testing_utilities::create_touch_contact,
        assert_matches::assert_matches,
        fuchsia_zircon as zx,
        maplit::hashset,
        std::any::TypeId,
    };

    const MAX_FINGER_DISPLACEMENT_IN_MM: f32 = 10.0;
    const HALF_MOTION: f32 = MAX_FINGER_DISPLACEMENT_IN_MM / 2.0;

    fn assert_contender_matches_type_id(result: ExamineEventResult, type_id: TypeId) {
        match result {
            ExamineEventResult::Contender(boxed) => {
                assert_eq!((&*boxed).as_any().type_id(), type_id);
            }
            other => panic!("Expected a Contender but found {:?}", other),
        }
    }

    fn assert_examined_matched_contender(result: ExamineEventResult) {
        match result {
            ExamineEventResult::MatchedContender(boxed) => {
                assert_eq!((&*boxed).as_any().type_id(), TypeId::of::<MatchedContender>());
            }
            other => panic!("Expected a MatchedContender but found {:?}", other),
        }
    }

    fn assert_verified_matched_contender(result: VerifyEventResult) {
        match result {
            VerifyEventResult::MatchedContender(boxed) => {
                assert_eq!((&*boxed).as_any().type_id(), TypeId::of::<MatchedContender>());
            }
            other => panic!("Expected a MatchedContender but found {:?}", other),
        }
    }

    /// Tests that an InitialContender with zero touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn contender_no_touch_contacts() {
        assert_matches!(
            Box::new(InitialContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an InitialContender with multiple touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn contender_many_touch_contacts() {
        assert_matches!(
            Box::new(InitialContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an InitialContender with a single touch contact and no
    /// pressed buttons yields an UnpressedContender.
    #[fuchsia::test]
    async fn contender_no_buttons() {
        assert_contender_matches_type_id(
            Box::new(InitialContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<UnpressedContender>(),
        );
    }

    /// Tests that an InitialContender with a single touch contact and one
    /// pressed button yields a PressedContender.
    #[fuchsia::test]
    async fn contender_single_button() {
        assert_contender_matches_type_id(
            Box::new(InitialContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            TypeId::of::<PressedContender>(),
        );
    }

    /// Tests that an InitialContender with a single touch contact and multiple
    /// pressed button yields a Mismatch.
    #[fuchsia::test]
    async fn contender_many_buttons() {
        assert_matches!(
            Box::new(InitialContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            },),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an UnpressedContender with zero touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn unpressed_contender_no_touch_contacts() {
        assert_matches!(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an UnpressedContender with multiple touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn unpressed_contender_many_touch_contacts() {
        assert_matches!(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an UnpressedContender with a single touch contact and
    /// too much displacement yields a Mismatch.
    #[fuchsia::test]
    async fn unpressed_contender_large_displacement() {
        assert_matches!(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: MAX_FINGER_DISPLACEMENT_IN_MM, y: MAX_FINGER_DISPLACEMENT_IN_MM }
                ),],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an UnpressedContender with a single touch contact,
    /// acceptable displacement, and no pressed buttons yields an
    /// UnpressedContender.
    #[fuchsia::test]
    async fn unpressed_contender_no_buttons() {
        assert_contender_matches_type_id(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<UnpressedContender>(),
        );
    }

    /// Tests that an UnpressedContender with a single touch contact,
    /// acceptable displacement, and one pressed button yields a
    /// PressedContender.
    #[fuchsia::test]
    async fn unpressed_contender_single_button() {
        assert_contender_matches_type_id(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            TypeId::of::<PressedContender>(),
        );
    }

    /// Tests that an UnpressedContender with a single touch contact,
    /// acceptable displacement, and multiple pressed buttons yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn unpressed_contender_many_buttons() {
        assert_matches!(
            Box::new(UnpressedContender {
                initial_position: Position::zero(),
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            },),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a PressedContender with zero touch contacts yields a
    /// MatchedContender.
    #[fuchsia::test]
    async fn pressed_contender_no_touch_contacts() {
        assert_examined_matched_contender(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
        );
    }

    /// Tests that a PressedContender with multiple touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn pressed_contender_many_touch_contacts() {
        assert_matches!(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a PressedContender with a single touch contact and
    /// too much displacement yields a Mismatch.
    #[fuchsia::test]
    async fn pressed_contender_large_displacement() {
        assert_matches!(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: MAX_FINGER_DISPLACEMENT_IN_MM, y: MAX_FINGER_DISPLACEMENT_IN_MM }
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a PressedContender with a single touch contact,
    /// acceptable displacement, and no pressed buttons yields a
    /// MatchedContender.
    #[fuchsia::test]
    async fn pressed_contender_no_buttons() {
        assert_examined_matched_contender(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
        );
    }

    /// Tests that a PressedContender with a single touch contact,
    /// acceptable displacement, and one pressed button yields a
    /// PressedContender.
    #[fuchsia::test]
    async fn pressed_contender_single_button() {
        assert_contender_matches_type_id(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            TypeId::of::<PressedContender>(),
        );
    }

    /// Tests that a PressedContender with a single touch contact,
    /// acceptable displacement, and multiple pressed buttons yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn pressed_contender_many_buttons() {
        assert_matches!(
            Box::new(PressedContender {
                max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
            })
            .examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION }
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with no button pressed yields a Mismatch.
    #[fuchsia::test]
    async fn matched_contender_no_buttons() {
        assert_matches!(
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![],
                },
            })
            .verify_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION }
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with multiple buttons pressed yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn matched_contender_multiple_buttons() {
        assert_matches!(
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![],
                },
            })
            .verify_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION }
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with multiple touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    async fn matched_contender_multiple_touch_contacts() {
        assert_matches!(
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![],
                },
            })
            .verify_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with one touch contact and no buttons
    /// yields a MatchedContender.
    #[fuchsia::test]
    async fn matched_contender_one_touch_contacts() {
        assert_verified_matched_contender(
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![],
                },
            })
            .verify_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
        );
    }

    /// Tests that a MatchedContender with no buttons or touch contacts
    /// yields a MatchedContender.
    #[fuchsia::test]
    async fn matched_contender_no_contacts_no_buttons() {
        assert_verified_matched_contender(
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp: zx::Time::from_nanos(0),
                    pressed_buttons: vec![],
                },
            })
            .verify_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
        );
    }

    /// Tests that a MatchedContender processes buffered events by
    /// returning mouse down and mouse up events.
    #[fuchsia::test]
    async fn matched_contender_process_buffered_events() {
        let timestamp = zx::Time::from_nanos(0);
        let ProcessBufferedEventsResult { generated_events, winner, recognized_gesture } =
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp,
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp,
                    pressed_buttons: vec![],
                },
            })
            .process_buffered_events(vec![]);

        assert_eq!(
            generated_events,
            [
                gesture_arena::MouseEvent {
                    timestamp: timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Down,
                        affected_buttons: hashset! {0},
                        pressed_buttons: hashset! {0},
                        is_precision_scroll: None,
                    },
                },
                gesture_arena::MouseEvent {
                    timestamp: timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Up,
                        affected_buttons: hashset! {0},
                        pressed_buttons: hashset! {},
                        is_precision_scroll: None,
                    },
                }
            ]
        );
        assert_matches!(winner, None);
        assert_eq!(recognized_gesture, RecognizedGesture::Click);
    }

    /// Tests that a MatchedContender processes buffered events by
    /// returning mouse down and mouse up events with motion removed.
    #[fuchsia::test]
    async fn matched_contender_process_buffered_events_removes_motion() {
        let timestamp = zx::Time::from_nanos(0);
        let ProcessBufferedEventsResult { generated_events, winner, recognized_gesture } =
            Box::new(MatchedContender {
                pressed_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(0, Position::zero())],
                    timestamp,
                    pressed_buttons: vec![0],
                },
                released_event: TouchpadEvent {
                    contacts: vec![create_touch_contact(
                        0,
                        Position { x: HALF_MOTION, y: HALF_MOTION },
                    )],
                    timestamp,
                    pressed_buttons: vec![],
                },
            })
            .process_buffered_events(vec![]);

        assert_eq!(
            generated_events,
            [
                gesture_arena::MouseEvent {
                    timestamp: timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Down,
                        affected_buttons: hashset! {0},
                        pressed_buttons: hashset! {0},
                        is_precision_scroll: None,
                    },
                },
                gesture_arena::MouseEvent {
                    timestamp: timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Up,
                        affected_buttons: hashset! {0},
                        pressed_buttons: hashset! {},
                        is_precision_scroll: None,
                    },
                }
            ]
        );
        assert_matches!(winner, None);
        assert_eq!(recognized_gesture, RecognizedGesture::Click);
    }
}
