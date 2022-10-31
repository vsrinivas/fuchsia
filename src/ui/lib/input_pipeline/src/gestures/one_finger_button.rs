// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::gesture_arena::{
        self, DetailedReasonFloat, DetailedReasonUint, EndGestureEvent, ExamineEventResult,
        MouseEvent, ProcessBufferedEventsResult, ProcessNewEventResult, Reason, RecognizedGesture,
        TouchpadEvent, VerifyEventResult,
    },
    crate::mouse_binding::{self, MouseButton},
    crate::utils::{euclidean_distance, Position},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    maplit::hashset,
    std::collections::HashSet,
};

/// The initial state of this recognizer, before a click has been detected.
#[derive(Debug)]
pub(super) struct InitialContender {
    /// The threshold to detect motion.
    pub(super) spurious_to_intentional_motion_threshold_mm: f32,

    /// Use a larger threshold to detect motion on the edge of contact-button down,
    /// button down-button up
    pub(super) spurious_to_intentional_motion_threshold_button_change_mm: f32,

    /// The timeout of the edge of contact-button down, button down-button up,
    /// The recognizer will leave edge state either timeout or motion detected.
    pub(super) button_change_state_timeout: zx::Duration,
}

/// The state when this recognizer has detected a single finger, but not a
/// button press or release.
#[derive(Debug)]
struct FingerContactContender {
    /// The threshold to detect motion.
    spurious_to_intentional_motion_threshold_mm: f32,

    /// Use a larger threshold to detect motion on the edge of contact-button down,
    /// button down-button up
    spurious_to_intentional_motion_threshold_button_change_mm: f32,

    /// The timeout of the edge of contact-button down, button down-button up,
    /// The recognizer will leave edge state either timeout or motion detected.
    button_change_state_timeout: zx::Duration,

    /// The position of the initial single touch contact.
    initial_position: Position,
}

/// The state when this recognizer has detected a button down, but the gesture arena
/// has not declared this recognizer the winner.
#[derive(Debug)]
struct MatchedContender {
    /// The threshold to detect motion.
    spurious_to_intentional_motion_threshold_mm: f32,

    /// Use a larger threshold to detect motion on the edge of contact-button down,
    /// button down-button up
    spurious_to_intentional_motion_threshold_button_change_mm: f32,

    /// The timeout of the edge of contact-button down, button down-button up,
    /// The recognizer will leave edge state either timeout or motion detected.
    button_change_state_timeout: zx::Duration,

    /// The TouchpadEvent when a button was first pressed.
    pressed_event: TouchpadEvent,
}

/// The state when this recognizer has won the contest.
#[derive(Debug)]
struct ButtonDownWinner {
    /// The threshold to detect motion.
    spurious_to_intentional_motion_threshold_mm: f32,

    /// Use a larger threshold to detect motion on the edge of contact-button down,
    /// button down-button up
    spurious_to_intentional_motion_threshold_button_change_mm: f32,

    /// The timeout of the edge of contact-button down, button down-button up,
    /// The recognizer will leave edge state either timeout or motion detected.
    button_change_state_timeout: zx::Duration,

    /// The TouchpadEvent when a button was first pressed.
    pressed_event: TouchpadEvent,
}

/// The state when ButtonDownWinner got motion more than threshold to recognize as
/// drag gesture.
#[derive(Debug)]
struct DragWinner {
    /// The last TouchpadEvent.
    last_event: TouchpadEvent,
}

impl InitialContender {
    fn into_finger_contact_contender(
        self: Box<Self>,
        initial_position: Position,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(FingerContactContender {
            spurious_to_intentional_motion_threshold_mm: self
                .spurious_to_intentional_motion_threshold_mm,
            spurious_to_intentional_motion_threshold_button_change_mm: self
                .spurious_to_intentional_motion_threshold_button_change_mm,
            button_change_state_timeout: self.button_change_state_timeout,
            initial_position,
        })
    }

