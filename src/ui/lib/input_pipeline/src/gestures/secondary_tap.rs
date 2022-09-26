// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::gesture_arena::{
        self, ExamineEventResult, MismatchData, MismatchDetailsFloat, MismatchDetailsUint,
        ProcessBufferedEventsResult, RecognizedGesture, TouchpadEvent, VerifyEventResult,
        SECONDARY_BUTTON,
    },
    crate::mouse_binding::{MouseEvent, MouseLocation, MousePhase, RelativeLocation},
    crate::utils::{euclidean_distance, Position},
    fuchsia_zircon as zx,
    maplit::hashset,
};

/// The initial state of this recognizer, before a secondary tap has been
/// detected.
#[derive(Debug)]
pub(super) struct InitialContender {
    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a secondary tap. Measured in millimeters.
    pub(super) max_finger_displacement_in_mm: f32,

    /// The maximum time that can elapse between two fingers down and fingers up
    /// to be considered a secondary tap gesture.
    pub(super) max_time_elapsed: zx::Duration,
}

impl InitialContender {
    fn into_one_finger_contact_contender(
        self: Box<Self>,
        one_finger_contact_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(OneFingerContactContender {
            one_finger_contact_event,
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            max_time_elapsed: self.max_time_elapsed,
        })
    }

    fn into_two_finger_contacts_contender(
        self: Box<Self>,
        two_finger_contacts_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(TwoFingerContactsContender {
            two_finger_contacts_event,
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            max_time_elapsed: self.max_time_elapsed,
        })
    }
}

impl gesture_arena::Contender for InitialContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 0 {
            return ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let num_contacts = event.contacts.len();
        match num_contacts {
            1 => {
                ExamineEventResult::Contender(self.into_one_finger_contact_contender(event.clone()))
            }
            2 => ExamineEventResult::Contender(
                self.into_two_finger_contacts_contender(event.clone()),
            ),
            0 | _ => {
                ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                    criterion: "num_contacts",
                    min: Some(1),
                    max: Some(2),
                    actual: num_contacts,
                }))
            }
        }
    }

    fn start_from_idle(&self) -> bool {
        true
    }
}

/// The state when this recognizer has detected a single finger down.
#[derive(Debug)]
struct OneFingerContactContender {
    /// The TouchpadEvent when a finger down was first detected.
    one_finger_contact_event: TouchpadEvent,

    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a tap. Measured in millimeters.
    max_finger_displacement_in_mm: f32,

    /// The maximum time that can elapse between two fingers down and fingers up
    /// to be considered a secondary tap gesture.
    max_time_elapsed: zx::Duration,
}

impl OneFingerContactContender {
    fn into_two_finger_contacts_contender(
        self: Box<Self>,
        two_finger_contacts_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(TwoFingerContactsContender {
            two_finger_contacts_event,
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            max_time_elapsed: self.max_time_elapsed,
        })
    }
}

impl gesture_arena::Contender for OneFingerContactContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        if !is_valid_event_time(event, &self.one_finger_contact_event, self.max_time_elapsed) {
            return ExamineEventResult::Mismatch(MismatchData::Basic("too much time elapsed"));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 0 {
            return ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let num_contacts = event.contacts.len();
        match num_contacts {
            1 => {
                let displacement_mm = euclidean_distance(
                    position_from_event(event, 0),
                    position_from_event(&self.one_finger_contact_event, 0),
                );
                if displacement_mm >= self.max_finger_displacement_in_mm {
                    return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                        MismatchDetailsFloat {
                            criterion: "displacement_mm",
                            min: None,
                            max: Some(self.max_finger_displacement_in_mm),
                            actual: displacement_mm,
                        },
                    ));
                }
                ExamineEventResult::Contender(self)
            }
            2 => {
                let displacement_mm = euclidean_distance(
                    position_from_event(event, 0),
                    position_from_event(&self.one_finger_contact_event, 0),
                );
                if displacement_mm >= self.max_finger_displacement_in_mm {
                    return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                        MismatchDetailsFloat {
                            criterion: "displacement_mm",
                            min: None,
                            max: Some(self.max_finger_displacement_in_mm),
                            actual: displacement_mm,
                        },
                    ));
                }
                ExamineEventResult::Contender(
                    self.into_two_finger_contacts_contender(event.clone()),
                )
            }
            0 | _ => {
                ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                    criterion: "num_contacts",
                    min: Some(1),
                    max: Some(2),
                    actual: num_contacts,
                }))
            }
        }
    }
}

