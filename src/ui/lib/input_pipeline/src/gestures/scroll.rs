// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::gesture_arena::{
        self, DetailedReasonUint, EndGestureEvent, ExamineEventResult, MouseEvent,
        ProcessBufferedEventsResult, ProcessNewEventResult, Reason, RecognizedGesture,
        TouchpadEvent, VerifyEventResult,
    },
    crate::mouse_binding,
    crate::utils::{euclidean_distance, Position},
    fuchsia_syslog::fx_log_err,
    maplit::hashset,
};

/// The initial state of this recognizer, before 2 finger contact has been detected.
#[derive(Debug)]
pub(super) struct InitialContender {
    /// The minimum movement in millimeters on surface to recognize as a scroll.
    pub(super) min_movement_in_mm: f32,

    /// The maximum movement in millimeters on surface to recognize as a scroll. If recognizer can
    /// not detect the scroll direction when movement more than this number, return Mismatch to
    /// end this recognizer.
    pub(super) max_movement_in_mm: f32,

    /// The limit tangent for direction detect, for example if we only want to allow ±15° the number
    /// is 0.26794919243.
    pub(super) limit_tangent_for_direction: f32,
}

/// The state when this recognizer has detected 2 finger contact, before finger movement > threshold.
#[derive(Debug)]
struct FingerContactContender {
    /// The minimum movement in millimeters on surface to recognize as a scroll.
    min_movement_in_mm: f32,

    /// The maximum movement in millimeters on surface to recognize as a scroll. If recognizer can
    /// not detect the scroll direction when movement more than this number, return Mismatch to
    /// end this recognizer.
    max_movement_in_mm: f32,

    /// The limit tangent for direction detect.
    limit_tangent_for_direction: f32,

    /// The initial contact position on touchpad surface.
    initial_positions: ContactPositions,
}

#[derive(Debug, PartialEq, Clone, Copy)]
enum ScrollDirection {
    Left,
    Right,
    Up,
    Down,
}

/// The state when this recognizer has detected 2 finger contact and a movement > threshold and
/// detected a scroll direction, but the gesture arena has not declared this recognizer the winner.
#[derive(Debug, PartialEq)]
struct MatchedContender {
    /// The limit tangent for direction detect.
    limit_tangent_for_direction: f32,

    /// The initial contact position on touchpad surface.
    initial_positions: ContactPositions,

    /// detected direction of scrolling.
    direction: ScrollDirection,
}

/// The state when this recognizer has won the contest.
#[derive(Debug)]
struct Winner {
    /// The limit tangent for direction detect.
    limit_tangent_for_direction: f32,

    /// detected direction of scrolling.
    direction: ScrollDirection,

    /// The last contact position on touchpad surface.
    last_positions: ContactPositions,
}

enum Error {
    FingerNotMatch,
    MustBe2Finger,
}

#[derive(Debug, PartialEq, Clone, Copy)]
struct ContactPosition {
    id: u32,
    position: Position,
}

#[derive(Debug, PartialEq)]
struct ContactPositions {
    first_contact: ContactPosition,
    second_contact: ContactPosition,
}

impl ContactPositions {
    fn from(event: &TouchpadEvent) -> Result<Self, Error> {
        let mut contact_positions: Vec<ContactPosition> = Vec::new();
        for c in &event.contacts {
            contact_positions.push(ContactPosition { id: c.id, position: c.position.clone() });
        }

        if contact_positions.len() != 2 {
            return Err(Error::MustBe2Finger);
        }
        contact_positions.sort_by_key(|a| a.id);
        Ok(ContactPositions {
            first_contact: contact_positions[0],
            second_contact: contact_positions[1],
        })
    }

    fn get_movements(&self, other: &Self) -> Result<Vec<Movement>, Error> {
        if self.first_contact.id != other.first_contact.id
            || self.second_contact.id != other.second_contact.id
        {
            return Err(Error::FingerNotMatch);
        }

        Ok(vec![
            Movement { from: self.first_contact.position, to: other.first_contact.position },
            Movement { from: self.second_contact.position, to: other.second_contact.position },
        ])
    }
}