    fn into_matched_contender(
        self: Box<Self>,
        pressed_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {
            spurious_to_intentional_motion_threshold_mm: self
                .spurious_to_intentional_motion_threshold_mm,
            spurious_to_intentional_motion_threshold_button_change_mm: self
                .spurious_to_intentional_motion_threshold_button_change_mm,
            button_change_state_timeout: self.button_change_state_timeout,
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
                self.into_finger_contact_contender(position_from_event(event)),
            ),
            1 => ExamineEventResult::MatchedContender(self.into_matched_contender(event.clone())),
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

impl FingerContactContender {
    fn into_matched_contender(
        self: Box<Self>,
        pressed_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {
            spurious_to_intentional_motion_threshold_mm: self
                .spurious_to_intentional_motion_threshold_mm,
            spurious_to_intentional_motion_threshold_button_change_mm: self
                .spurious_to_intentional_motion_threshold_button_change_mm,
            button_change_state_timeout: self.button_change_state_timeout,
            pressed_event,
        })
    }
}

impl gesture_arena::Contender for FingerContactContender {
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
        if displacement_mm >= self.spurious_to_intentional_motion_threshold_mm {
            return ExamineEventResult::Mismatch(Reason::DetailedFloat(DetailedReasonFloat {
                criterion: "displacement_mm",
                min: None,
                max: Some(self.spurious_to_intentional_motion_threshold_mm),
                actual: displacement_mm,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        match num_pressed_buttons {
            0 => ExamineEventResult::Contender(self),
            1 => ExamineEventResult::MatchedContender(self.into_matched_contender(event.clone())),
            _ => ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: Some(0),
                max: Some(1),
                actual: num_pressed_buttons,
            })),
        }
    }
}

impl MatchedContender {
    fn into_button_down_winner(self: Box<Self>) -> Box<dyn gesture_arena::Winner> {
        Box::new(ButtonDownWinner {
            spurious_to_intentional_motion_threshold_mm: self
                .spurious_to_intentional_motion_threshold_mm,
            spurious_to_intentional_motion_threshold_button_change_mm: self
                .spurious_to_intentional_motion_threshold_button_change_mm,
            button_change_state_timeout: self.button_change_state_timeout,
            pressed_event: self.pressed_event,
        })
    }
}

impl gesture_arena::MatchedContender for MatchedContender {
    fn verify_event(self: Box<Self>, _event: &TouchpadEvent) -> VerifyEventResult {
        // This verify_event expected not call because all other recognizers
        // should exit on 1 finger button down.

        fx_log_err!("Unexpected MatchedContender::verify_event() called");

        VerifyEventResult::MatchedContender(self)
    }