/// The state when this recognizer has detected two fingers down.
#[derive(Debug)]
struct TwoFingerContactsContender {
    /// The TouchpadEvent when two fingers were first detected.
    two_finger_contacts_event: TouchpadEvent,

    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a tap. Measured in millimeters.
    max_finger_displacement_in_mm: f32,

    /// The maximum time that can elapse between two fingers down and fingers up
    /// to be considered a secondary tap gesture.
    max_time_elapsed: zx::Duration,
}

impl TwoFingerContactsContender {
    fn into_one_finger_raised_contender(self: Box<Self>) -> Box<dyn gesture_arena::Contender> {
        Box::new(OneFingerRaisedContender {
            two_finger_contacts_event: self.two_finger_contacts_event,
            max_finger_displacement_in_mm: self.max_finger_displacement_in_mm,
            max_time_elapsed: self.max_time_elapsed,
        })
    }

    fn into_matched_contender(
        self: Box<Self>,
        no_contacts_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {
            two_finger_contacts_event: self.two_finger_contacts_event,
            no_contacts_event,
            max_time_elapsed: self.max_time_elapsed,
        })
    }
}

impl gesture_arena::Contender for TwoFingerContactsContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        if !is_valid_event_time(event, &self.two_finger_contacts_event, self.max_time_elapsed) {
            return ExamineEventResult::Mismatch(MismatchData::Basic("too much time elapsed"));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 0 {
            return ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let num_contacts = u8::try_from(event.contacts.len()).unwrap_or(u8::MAX);
        match num_contacts {
            0 => ExamineEventResult::MatchedContender(self.into_matched_contender(event.clone())),
            1 => {
                match &self
                    .two_finger_contacts_event
                    .clone()
                    .contacts
                    .into_iter()
                    .find(|contact| contact.id == event.contacts[0].id)
                {
                    Some(contact) => {
                        let displacement_mm =
                            euclidean_distance(position_from_event(event, 0), contact.position);
                        if displacement_mm >= self.max_finger_displacement_in_mm {
                            return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                                MismatchDetailsFloat {
                                    criterion: "displacement_mm",
                                    min: None,
                                    max: Some(self.max_finger_displacement_in_mm),
                                    actual: displacement_mm,
                                },
                            ));
                        }
                    }
                    None => {
                        return ExamineEventResult::Mismatch(MismatchData::Basic(
                            "remaining contact id differs from initial two finger contacts",
                        ));
                    }
                }

                ExamineEventResult::Contender(self.into_one_finger_raised_contender())
            }
            2 => {
                // Acceptable displacement on the first touch contact.
                let displacement_mm = euclidean_distance(
                    position_from_event(event, 0),
                    position_from_event(&self.two_finger_contacts_event, 0),
                );
                if displacement_mm >= self.max_finger_displacement_in_mm {
                    return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                        MismatchDetailsFloat {
                            criterion: "displacement_mm",
                            min: None,
                            max: Some(self.max_finger_displacement_in_mm),
                            actual: displacement_mm,
                        },
                    ));
                }

                // Acceptable displacement on the second touch contact.
                let displacement_mm = euclidean_distance(
                    position_from_event(event, 1),
                    position_from_event(&self.two_finger_contacts_event, 1),
                );
                if displacement_mm >= self.max_finger_displacement_in_mm {
                    return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                        MismatchDetailsFloat {
                            criterion: "displacement_mm",
                            min: None,
                            max: Some(self.max_finger_displacement_in_mm),
                            actual: displacement_mm,
                        },
                    ));
                }

                ExamineEventResult::Contender(self)
            }
            3.. => ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_contacts",
                min: Some(0),
                max: Some(2),
                actual: usize::from(num_contacts),
            })),
        }
    }
}

/// The state when this recognizer has already detected two fingers down,
/// and one of those fingers has been raised.
#[derive(Debug)]
struct OneFingerRaisedContender {
    /// The TouchpadEvent when two fingers were first detected.
    two_finger_contacts_event: TouchpadEvent,

    /// The maximum displacement that a detected finger can withstand to still
    /// be considered a tap. Measured in millimeters.
    max_finger_displacement_in_mm: f32,

    /// The maximum time that can elapse between two fingers down and fingers up
    /// to be considered a secondary tap gesture.
    max_time_elapsed: zx::Duration,
}

