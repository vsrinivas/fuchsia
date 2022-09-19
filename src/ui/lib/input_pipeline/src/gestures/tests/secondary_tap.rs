// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod test {
    use {
        super::super::utils,
        crate::{gestures::args, input_device, mouse_binding, touch_binding, Position},
        assert_matches::assert_matches,
        fuchsia_zircon as zx,
        maplit::hashset,
        pretty_assertions::assert_eq,
    };

    fn touchpad_event(positions: Vec<Position>, time: zx::Time) -> input_device::InputEvent {
        let injector_contacts: Vec<touch_binding::TouchContact> = positions
            .iter()
            .enumerate()
            .map(|(i, p)| touch_binding::TouchContact {
                id: i as u32,
                position: *p,
                contact_size: None,
                pressure: None,
            })
            .collect();

        input_device::InputEvent {
            event_time: time,
            ..utils::make_touchpad_event(touch_binding::TouchpadEvent {
                injector_contacts,
                pressed_buttons: hashset!(),
            })
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn secondary_tap() {
        let inputs = vec![
            // contact.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }, Position { x: 5.0, y: 3.0 }],
                zx::Time::from_nanos(0),
            ),
            // finger lift.
            touchpad_event(vec![], zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 2)),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 2);
        assert_eq!(got[0].as_slice(), []);
        assert_matches!(got[1].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
          assert_eq!(pressed_button_a, &hashset! {2});
          assert_eq!(affected_button_a, &hashset! {2});
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
          assert_eq!(pressed_button_b, &hashset! {});
          assert_eq!(affected_button_b, &hashset! {2});
          assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn place_1st_finger_then_2nd_finger_lift_2fingers() {
        let inputs = vec![
            // place 1st finger.
            touchpad_event(vec![Position { x: 2.0, y: 3.0 }], zx::Time::from_nanos(0)),
            // place 2nd finger.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }, Position { x: 5.0, y: 3.0 }],
                zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 10 * 2),
            ),
            // finger lift.
            touchpad_event(vec![], zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 10 * 3)),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 3);
        assert_eq!(got[0].as_slice(), []);
        assert_eq!(got[1].as_slice(), []);
        assert_matches!(got[2].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
          assert_eq!(pressed_button_a, &hashset! {2});
          assert_eq!(affected_button_a, &hashset! {2});
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
          assert_eq!(pressed_button_b, &hashset! {});
          assert_eq!(affected_button_b, &hashset! {2});
          assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn place_2fingers_then_lift_1_then_another() {
        let inputs = vec![
            // contact.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }, Position { x: 5.0, y: 3.0 }],
                zx::Time::from_nanos(0),
            ),
            // 1 finger lift.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }],
                zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 10 * 2),
            ),
            // lift another finger.
            touchpad_event(vec![], zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 10 * 3)),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 3);
        assert_eq!(got[0].as_slice(), []);
        assert_eq!(got[1].as_slice(), []);
        assert_matches!(got[2].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
          assert_eq!(pressed_button_a, &hashset! {2});
          assert_eq!(affected_button_a, &hashset! {2});
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
          assert_eq!(pressed_button_b, &hashset! {});
          assert_eq!(affected_button_b, &hashset! {2});
          assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn secondary_tap_timeout() {
        let inputs = vec![
            // contact.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }, Position { x: 5.0, y: 3.0 }],
                zx::Time::from_nanos(0),
            ),
            // finger lift.
            touchpad_event(vec![], zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() + 1)),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 2);
        assert_eq!(got[0].as_slice(), []);
        assert_eq!(got[1].as_slice(), []);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn place_2fingers_then_lift_1_then_another_after_timeout() {
        let inputs = vec![
            // contact.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }, Position { x: 5.0, y: 3.0 }],
                zx::Time::from_nanos(0),
            ),
            // 1 finger lift.
            touchpad_event(
                vec![Position { x: 2.0, y: 3.0 }],
                zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() / 10 * 2),
            ),
            // lift another finger.
            touchpad_event(vec![], zx::Time::from_nanos(args::TAP_TIMEOUT.into_nanos() + 1)),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 3);
        assert_eq!(got[0].as_slice(), []);
        assert_eq!(got[1].as_slice(), []);
        assert_eq!(got[2].as_slice(), []);
    }
}