    fn process_buffered_events(
        self: Box<Self>,
        _events: Vec<TouchpadEvent>,
    ) -> ProcessBufferedEventsResult {
        // all small motion before button down are ignored.
        ProcessBufferedEventsResult {
            generated_events: vec![touchpad_event_to_mouse_down_event(&self.pressed_event)],
            winner: Some(self.into_button_down_winner()),
            recognized_gesture: RecognizedGesture::OneButtonDown,
        }
    }
}

impl ButtonDownWinner {
    fn into_drag_winner(self: Box<Self>) -> Box<dyn gesture_arena::Winner> {
        Box::new(DragWinner { last_event: self.pressed_event })
    }
}

impl gesture_arena::Winner for ButtonDownWinner {
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> ProcessNewEventResult {
        let motion_threshold =
            if event.timestamp - self.pressed_event.timestamp > self.button_change_state_timeout {
                self.spurious_to_intentional_motion_threshold_mm
            } else {
                self.spurious_to_intentional_motion_threshold_button_change_mm
            };

        // Check for drag (button held, with sufficient contact movement).
        // TODO(fxbug.dev/93688): may want to handle contact > 1 with different logic.
        if event.contacts.len() > 0
            && euclidean_distance(
                position_from_event(&event),
                position_from_event(&self.pressed_event),
            ) > motion_threshold
        {
            let drag_winner = self.into_drag_winner();
            return drag_winner.process_new_event(event);
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        match num_pressed_buttons {
            // All small motion before button up, and motion in button up event
            // are ignored.
            0 => ProcessNewEventResult::EndGesture(
                EndGestureEvent::GeneratedEvent(touchpad_event_to_mouse_up_event(&event)),
                Reason::DetailedUint(DetailedReasonUint {
                    criterion: "num_buttons",
                    min: Some(1),
                    max: Some(1),
                    actual: 0,
                }),
            ),
            1 => ProcessNewEventResult::ContinueGesture(None, self),
            // Also wait for the button release to complete the click or drag gesture.
            // this should never happens unless there is a touchpad has more than 1 button.
            _ => ProcessNewEventResult::ContinueGesture(None, self),
        }
    }
}

impl DragWinner {
    fn into_drag_winner(
        self: Box<Self>,
        last_event: TouchpadEvent,
    ) -> Box<dyn gesture_arena::Winner> {
        Box::new(DragWinner { last_event })
    }
}

impl gesture_arena::Winner for DragWinner {
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> ProcessNewEventResult {
        let num_pressed_buttons = event.pressed_buttons.len();
        match num_pressed_buttons {
            // TODO(fxbug.dev/93688): may want to handle contact > 1 with different logic.
            // Motion in button up event is ignored.
            0 => ProcessNewEventResult::EndGesture(
                EndGestureEvent::GeneratedEvent(touchpad_event_to_mouse_up_event(&event)),
                Reason::DetailedUint(DetailedReasonUint {
                    criterion: "num_buttons",
                    min: Some(1),
                    max: Some(1),
                    actual: 0,
                }),
            ),
            _ => {
                // More than 2 button should never happens unless there is a touchpad has
                // more than 1 button. Just treat this same with 1 button down.
                ProcessNewEventResult::ContinueGesture(
                    Some(touchpad_event_to_mouse_drag_event(&self.last_event, &event)),
                    self.into_drag_winner(event),
                )
            }
        }
    }
}

/// This function returns the position associated with a TouchpadEvent that is
/// assumed to have a single associated TouchContact.
fn position_from_event(event: &TouchpadEvent) -> Position {
    event.contacts[0].position
}

fn touchpad_event_to_mouse_down_event(event: &TouchpadEvent) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        Position::zero(),
        mouse_binding::MousePhase::Down,
        hashset! {1},
        hashset! {1},
    )
}

fn touchpad_event_to_mouse_up_event(event: &TouchpadEvent) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        Position::zero(),
        mouse_binding::MousePhase::Up,
        hashset! {1},
        hashset! {},
    )
}