/// a movement action, includes from position and to position.
struct Movement {
    /// From position.
    from: Position,
    /// To position.
    to: Position,
}

impl Movement {
    fn is_in_direction(
        &self,
        direction: ScrollDirection,
        limit_tangent_for_direction: f32,
    ) -> bool {
        let dx = self.to.x - self.from.x;
        let dy = self.to.y - self.from.y;

        // filter out the case movement not in given direction.
        match direction {
            ScrollDirection::Left => {
                if dx >= 0.0 {
                    return false;
                }
            }
            ScrollDirection::Right => {
                if dx <= 0.0 {
                    return false;
                }
            }
            ScrollDirection::Up => {
                if dy >= 0.0 {
                    return false;
                }
            }
            ScrollDirection::Down => {
                if dy <= 0.0 {
                    return false;
                }
            }
        }

        let (long, short) = match direction {
            ScrollDirection::Left => (dx.abs(), dy.abs()),
            ScrollDirection::Right => (dx.abs(), dy.abs()),
            ScrollDirection::Up => (dy.abs(), dx.abs()),
            ScrollDirection::Down => (dy.abs(), dx.abs()),
        };

        short < (long * limit_tangent_for_direction)
    }

    fn direction(&self, limit_tangent_for_direction: f32) -> Option<ScrollDirection> {
        let directions = [
            ScrollDirection::Left,
            ScrollDirection::Right,
            ScrollDirection::Up,
            ScrollDirection::Down,
        ];
        for d in directions {
            if self.is_in_direction(d, limit_tangent_for_direction) {
                return Some(d);
            }
        }

        None
    }

    fn has_delta_on_reverse_direction(&self, want_direction: ScrollDirection) -> bool {
        let dx = self.to.x - self.from.x;
        let dy = self.to.y - self.from.y;
        match want_direction {
            ScrollDirection::Up => dy > 0.0,
            ScrollDirection::Down => dy < 0.0,
            ScrollDirection::Left => dx > 0.0,
            ScrollDirection::Right => dx < 0.0,
        }
    }
}

impl InitialContender {
    fn into_finger_contact_contender(
        self: Box<Self>,
        initial_positions: ContactPositions,
    ) -> Box<dyn gesture_arena::Contender> {
        Box::new(FingerContactContender {
            min_movement_in_mm: self.min_movement_in_mm,
            max_movement_in_mm: self.max_movement_in_mm,
            limit_tangent_for_direction: self.limit_tangent_for_direction,
            initial_positions,
        })
    }
}

impl gesture_arena::Contender for InitialContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 2 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(2),
                max: Some(2),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons > 0 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: None,
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let current_positions = match ContactPositions::from(event) {
            Ok(positions) => positions,
            Err(_) => {
                fx_log_err!("failed to parse positions");
                return ExamineEventResult::Mismatch(Reason::Basic("failed to parse positions"));
            }
        };

        ExamineEventResult::Contender(self.into_finger_contact_contender(current_positions))
    }
}

impl FingerContactContender {
    fn into_matched_contender(
        self: Box<Self>,
        direction: ScrollDirection,
    ) -> Box<dyn gesture_arena::MatchedContender> {
        Box::new(MatchedContender {
            limit_tangent_for_direction: self.limit_tangent_for_direction,
            initial_positions: self.initial_positions,
            direction,
        })
    }
}