impl OneFingerRaisedContender {
    fn into_matched_contender(
        self: Box<Self>,
        no_contacts_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {
            two_finger_contacts_event: self.two_finger_contacts_event,
            no_contacts_event,
            max_time_elapsed: self.max_time_elapsed,
        })
    }
}

impl gesture_arena::Contender for OneFingerRaisedContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        if !is_valid_event_time(event, &self.two_finger_contacts_event, self.max_time_elapsed) {
            return ExamineEventResult::Mismatch(MismatchData::Basic("too much time elapsed"));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 0 {
            return ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let num_contacts = u8::try_from(event.contacts.len()).unwrap_or(u8::MAX);
        match num_contacts {
            0 => ExamineEventResult::MatchedContender(self.into_matched_contender(event.clone())),
            1 => {
                match &self
                    .two_finger_contacts_event
                    .clone()
                    .contacts
                    .into_iter()
                    .find(|contact| contact.id == event.contacts[0].id)
                {
                    Some(contact) => {
                        let displacement_mm =
                            euclidean_distance(position_from_event(event, 0), contact.position);
                        if displacement_mm >= self.max_finger_displacement_in_mm {
                            return ExamineEventResult::Mismatch(MismatchData::DetailedFloat(
                                MismatchDetailsFloat {
                                    criterion: "displacement_mm",
                                    min: None,
                                    max: Some(self.max_finger_displacement_in_mm),
                                    actual: displacement_mm,
                                },
                            ));
                        }
                    }
                    None => {
                        return ExamineEventResult::Mismatch(MismatchData::Basic(
                            "remaining contact id differs from initial two finger contacts",
                        ));
                    }
                }

                ExamineEventResult::Contender(self)
            }
            2.. => ExamineEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_contacts",
                min: Some(0),
                max: Some(1),
                actual: usize::from(num_contacts),
            })),
        }
    }
}

/// The state when this recognizer has detected a secondary tap, but the
/// gesture arena has not declared this recognizer the winner.
#[derive(Debug)]
struct MatchedContender {
    /// The TouchpadEvent when two fingers were first detected.
    two_finger_contacts_event: TouchpadEvent,

    /// The TouchpadEvent when two fingers, previously detected, were released.
    no_contacts_event: TouchpadEvent,

    /// The maximum time that can elapse between two fingers down and fingers up
    /// to be considered a secondary tap gesture.
    max_time_elapsed: zx::Duration,
}

impl gesture_arena::MatchedContender for MatchedContender {
    fn verify_event(self: Box<Self>, event: &TouchpadEvent) -> VerifyEventResult {
        if !is_valid_event_time(event, &self.two_finger_contacts_event, self.max_time_elapsed) {
            return VerifyEventResult::Mismatch(MismatchData::Basic("too much time elapsed"));
        }

        let num_contacts = event.contacts.len();
        if num_contacts != 0 {
            return VerifyEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_contacts",
                min: Some(0),
                max: Some(0),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons != 0 {
            return VerifyEventResult::Mismatch(MismatchData::DetailedUint(MismatchDetailsUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(0),
                actual: num_pressed_buttons,
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
                    timestamp: self.two_finger_contacts_event.timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position::zero(),
                            millimeters: Position::zero(),
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Down,
                        affected_buttons: hashset! {SECONDARY_BUTTON},
                        pressed_buttons: hashset! {SECONDARY_BUTTON},
                        is_precision_scroll: None,
                    },
                },
                gesture_arena::MouseEvent {
                    timestamp: self.no_contacts_event.timestamp,
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position::zero(),
                            millimeters: Position::zero(),
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Up,
                        affected_buttons: hashset! {SECONDARY_BUTTON},
                        pressed_buttons: hashset! {},
                        is_precision_scroll: None,
                    },
                },
            ],
            winner: None,
            recognized_gesture: RecognizedGesture::SecondaryTap,
        }
    }
}

/// This function returns the position associated with the touch contact at the
/// given index from a TouchpadEvent.
fn position_from_event(event: &TouchpadEvent, index: usize) -> Position {
    event.contacts[index].position
}