fn touchpad_event_to_mouse_drag_event(
    last_event: &TouchpadEvent,
    event: &TouchpadEvent,
) -> MouseEvent {
    make_mouse_event(
        event.timestamp,
        event.contacts[0].position - last_event.contacts[0].position,
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
    use {super::*, crate::touch_binding, assert_matches::assert_matches, test_case::test_case};

    fn make_touch_contact(id: u32, position: Position) -> touch_binding::TouchContact {
        touch_binding::TouchContact { id, position, pressure: None, contact_size: None }
    }

    const SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM: f32 = 10.0;
    const SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM: f32 = 20.0;
    const BUTTON_CHANGE_STATE_TIMEOUT: zx::Duration = zx::Duration::from_seconds(1);

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
        let contender: Box<dyn gesture_arena::Contender> = Box::new(InitialContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[fuchsia::test]
    fn initial_contender_examine_event_finger_contact_contender() {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(InitialContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
        });
        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
        };

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[fuchsia::test]
    fn initial_contender_examine_event_matched_contender() {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(InitialContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
        });
        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
        };

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::MatchedContender(_));
    }

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
    #[test_case(TouchpadEvent{
      timestamp: zx::Time::ZERO,
      pressed_buttons: vec![],
      contacts: vec![
          make_touch_contact(1, Position{x: 10.0, y: 1.0}),
      ],
    };"1 fingers move more than threshold")]
    #[fuchsia::test]
    fn finger_contact_contender_examine_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            initial_position: Position { x: 0.0, y: 0.0 },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[test_case(TouchpadEvent{
      timestamp: zx::Time::ZERO,
      pressed_buttons: vec![],
      contacts: vec![
          make_touch_contact(1, Position{x: 9.0, y: 1.0}),
      ],
    };"1 fingers move less than threshold")]
    #[test_case(TouchpadEvent{
      timestamp: zx::Time::ZERO,
      pressed_buttons: vec![],
      contacts: vec![
          make_touch_contact(1, Position{x: 0.0, y: 0.0}),
      ],
    };"1 fingers stay")]
    #[fuchsia::test]
    fn finger_contact_contender_examine_event_finger_contact_contender(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            initial_position: Position { x: 0.0, y: 0.0 },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[fuchsia::test]
    fn finger_contact_contender_examine_event_matched_contender() {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            initial_position: Position { x: 0.0, y: 0.0 },
        });
        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
        };

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::MatchedContender(_));
    }

    #[fuchsia::test]
    fn matched_contender_process_buffered_events() {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            pressed_event: TouchpadEvent {
                timestamp: zx::Time::from_nanos(41),
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
            },
        });
        let events = vec![
            TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![],
                contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(41),
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 1.0, y: 1.0 })],
            },
        ];

        let got = contender.process_buffered_events(events);

        assert_matches!(got, ProcessBufferedEventsResult{
          generated_events,
          winner: Some(winner),
          recognized_gesture: RecognizedGesture::OneButtonDown,
        } => {
          pretty_assertions::assert_eq!(generated_events, vec![
            MouseEvent {
              timestamp:zx::Time::from_nanos(41),
              mouse_data: mouse_binding::MouseEvent::new(
                  mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                      counts: Position { x: 0.0, y: 0.0 },
                      millimeters: Position { x: 0.0, y: 0.0 },
                  }),
                  /* wheel_delta_v= */ None,
                  /* wheel_delta_h= */ None,
                  mouse_binding::MousePhase::Down,
                  /* affected_buttons= */ hashset!{1},
                  /* pressed_buttons= */ hashset!{1},
                  /* is_precision_scroll= */ None,
              ),
            }
          ]);
          pretty_assertions::assert_eq!(winner.get_type_name(), "input_pipeline_lib_test::gestures::one_finger_button::ButtonDownWinner");
        });
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            ],
        };"button release")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 19.0, y: 1.0}),
        ],
    };"move less than threshold in edge state")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO + zx::Duration::from_millis(1500),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 9.0, y: 1.0}),
        ],
    };"move less than threshold out of edge state")]
    #[fuchsia::test]
    fn button_down_winner_button_up_end(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(ButtonDownWinner {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            pressed_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::EndGesture(EndGestureEvent::GeneratedEvent(MouseEvent {mouse_data, ..}), _) => {
            pretty_assertions::assert_eq!(mouse_data, mouse_binding::MouseEvent::new(
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 0.0, y: 0.0 },
                    millimeters: Position { x: 0.0, y: 0.0 },
                }),
                /* wheel_delta_v= */ None,
                /* wheel_delta_h= */ None,
                mouse_binding::MousePhase::Up,
                /* affected_buttons= */ hashset!{1},
                /* pressed_buttons= */ hashset!{},
                /* is_precision_scroll= */ None,
            ));
        });
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 19.0, y: 1.0}),
        ],
    };"move less than threshold in edge state")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO + zx::Duration::from_millis(1500),
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 9.0, y: 1.0}),
        ],
    };"move less than threshold out of edge state")]
    #[fuchsia::test]
    fn button_down_winner_continue(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(ButtonDownWinner {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            pressed_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::ContinueGesture(None, got_winner)=>{
            pretty_assertions::assert_eq!(got_winner.get_type_name(), "input_pipeline_lib_test::gestures::one_finger_button::ButtonDownWinner");
        });
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 20.0, y: 1.0}),
        ],
    };"move more than threshold in edge state and release button")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO + zx::Duration::from_millis(1500),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 10.0, y: 1.0}),
        ],
    };"move more than threshold out of edge state and release button")]
    #[fuchsia::test]
    fn button_down_winner_drag_winner_button_up_end(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(ButtonDownWinner {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            pressed_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::EndGesture(EndGestureEvent::GeneratedEvent(MouseEvent {mouse_data, ..}), _) => {
            pretty_assertions::assert_eq!(mouse_data, mouse_binding::MouseEvent::new(
                // Motion in button up event is ignored.
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 0.0, y: 0.0 },
                    millimeters: Position { x: 0.0, y: 0.0 },
                }),
                /* wheel_delta_v= */ None,
                /* wheel_delta_h= */ None,
                mouse_binding::MousePhase::Up,
                /* affected_buttons= */ hashset!{1},
                /* pressed_buttons= */ hashset!{},
                /* is_precision_scroll= */ None,
            ));
        });
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 20.0, y: 1.0}),
        ],
    };"move more than threshold in edge state")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO + zx::Duration::from_millis(1500),
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 10.0, y: 1.0}),
        ],
    };"move more than threshold out of edge state")]
    #[fuchsia::test]
    fn button_down_winner_drag_winner_continue(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(ButtonDownWinner {
            spurious_to_intentional_motion_threshold_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM,
            spurious_to_intentional_motion_threshold_button_change_mm:
                SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_BUTTON_CHANGE_MM,
            button_change_state_timeout: BUTTON_CHANGE_STATE_TIMEOUT,
            pressed_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::ContinueGesture(Some(MouseEvent {mouse_data, ..}), got_winner)=>{
            pretty_assertions::assert_eq!(mouse_data.phase, mouse_binding::MousePhase::Move);
            pretty_assertions::assert_eq!(got_winner.get_type_name(), "input_pipeline_lib_test::gestures::one_finger_button::DragWinner");
        });
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            ],
    };"button release")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::from_nanos(41),
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 19.0, y: 1.0}),
        ],
    };"move and button release")]
    #[fuchsia::test]
    fn drag_winner_button_up_end(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(DragWinner {
            last_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::EndGesture(EndGestureEvent::GeneratedEvent(MouseEvent {mouse_data, ..}), _) => {
            pretty_assertions::assert_eq!(mouse_data, mouse_binding::MouseEvent::new(
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 0.0, y: 0.0 },
                    millimeters: Position { x: 0.0, y: 0.0 },
                }),
                /* wheel_delta_v= */ None,
                /* wheel_delta_h= */ None,
                mouse_binding::MousePhase::Up,
                /* affected_buttons= */ hashset!{1},
                /* pressed_buttons= */ hashset!{},
                /* is_precision_scroll= */ None,
            ));
        });
    }

    #[fuchsia::test]
    fn drag_winner_continue() {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(DragWinner {
            last_event: TouchpadEvent {
                timestamp: zx::Time::ZERO,
                pressed_buttons: vec![1],
                contacts: vec![make_touch_contact(1, Position { x: 0.0, y: 0.0 })],
            },
        });

        let event = TouchpadEvent {
            timestamp: zx::Time::from_nanos(41),
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position { x: 19.0, y: 1.0 })],
        };

        let got = winner.process_new_event(event);
        assert_matches!(got, ProcessNewEventResult::ContinueGesture(Some(MouseEvent {mouse_data, ..}), got_winner)=>{
            pretty_assertions::assert_eq!(mouse_data, mouse_binding::MouseEvent::new(
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: Position { x: 0.0, y: 0.0 },
                    millimeters: Position { x: 19.0, y: 1.0 },
                }),
                /* wheel_delta_v= */ None,
                /* wheel_delta_h= */ None,
                mouse_binding::MousePhase::Move,
                /* affected_buttons= */ hashset!{},
                /* pressed_buttons= */ hashset!{1},
                /* is_precision_scroll= */ None,
            ));
            pretty_assertions::assert_eq!(got_winner.get_type_name(), "input_pipeline_lib_test::gestures::one_finger_button::DragWinner");
        });
    }
}