impl gesture_arena::Contender for FingerContactContender {
    fn examine_event(self: Box<Self>, event: &TouchpadEvent) -> ExamineEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 2 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(2),
                max: Some(2),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons > 0 {
            return ExamineEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: None,
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let current_positions = match ContactPositions::from(event) {
            Ok(positions) => positions,
            Err(_) => {
                fx_log_err!("failed to parse positions");
                return ExamineEventResult::Mismatch(Reason::Basic("failed to parse positions"));
            }
        };

        let movements = match self.initial_positions.get_movements(&current_positions) {
            // new event contact id not match old event without a finger leave surface
            // event, this is likely a bug in firmware or driver.
            Err(_) => {
                fx_log_err!("new event contact id not match old event");
                return ExamineEventResult::Mismatch(Reason::Basic(
                    "contact ids changed since last event",
                ));
            }
            Ok(m) => m,
        };

        // Both 2 fingers movement must > min_threshold.
        if movements.iter().any(|movement| {
            euclidean_distance(movement.to, movement.from) < self.min_movement_in_mm
        }) {
            return ExamineEventResult::Contender(self);
        }

        let directions: Vec<Option<ScrollDirection>> =
            movements.iter().map(|m| m.direction(self.limit_tangent_for_direction)).collect();
        if let Some(first_direction) = directions[0] {
            if directions.iter().all(|&d| d == directions[0]) {
                return ExamineEventResult::MatchedContender(
                    self.into_matched_contender(first_direction),
                );
            }
        }

        // Stop try to match if any finger movement > max_threshold still no direction
        // detected.
        if movements.iter().any(|movement| {
            euclidean_distance(movement.to, movement.from) > self.max_movement_in_mm
        }) {
            return ExamineEventResult::Mismatch(Reason::Basic(
                "too much motion without clear direction",
            ));
        }

        ExamineEventResult::Contender(self)
    }
}

impl MatchedContender {
    fn into_winner(
        self: Box<Self>,
        last_positions: ContactPositions,
    ) -> Box<dyn gesture_arena::Winner> {
        Box::new(Winner {
            limit_tangent_for_direction: self.limit_tangent_for_direction,
            direction: self.direction,
            last_positions,
        })
    }
}

impl gesture_arena::MatchedContender for MatchedContender {
    fn verify_event(self: Box<Self>, event: &TouchpadEvent) -> VerifyEventResult {
        let num_contacts = event.contacts.len();
        if num_contacts != 2 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_contacts",
                min: Some(2),
                max: Some(2),
                actual: num_contacts,
            }));
        }

        let num_pressed_buttons = event.pressed_buttons.len();
        if num_pressed_buttons > 0 {
            return VerifyEventResult::Mismatch(Reason::DetailedUint(DetailedReasonUint {
                criterion: "num_pressed_buttons",
                min: None,
                max: Some(0),
                actual: num_pressed_buttons,
            }));
        }

        let current_positions = match ContactPositions::from(event) {
            Ok(positions) => positions,
            Err(_) => {
                fx_log_err!("failed to parse positions");
                return VerifyEventResult::Mismatch(Reason::Basic("failed to parse positions"));
            }
        };
        let movements = match self.initial_positions.get_movements(&current_positions) {
            // new event contact id not match old event without a finger leave surface
            // event, this is likely a bug in firmware or driver.
            Err(_) => {
                fx_log_err!("new event contact id not match old event");
                return VerifyEventResult::Mismatch(Reason::Basic(
                    "contact ids changed since last event",
                ));
            }
            Ok(m) => m,
        };

        let directions: Vec<Option<ScrollDirection>> =
            movements.into_iter().map(|m| m.direction(self.limit_tangent_for_direction)).collect();

        if directions.iter().all(|&d| d == Some(self.direction)) {
            return VerifyEventResult::MatchedContender(self);
        }

        VerifyEventResult::Mismatch(Reason::Basic("contacts moved in different directions"))
    }

    fn process_buffered_events(
        self: Box<Self>,
        events: Vec<TouchpadEvent>,
    ) -> ProcessBufferedEventsResult {
        let mut mouse_events: Vec<MouseEvent> = Vec::new();

        for pair in events.windows(2) {
            let old_positions = match ContactPositions::from(&pair[0]) {
                Ok(positions) => positions,
                Err(_) => ContactPositions {
                    // Likely a bug in `GestureArena`, because all event here has been
                    // verified in `examine_event` or `verify_event`.
                    first_contact: ContactPosition { id: 0, position: Position { x: 0.0, y: 0.0 } },
                    second_contact: ContactPosition {
                        id: 0,
                        position: Position { x: 0.0, y: 0.0 },
                    },
                },
            };
            mouse_events.push(touchpad_event_to_mouse_scroll_event(
                self.direction,
                old_positions,
                &pair[1],
            ));
        }

        let last_positions = match ContactPositions::from(&events[events.len() - 1]) {
            Ok(positions) => positions,
            Err(_) => ContactPositions {
                // Likely a bug in `GestureArena`, because all event here has been
                // verified in `examine_event` or `verify_event`.
                first_contact: ContactPosition { id: 0, position: Position { x: 0.0, y: 0.0 } },
                second_contact: ContactPosition { id: 0, position: Position { x: 0.0, y: 0.0 } },
            },
        };

        ProcessBufferedEventsResult {
            generated_events: mouse_events,
            winner: Some(self.into_winner(last_positions)),
            recognized_gesture: RecognizedGesture::Scroll,
        }
    }
}