/// Returns true iff the timestamp for new_event has not exceeded
/// the threshold since the old_event timestamp.
fn is_valid_event_time(
    new_event: &TouchpadEvent,
    old_event: &TouchpadEvent,
    threshold: zx::Duration,
) -> bool {
    new_event.timestamp - old_event.timestamp < threshold
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::gestures::gesture_arena::{Contender, MatchedContender as _, TouchpadEvent},
        crate::testing_utilities::create_touch_contact,
        assert_matches::assert_matches,
        std::any::TypeId,
    };

    const MAX_TIME_ELAPSED: zx::Duration = zx::Duration::from_nanos(10000);
    const MAX_FINGER_DISPLACEMENT_IN_MM: f32 = 10.0;
    const HALF_MOTION: f32 = MAX_FINGER_DISPLACEMENT_IN_MM / 2.0;

    fn get_initial_contender() -> Box<InitialContender> {
        Box::new(InitialContender {
            max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            max_time_elapsed: MAX_TIME_ELAPSED,
        })
    }

    fn get_one_finger_contact_contender() -> Box<OneFingerContactContender> {
        Box::new(OneFingerContactContender {
            max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            max_time_elapsed: MAX_TIME_ELAPSED,
            one_finger_contact_event: TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
        })
    }

    fn get_two_finger_contacts_contender() -> Box<TwoFingerContactsContender> {
        Box::new(TwoFingerContactsContender {
            max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            max_time_elapsed: MAX_TIME_ELAPSED,
            two_finger_contacts_event: TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
        })
    }

    fn get_one_finger_raised_contender() -> Box<OneFingerRaisedContender> {
        Box::new(OneFingerRaisedContender {
            max_finger_displacement_in_mm: MAX_FINGER_DISPLACEMENT_IN_MM,
            max_time_elapsed: MAX_TIME_ELAPSED,
            two_finger_contacts_event: TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
        })
    }

    fn get_matched_contender() -> Box<MatchedContender> {
        Box::new(MatchedContender {
            two_finger_contacts_event: TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
            no_contacts_event: TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(123),
                pressed_buttons: vec![],
            },
            max_time_elapsed: MAX_TIME_ELAPSED,
        })
    }

    fn assert_contender(result: ExamineEventResult, type_id: TypeId) {
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

    /// Tests that an InitialContender with a single touch contact and one
    /// pressed button yields a Mismatch.
    #[fuchsia::test]
    fn contender_single_button() {
        assert_matches!(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            },),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an InitialContender with a single touch contact and multiple
    /// pressed button yields a Mismatch.
    #[fuchsia::test]
    fn contender_many_buttons() {
        assert_matches!(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            },),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an InitialContender with zero touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    fn contender_no_contacts() {
        assert_matches!(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that an InitialContender with a single touch contact and no
    /// pressed buttons yields a OneFingerContactContender.
    #[fuchsia::test]
    fn contender_one_contact() {
        assert_contender(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerContactContender>(),
        );
    }

    /// Tests that an InitialContender with two touch contacts and no pressed
    /// buttons yields a TwoFingerContactsContender.
    #[fuchsia::test]
    fn contender_two_contacts() {
        assert_contender(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );
    }

    /// Tests that an InitialContender with more than two touch contacts yields
    /// a Mismatch.
    #[fuchsia::test]
    fn contender_many_contacts() {
        assert_matches!(
            get_initial_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                    create_touch_contact(2, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with an event whose timestamp
    /// exceeds the elapsed threshold yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_too_long() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero()),],
                timestamp: MAX_TIME_ELAPSED + zx::Time::from_nanos(1),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with one pressed button yields a
    /// Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_single_button() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with multiple pressed buttons
    /// yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_many_buttons() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with zero touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_no_contacts() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with more than two touch contacts
    /// yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_many_touch_contacts() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                    create_touch_contact(2, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with one touch contact yields
    /// a OneFingerContactContender.
    #[fuchsia::test]
    fn one_finger_contact_contender_one_contact() {
        assert_contender(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerContactContender>(),
        );
    }

    /// Tests that a OneFingerContactContender with two touch contacts yields
    /// a TwoFingerContactsContender.
    #[fuchsia::test]
    fn one_finger_contact_contender_two_touch_contacts() {
        assert_contender(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );
    }

    /// Tests that a OneFingerContactContender with a single touch contact and
    /// too much displacement yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_one_contact_large_displacement() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
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

    /// Tests that a OneFingerContactContender with two touch contacts and
    /// one with too much displacement yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_contact_contender_two_contacts_large_displacement() {
        assert_matches!(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(
                        0,
                        Position {
                            x: MAX_FINGER_DISPLACEMENT_IN_MM,
                            y: MAX_FINGER_DISPLACEMENT_IN_MM
                        }
                    ),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerContactContender with a single touch contact
    /// and acceptable displacement yields a OneFingerContactContender.
    #[fuchsia::test]
    fn one_finger_contact_contender_one_contact_some_displacement() {
        assert_contender(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerContactContender>(),
        );
    }

    /// Tests that a OneFingerContactContender with a two touch contacts
    /// and one with acceptable displacement yields a OneFingerContactContender.
    #[fuchsia::test]
    fn one_finger_contact_contender_two_contacts_some_displacement() {
        assert_contender(
            get_one_finger_contact_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position { x: HALF_MOTION, y: HALF_MOTION }),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );
    }

    /// Tests that a TwoFingerContactsContender with an event whose timestamp
    /// exceeds the elapsed threshold yields a Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_too_long() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: MAX_TIME_ELAPSED + zx::Time::from_nanos(1),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with one pressed button yields a
    /// Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_single_button() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with multiple pressed buttons
    /// yields a Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_many_buttons() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with zero touch contacts yields a
    /// MatchedContender.
    #[fuchsia::test]
    fn two_finger_contacts_contender_no_touch_contacts() {
        assert_examined_matched_contender(get_two_finger_contacts_contender().examine_event(
            &TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
        ));
    }

    /// Tests that a TwoFingerContactsContender with one touch contact with
    /// acceptable displacement yields a OneFingerRaisedContender.
    #[fuchsia::test]
    fn two_finger_contacts_contender_one_contact() {
        assert_contender(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerRaisedContender>(),
        );
    }

    /// Tests that a TwoFingerContactsContender with one touch contact with
    /// too much displacement yields a Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_one_contact_large_displacement() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: MAX_FINGER_DISPLACEMENT_IN_MM, y: MAX_FINGER_DISPLACEMENT_IN_MM },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with more than two touch contacts
    /// yields a Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_many_touch_contacts() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                    create_touch_contact(2, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with two touch contacts yields
    /// a TwoFingerContactsContender.
    #[fuchsia::test]
    fn two_finger_contacts_contender_two_touch_contacts() {
        assert_contender(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );
    }

    /// Tests that a TwoFingerContactsContender with two touch contacts and
    /// one with too much displacement yields a Mismatch.
    #[fuchsia::test]
    fn two_finger_contacts_contender_two_contacts_large_displacement() {
        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(
                        0,
                        Position {
                            x: MAX_FINGER_DISPLACEMENT_IN_MM,
                            y: MAX_FINGER_DISPLACEMENT_IN_MM
                        }
                    ),
                    create_touch_contact(1, Position::zero())
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );

        assert_matches!(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(
                        1,
                        Position {
                            x: MAX_FINGER_DISPLACEMENT_IN_MM,
                            y: MAX_FINGER_DISPLACEMENT_IN_MM
                        }
                    ),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a TwoFingerContactsContender with two touch contact
    /// and acceptable displacement yields a TwoFingerContactsContender.
    #[fuchsia::test]
    fn two_finger_contacts_contender_two_contacts_some_displacement() {
        assert_contender(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position { x: HALF_MOTION, y: HALF_MOTION }),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );

        assert_contender(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position { x: HALF_MOTION, y: HALF_MOTION }),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );

        assert_contender(
            get_two_finger_contacts_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position { x: HALF_MOTION, y: HALF_MOTION }),
                    create_touch_contact(1, Position { x: HALF_MOTION, y: HALF_MOTION }),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<TwoFingerContactsContender>(),
        );
    }

    /// Tests that a OneFingerRaisedContender with an event whose timestamp
    /// exceeds the elapsed threshold yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_too_long() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: MAX_TIME_ELAPSED + zx::Time::from_nanos(1),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerRaisedContender with one pressed button yields a
    /// Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_single_button() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerRaisedContender with multiple pressed buttons
    /// yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_many_buttons() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerRaisedContender with zero touch contacts yields a
    /// MatchedContender.
    #[fuchsia::test]
    fn one_finger_raised_contender_no_touch_contacts() {
        assert_examined_matched_contender(get_one_finger_raised_contender().examine_event(
            &TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            },
        ));
    }

    /// Tests that a OneFingerRaisedContender with one touch contact with
    /// acceptable displacement against first recorded contact yields a
    /// OneFingerRaisedContender.
    #[fuchsia::test]
    fn one_finger_raised_contender_one_contact_first_id() {
        assert_contender(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerRaisedContender>(),
        );

        assert_contender(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerRaisedContender>(),
        );
    }

    /// Tests that a OneFingerRaisedContender with one touch contact with
    /// acceptable displacement against second recorded contact yields a
    /// OneFingerRaisedContender.
    #[fuchsia::test]
    fn one_finger_raised_contender_one_contact_second_id() {
        assert_contender(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(1, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerRaisedContender>(),
        );

        assert_contender(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    1,
                    Position { x: HALF_MOTION, y: HALF_MOTION },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            TypeId::of::<OneFingerRaisedContender>(),
        );
    }

    /// Tests that a OneFingerRaisedContender with one touch contact with
    /// acceptable displacement against an unrecorded contact id yields a
    /// Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_one_contact_invalid_id() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(2, Position::zero(),)],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerRaisedContender with one touch contact with
    /// too much displacement yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_one_contact_large_displacement() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(
                    0,
                    Position { x: MAX_FINGER_DISPLACEMENT_IN_MM, y: MAX_FINGER_DISPLACEMENT_IN_MM },
                )],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a OneFingerRaisedContender with more than one touch contacts
    /// yields a Mismatch.
    #[fuchsia::test]
    fn one_finger_raised_contender_many_touch_contacts() {
        assert_matches!(
            get_one_finger_raised_contender().examine_event(&TouchpadEvent {
                contacts: vec![
                    create_touch_contact(0, Position::zero()),
                    create_touch_contact(1, Position::zero()),
                ],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            ExamineEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with an event whose timestamp exceeds
    /// the elapsed threshold yields a Mismatch.
    #[fuchsia::test]
    fn matched_contender_too_long() {
        assert_matches!(
            get_matched_contender().verify_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: MAX_TIME_ELAPSED + zx::Time::from_nanos(1),
                pressed_buttons: vec![],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with one touch contact yields a Mismatch.
    #[fuchsia::test]
    fn matched_contender_one_contact() {
        assert_matches!(
            get_matched_contender().verify_event(&TouchpadEvent {
                contacts: vec![create_touch_contact(0, Position::zero())],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with multiple touch contacts yields a
    /// Mismatch.
    #[fuchsia::test]
    fn matched_contender_many_contacts() {
        assert_matches!(
            get_matched_contender().verify_event(&TouchpadEvent {
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

    /// Tests that a MatchedContender with one button pressed yields a Mismatch.
    #[fuchsia::test]
    fn matched_contender_one_button() {
        assert_matches!(
            get_matched_contender().verify_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with multiple buttons pressed yields a
    /// Mismatch.
    #[fuchsia::test]
    fn matched_contender_many_buttons() {
        assert_matches!(
            get_matched_contender().verify_event(&TouchpadEvent {
                contacts: vec![],
                timestamp: zx::Time::from_nanos(0),
                pressed_buttons: vec![0, 1],
            }),
            VerifyEventResult::Mismatch(_)
        );
    }

    /// Tests that a MatchedContender with no touch contacts yields a
    /// MatchedContender.
    #[fuchsia::test]
    fn matched_contender_no_contacts() {
        assert_verified_matched_contender(get_matched_contender().verify_event(&TouchpadEvent {
            contacts: vec![],
            timestamp: zx::Time::from_nanos(0),
            pressed_buttons: vec![],
        }));
    }

    /// Tests that a MatchedContender processes buffered events by
    /// returning mouse down and mouse up events.
    #[fuchsia::test]
    fn matched_contender_process_buffered_events() {
        let ProcessBufferedEventsResult { generated_events, winner, recognized_gesture } =
            get_matched_contender().process_buffered_events(vec![]);

        assert_eq!(
            generated_events,
            [
                gesture_arena::MouseEvent {
                    timestamp: zx::Time::from_nanos(0),
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Down,
                        affected_buttons: hashset! {SECONDARY_BUTTON},
                        pressed_buttons: hashset! {SECONDARY_BUTTON},
                        is_precision_scroll: None,
                    },
                },
                gesture_arena::MouseEvent {
                    timestamp: zx::Time::from_nanos(123),
                    mouse_data: MouseEvent {
                        location: MouseLocation::Relative(RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 }
                        }),
                        wheel_delta_v: None,
                        wheel_delta_h: None,
                        phase: MousePhase::Up,
                        affected_buttons: hashset! {SECONDARY_BUTTON},
                        pressed_buttons: hashset! {},
                        is_precision_scroll: None,
                    },
                }
            ]
        );
        assert_matches!(winner, None);
        assert_eq!(recognized_gesture, RecognizedGesture::SecondaryTap);
    }
}