impl gesture_arena::Winner for Winner {
    fn process_new_event(self: Box<Self>, event: TouchpadEvent) -> ProcessNewEventResult {
        match u8::try_from(event.contacts.len()).unwrap_or(u8::MAX) {
            0 => ProcessNewEventResult::EndGesture(
                EndGestureEvent::NoEvent,
                Reason::Basic("wanted 2 contacts, got 0"),
            ),
            2 => {
                if event.pressed_buttons.len() > 0 {
                    return ProcessNewEventResult::EndGesture(
                        EndGestureEvent::UnconsumedEvent(event),
                        Reason::Basic("wanted 0 pressed buttons"),
                    );
                }

                let positions = match ContactPositions::from(&event) {
                    Ok(positions) => positions,
                    Err(_) => {
                        fx_log_err!("failed to parse positions");
                        return ProcessNewEventResult::EndGesture(
                            EndGestureEvent::UnconsumedEvent(event),
                            Reason::Basic("failed to parse positions"),
                        );
                    }
                };

                let movements = match self.last_positions.get_movements(&positions) {
                    // new event contact id not match old event without a finger leave
                    // surface event, this is likely a bug in firmware or driver.
                    Err(_) => {
                        fx_log_err!("new event contact id not match old event");
                        return ProcessNewEventResult::EndGesture(
                            EndGestureEvent::UnconsumedEvent(event),
                            Reason::Basic("contact ids changed since last event"),
                        );
                    }
                    Ok(m) => m,
                };

                if movements.iter().any(|m| m.has_delta_on_reverse_direction(self.direction)) {
                    return ProcessNewEventResult::EndGesture(
                        EndGestureEvent::UnconsumedEvent(event),
                        Reason::Basic("inconsistent direction"),
                    );
                }

                ProcessNewEventResult::ContinueGesture(
                    Some(touchpad_event_to_mouse_scroll_event(
                        self.direction,
                        self.last_positions,
                        &event,
                    )),
                    Box::new(Winner {
                        limit_tangent_for_direction: self.limit_tangent_for_direction,
                        direction: self.direction,
                        last_positions: positions,
                    }),
                )
            }
            1 => ProcessNewEventResult::EndGesture(
                EndGestureEvent::UnconsumedEvent(event),
                Reason::Basic("wanted 2 contacts, got 1"),
            ),
            3.. => ProcessNewEventResult::EndGesture(
                EndGestureEvent::UnconsumedEvent(event),
                Reason::Basic("wanted 2 contacts, got >= 3"),
            ),
        }
    }
}

fn wheel_delta_mm(delta: f32) -> Option<mouse_binding::WheelDelta> {
    Some(mouse_binding::WheelDelta {
        raw_data: mouse_binding::RawWheelDelta::Millimeters(delta),
        physical_pixel: None,
    })
}

// filter out motion not in given direction.
fn filter_off_direction_movement(
    direction: ScrollDirection,
    offset_v: f32,
    offset_h: f32,
) -> (Option<mouse_binding::WheelDelta>, Option<mouse_binding::WheelDelta>) {
    match direction {
        ScrollDirection::Left => {
            if offset_h > 0.0 {
                (None, wheel_delta_mm(0.0))
            } else {
                (None, wheel_delta_mm(offset_h))
            }
        }
        ScrollDirection::Right => {
            if offset_h < 0.0 {
                (None, wheel_delta_mm(0.0))
            } else {
                (None, wheel_delta_mm(offset_h))
            }
        }
        ScrollDirection::Up => {
            if offset_v > 0.0 {
                (wheel_delta_mm(0.0), None)
            } else {
                (wheel_delta_mm(offset_v), None)
            }
        }
        ScrollDirection::Down => {
            if offset_v < 0.0 {
                (wheel_delta_mm(0.0), None)
            } else {
                (wheel_delta_mm(offset_v), None)
            }
        }
    }
}

fn touchpad_event_to_mouse_scroll_event(
    direction: ScrollDirection,
    old_positions: ContactPositions,
    new_event: &TouchpadEvent,
) -> MouseEvent {
    // Pre-check already ensure contact fingers in new event are same with the old event.
    // Use avg(finger1 movement + finger2 movement) as scroll offset.

    // To compute the offset, for example the Y offset
    // Y offset = avg(finger1 movement + finger2 movement) =
    // ((new_finger1.y - old_finger1.y) + (new_finger2.y - old_finger2.y)) / 2 =
    // (new_finger1.y + new_finger2.y - old_finger1.y - old_finger2.y) / 2

    let offset_v = (new_event.contacts[0].position.y + new_event.contacts[1].position.y
        - old_positions.first_contact.position.y
        - old_positions.second_contact.position.y)
        / 2.0;
    let offset_h = (new_event.contacts[0].position.x + new_event.contacts[1].position.x
        - old_positions.first_contact.position.x
        - old_positions.second_contact.position.x)
        / 2.0;

    // If offset not in given ScrollDirection (maybe happen in early events), result 0
    // scroll offset.
    let (wheel_delta_v, wheel_delta_h) =
        filter_off_direction_movement(direction, offset_v, offset_h);
    MouseEvent {
        timestamp: new_event.timestamp,
        mouse_data: mouse_binding::MouseEvent::new(
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: Position { x: 0.0, y: 0.0 },
            }),
            wheel_delta_v,
            wheel_delta_h,
            mouse_binding::MousePhase::Wheel,
            /* affected_buttons= */ hashset! {},
            /* pressed_buttons= */ hashset! {},
            Some(mouse_binding::PrecisionScroll::Yes),
        ),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::touch_binding, assert_matches::assert_matches, fuchsia_zircon as zx,
        test_case::test_case,
    };

    const MIN_MOVEMENT_IN_MM: f32 = 10.0;
    const MAX_MOVEMENT_IN_MM: f32 = 20.0;
    const LIMIT_TANGENT_FOR_DIRECTION: f32 = 0.2;

    #[test_case(Position {x: 0.0, y: 1.0}, Some(ScrollDirection::Down); "scroll down")]
    #[test_case(Position {x: 0.0, y: -1.0}, Some(ScrollDirection::Up); "scroll up")]
    #[test_case(Position {x: 1.0, y: 0.0}, Some(ScrollDirection::Right); "scroll right")]
    #[test_case(Position {x: -1.0, y: 0.0}, Some(ScrollDirection::Left); "scroll left")]
    #[test_case(Position {x: 1.0, y: 1.0}, None; "scroll No 45°")]
    #[test_case(
        Position {x: 0.9, y: 5.0}, Some(ScrollDirection::Down);
    "scroll down inside tolerated right")]
    #[test_case(
        Position {x: -0.9, y: 5.0}, Some(ScrollDirection::Down);
        "scroll down inside tolerated left")]
    #[test_case(Position {x: 1.0, y: 5.0}, None; "scroll No outside tolerated right")]
    #[test_case(Position {x: -1.0, y: 5.0}, None; "scroll No outside tolerated left")]
    #[fuchsia::test]
    fn direction(to: Position, want: Option<ScrollDirection>) {
        let movement = Movement { from: Position { x: 0.0, y: 0.0 }, to };
        let got = movement.direction(LIMIT_TANGENT_FOR_DIRECTION);
        pretty_assertions::assert_eq!(want, got);
    }

    fn make_touch_contact(id: u32, position: Position) -> touch_binding::TouchContact {
        touch_binding::TouchContact { id, position, pressure: None, contact_size: None }
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0})
        ],
    };"button down")]
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
        ],
    };"1 fingers")]
    #[fuchsia::test]
    fn initial_contender_examine_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(InitialContender {
            min_movement_in_mm: MIN_MOVEMENT_IN_MM,
            max_movement_in_mm: MAX_MOVEMENT_IN_MM,
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[fuchsia::test]
    fn initial_contender_examine_event_finger_contact_contender() {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(InitialContender {
            min_movement_in_mm: MIN_MOVEMENT_IN_MM,
            max_movement_in_mm: MAX_MOVEMENT_IN_MM,
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
        });

        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position { x: 1.0, y: 1.0 }),
                make_touch_contact(2, Position { x: 5.0, y: 5.0 }),
            ],
        };
        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![1],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0}),
        ],
    };"button down")]
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
        ],
    };"1 fingers")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 20.0, y: 20.0}),
            make_touch_contact(2, Position{x: 25.0, y: 25.0}),
        ],
    };"> max movement no direction")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 21.0}),
            make_touch_contact(2, Position{x: 5.0, y: -16.0}),
        ],
    };"> max movement different direction")]
    #[fuchsia::test]
    fn finger_contact_contender_examine_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            min_movement_in_mm: MIN_MOVEMENT_IN_MM,
            max_movement_in_mm: MAX_MOVEMENT_IN_MM,
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Mismatch(_));
    }

    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            make_touch_contact(2, Position{x: 5.0, y: 5.0}),
        ],
    };"finger hold")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 2.0, y: 1.0}),
            make_touch_contact(2, Position{x: 6.0, y: 5.0}),
        ],
    };"2 finger move less than threshold")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 12.0, y: 1.0}),
            make_touch_contact(2, Position{x: 6.0, y: 5.0}),
        ],
    };"both finger move, 1 finger move less than threshold")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 10.0, y: 10.0}),
            make_touch_contact(2, Position{x: 15.0, y: 15.0}),
        ],
    };"no direction")]
    #[test_case(TouchpadEvent{
        timestamp: zx::Time::ZERO,
        pressed_buttons: vec![],
        contacts: vec![
            make_touch_contact(1, Position{x: 1.0, y: 11.0}),
            make_touch_contact(2, Position{x: 5.0, y: -6.0}),
        ],
    };"different direction")]
    #[fuchsia::test]
    fn finger_contact_contender_examine_event_finger_contact_contender(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            min_movement_in_mm: MIN_MOVEMENT_IN_MM,
            max_movement_in_mm: MAX_MOVEMENT_IN_MM,
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
        });

        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::Contender(_));
    }

    #[fuchsia::test]
    fn finger_contact_contender_examine_event_matched_contender() {
        let initial_positions = ContactPositions {
            first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
            second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
        };
        let contender: Box<dyn gesture_arena::Contender> = Box::new(FingerContactContender {
            min_movement_in_mm: MIN_MOVEMENT_IN_MM,
            max_movement_in_mm: MAX_MOVEMENT_IN_MM,
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions,
        });

        let event = TouchpadEvent {
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position { x: 1.0, y: 11.0 }),
                make_touch_contact(2, Position { x: 5.0, y: 15.0 }),
            ],
        };
        let got = contender.examine_event(&event);
        assert_matches!(got, ExamineEventResult::MatchedContender(_));
    }

    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
        };"button down")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![],
        };"0 fingers")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            ],
        };"1 fingers")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 1.0}),
                make_touch_contact(2, Position{x: 5.0, y: 5.0}),
            ],
        };"finger hold")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 10.0, y: 10.0}),
                make_touch_contact(2, Position{x: 15.0, y: 15.0}),
            ],
        };"no direction")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 11.0}),
                make_touch_contact(2, Position{x: 5.0, y: -6.0}),
            ],
        };"different direction")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 11.0}),
                make_touch_contact(2, Position{x: 5.0, y: 16.0}),
            ],
        };"wrong direction")]
    #[fuchsia::test]
    fn matched_contender_verify_event_mismatch(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
            direction: ScrollDirection::Up,
        });

        let got = contender.verify_event(&event);
        assert_matches!(got, VerifyEventResult::Mismatch(_));
    }

    #[test_case(TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: -11.0}),
                make_touch_contact(2, Position{x: 5.0, y: -5.0}),
            ],
        };"on direction")]
    #[fuchsia::test]
    fn matched_contender_verify_event_matched_contender(event: TouchpadEvent) {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
            direction: ScrollDirection::Up,
        });

        let got = contender.verify_event(&event);
        assert_matches!(got, VerifyEventResult::MatchedContender(_));
    }

    #[fuchsia::test]
    fn matched_contender_process_buffered_events() {
        let contender: Box<dyn gesture_arena::MatchedContender> = Box::new(MatchedContender {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            initial_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
            direction: ScrollDirection::Up,
        });

        let got = contender.process_buffered_events(vec![
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(1),
                pressed_buttons: vec![],
                contacts: vec![
                    make_touch_contact(1, Position { x: 1.0, y: 1.0 }),
                    make_touch_contact(2, Position { x: 5.0, y: 5.0 }),
                ],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(2),
                pressed_buttons: vec![],
                contacts: vec![
                    make_touch_contact(1, Position { x: 1.0, y: 1.0 }),
                    make_touch_contact(2, Position { x: 5.0, y: 5.0 }),
                ],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(3),
                pressed_buttons: vec![],
                contacts: vec![
                    make_touch_contact(1, Position { x: 1.0, y: 2.0 }),
                    make_touch_contact(2, Position { x: 5.0, y: 6.0 }),
                ],
            },
            TouchpadEvent {
                timestamp: zx::Time::from_nanos(4),
                pressed_buttons: vec![],
                contacts: vec![
                    make_touch_contact(1, Position { x: 1.0, y: -11.0 }),
                    make_touch_contact(2, Position { x: 5.0, y: -5.0 }),
                ],
            },
        ]);

        pretty_assertions::assert_eq!(
            got.generated_events,
            vec![
                // Finger hold, expect no scroll delta.
                MouseEvent {
                    timestamp: zx::Time::from_nanos(2),
                    mouse_data: mouse_binding::MouseEvent::new(
                        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 },
                        }),
                        /* wheel_delta_v= */ wheel_delta_mm(0.0),
                        /* wheel_delta_h= */ None,
                        mouse_binding::MousePhase::Wheel,
                        /* affected_buttons= */ hashset! {},
                        /* pressed_buttons= */ hashset! {},
                        /* is_precision_scroll= */ Some(mouse_binding::PrecisionScroll::Yes),
                    ),
                },
                // Finger move to wrong direction, expect no scroll delta.
                MouseEvent {
                    timestamp: zx::Time::from_nanos(3),
                    mouse_data: mouse_binding::MouseEvent::new(
                        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 },
                        }),
                        /* wheel_delta_v= */ wheel_delta_mm(0.0),
                        /* wheel_delta_h= */ None,
                        mouse_binding::MousePhase::Wheel,
                        /* affected_buttons= */ hashset! {},
                        /* pressed_buttons= */ hashset! {},
                        /* is_precision_scroll= */ Some(mouse_binding::PrecisionScroll::Yes),
                    ),
                },
                MouseEvent {
                    timestamp: zx::Time::from_nanos(4),
                    mouse_data: mouse_binding::MouseEvent::new(
                        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                            counts: Position { x: 0.0, y: 0.0 },
                            millimeters: Position { x: 0.0, y: 0.0 },
                        }),
                        /* wheel_delta_v= */ wheel_delta_mm(-12.0),
                        /* wheel_delta_h= */ None,
                        mouse_binding::MousePhase::Wheel,
                        /* affected_buttons= */ hashset! {},
                        /* pressed_buttons= */ hashset! {},
                        /* is_precision_scroll= */ Some(mouse_binding::PrecisionScroll::Yes),
                    ),
                },
            ]
        );
        pretty_assertions::assert_eq!(got.recognized_gesture, RecognizedGesture::Scroll);
    }

    #[fuchsia::test]
    fn winner_process_new_event_end_gesture_none() {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(Winner {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            direction: ScrollDirection::Up,
            last_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
        });
        let event =
            TouchpadEvent { timestamp: zx::Time::ZERO, pressed_buttons: vec![], contacts: vec![] };
        let got = winner.process_new_event(event);

        assert_matches!(got, ProcessNewEventResult::EndGesture(EndGestureEvent::NoEvent, _reason));
    }

    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![1],
            contacts: vec![make_touch_contact(1, Position{x: 1.0, y: 1.0})],
        };"button down")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 1.0}),
            ],
        };"1 fingers")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 11.0}),
                make_touch_contact(2, Position{x: 5.0, y: -6.0}),
            ],
        };"1 finger reverse direction")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 11.0}),
                make_touch_contact(2, Position{x: 5.0, y: 16.0}),
            ],
        };"2 fingers reverse direction")]
    #[fuchsia::test]
    fn winner_process_new_event_end_gesture_some(event: TouchpadEvent) {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(Winner {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            direction: ScrollDirection::Up,
            last_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
        });
        let got = winner.process_new_event(event);

        assert_matches!(
            got,
            ProcessNewEventResult::EndGesture(EndGestureEvent::UnconsumedEvent(_), _reason)
        );
    }

    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: 1.0}),
                make_touch_contact(2, Position{x: 5.0, y: 5.0}),
            ],
        } => MouseEvent {
            timestamp: zx::Time::from_nanos(0),
            mouse_data: mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(
                    mouse_binding::RelativeLocation {
                        counts: Position { x: 0.0, y: 0.0 },
                        millimeters: Position { x: 0.0, y: 0.0 },
                    }
                ),
                wheel_delta_v: wheel_delta_mm(0.0),
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Wheel,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
                is_precision_scroll: Some(mouse_binding::PrecisionScroll::Yes),
            },
        };"finger hold")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 10.0, y: -10.0}),
                make_touch_contact(2, Position{x: 15.0, y: -15.0}),
            ],
        } => MouseEvent {
            timestamp: zx::Time::from_nanos(0),
            mouse_data: mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(
                    mouse_binding::RelativeLocation {
                        counts: Position { x: 0.0, y: 0.0 },
                        millimeters: Position { x: 0.0, y: 0.0 },
                    }
                ),
                wheel_delta_v: wheel_delta_mm(-15.5),
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Wheel,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
                is_precision_scroll: Some(mouse_binding::PrecisionScroll::Yes),
            },
        };"direction contact1 only")]
    #[test_case(
        TouchpadEvent{
            timestamp: zx::Time::ZERO,
            pressed_buttons: vec![],
            contacts: vec![
                make_touch_contact(1, Position{x: 1.0, y: -11.0}),
                make_touch_contact(2, Position{x: 5.0, y: -5.0}),
            ],
        } => MouseEvent {
            timestamp: zx::Time::from_nanos(0),
            mouse_data: mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(
                    mouse_binding::RelativeLocation {
                        counts: Position { x: 0.0, y: 0.0 },
                        millimeters: Position { x: 0.0, y: 0.0 },
                    }
                ),
                wheel_delta_v: wheel_delta_mm(-11.0),
                wheel_delta_h: None,
                phase: mouse_binding::MousePhase::Wheel,
                affected_buttons: hashset! {},
                pressed_buttons: hashset! {},
                is_precision_scroll: Some(mouse_binding::PrecisionScroll::Yes),
            },
        };"on direction")]
    #[fuchsia::test]
    fn winner_process_new_event_continue_gesture(event: TouchpadEvent) -> MouseEvent {
        let winner: Box<dyn gesture_arena::Winner> = Box::new(Winner {
            limit_tangent_for_direction: LIMIT_TANGENT_FOR_DIRECTION,
            direction: ScrollDirection::Up,
            last_positions: ContactPositions {
                first_contact: ContactPosition { id: 1, position: Position { x: 1.0, y: 1.0 } },
                second_contact: ContactPosition { id: 2, position: Position { x: 5.0, y: 5.0 } },
            },
        });
        let got = winner.process_new_event(event);

        // This not able to use `assert_eq` or `assert_matches` because:
        // - assert_matches: floating point is not allow in match.
        // - assert_eq: `ContinueGesture` has Box dyn type.
        match got {
            ProcessNewEventResult::EndGesture(..) => {
                panic!("Got {:?}, want ContinueGesture()", got)
            }
            ProcessNewEventResult::ContinueGesture(got_mouse_event, _) => got_mouse_event.unwrap(),
        }
    }
}
