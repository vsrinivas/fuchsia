// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {
        super::super::utils,
        crate::{input_device, mouse_binding, touch_binding, Position},
        assert_matches::assert_matches,
        maplit::hashset,
        pretty_assertions::assert_eq,
        std::collections::HashSet,
    };

    fn touchpad_event(
        positions: Vec<Position>,
        pressed_buttons: HashSet<mouse_binding::MouseButton>,
    ) -> input_device::InputEvent {
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

        utils::make_touchpad_event(touch_binding::TouchpadEvent {
            injector_contacts,
            pressed_buttons,
        })
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn click_not_lift_finger() {
        let inputs = vec![
            touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {1}),
            touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 2);
        assert_eq!(got[0].as_slice(), []);
        assert_matches!(got[1].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
          assert_eq!(pressed_button_a, &hashset! {1});
          assert_eq!(affected_button_a, &hashset! {1});
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
          assert_eq!(pressed_button_b, &hashset! {});
          assert_eq!(affected_button_b, &hashset! {1});
          assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn click_lift_finger() {
        let inputs = vec![
            touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {1}),
            touchpad_event(vec![], hashset! {}),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 2);
        assert_eq!(got[0].as_slice(), []);
        assert_matches!(got[1].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
          assert_eq!(pressed_button_a, &hashset! {1});
          assert_eq!(affected_button_a, &hashset! {1});
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
          assert_eq!(pressed_button_b, &hashset! {});
          assert_eq!(affected_button_b, &hashset! {1});
          assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    mod chain {
        use {
            super::super::super::utils,
            super::touchpad_event,
            crate::{gestures::args, input_device, mouse_binding, Position},
            assert_matches::assert_matches,
            maplit::hashset,
            pretty_assertions::assert_eq,
            test_util::{assert_gt, assert_near},
        };

        #[fuchsia::test(allow_stalls = false)]
        async fn click_then_lift_finger() {
            let inputs = vec![
                // button down.
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {1}),
                // button up, finger still contacting for 2 events.
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
                // finger lift, ensure no extra tap.
                touchpad_event(vec![], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 4);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
              utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
              assert_eq!(pressed_button_a, &hashset! {1});
              assert_eq!(affected_button_a, &hashset! {1});
              assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
              assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
              assert_eq!(pressed_button_b, &hashset! {});
              assert_eq!(affected_button_b, &hashset! {1});
              assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
            });
            assert_eq!(got[2].as_slice(), []);
            assert_eq!(got[3].as_slice(), []);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn contact_click_then_lift_finger() {
            let inputs = vec![
                // contact.
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
                // button down.
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {1}),
                // button up, finger still contacting for 2 events.
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
                touchpad_event(vec![Position { x: 2.0, y: 3.0 }], hashset! {}),
                // finger lift, ensure no extra tap.
                touchpad_event(vec![], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 5);
            assert_eq!(got[0].as_slice(), []);
            assert_eq!(got[1].as_slice(), []);
            assert_matches!(got[2].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
              utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
              assert_eq!(pressed_button_a, &hashset! {1});
              assert_eq!(affected_button_a, &hashset! {1});
              assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
              assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
              assert_eq!(pressed_button_b, &hashset! {});
              assert_eq!(affected_button_b, &hashset! {1});
              assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
            });
            assert_eq!(got[3].as_slice(), []);
            assert_eq!(got[4].as_slice(), []);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn click_then_motion() {
            let pos1 = Position { x: 2_000.0, y: 3_000.0 };
            let pos2 = pos1
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let inputs = vec![
                touchpad_event(vec![pos1], hashset! {1}),
                touchpad_event(vec![pos1], hashset! {}),
                touchpad_event(vec![pos1], hashset! {}),
                touchpad_event(vec![pos2], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 4);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
              utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
              assert_eq!(pressed_button_a, &hashset! {1});
              assert_eq!(affected_button_a, &hashset! {1});
              assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
              assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
              assert_eq!(pressed_button_b, &hashset! {});
              assert_eq!(affected_button_b, &hashset! {1});
              assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
            });
            assert_eq!(got[2].as_slice(), []);
            assert_matches!(got[3].as_slice(), [
              utils::expect_mouse_event!(phase: phase, location: location),
            ] => {
              assert_eq!(phase, &mouse_binding::MousePhase::Move);
              assert_near!(location.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location.millimeters.y, 0.0);
            });
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn click_then_place_2nd_finger_then_scroll() {
            let finger1_pos1_um = Position { x: 2_000.0, y: 3_000.0 };
            let finger1_pos2_um = finger1_pos1_um.clone();
            let finger1_pos3_um = finger1_pos1_um.clone();
            let finger2_pos3_um = Position { x: 5_000.0, y: 3_000.0 };
            let finger1_pos4_um = finger1_pos3_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger2_pos4_um = finger2_pos3_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let inputs = vec![
                touchpad_event(vec![finger1_pos1_um], hashset! {1}),
                touchpad_event(vec![finger1_pos2_um], hashset! {}),
                touchpad_event(vec![finger1_pos3_um, finger2_pos3_um], hashset! {}),
                touchpad_event(vec![finger1_pos4_um, finger2_pos4_um], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 4);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
              utils::expect_mouse_event!(phase: phase_b, pressed_buttons: pressed_button_b, affected_buttons: affected_button_b, location: location_b),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
              assert_eq!(pressed_button_a, &hashset! {1});
              assert_eq!(affected_button_a, &hashset! {1});
              assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
              assert_eq!(phase_b, &mouse_binding::MousePhase::Up);
              assert_eq!(pressed_button_b, &hashset! {});
              assert_eq!(affected_button_b, &hashset! {1});
              assert_eq!(location_b, &utils::NO_MOVEMENT_LOCATION);
            });
            assert_eq!(got[2].as_slice(), []);
            assert_matches!(got[3].as_slice(), [
              utils::expect_mouse_event!(phase: phase, delta_v: delta_v, delta_h: delta_h, location: location),
            ] => {
              assert_eq!(phase, &mouse_binding::MousePhase::Wheel);
              assert_matches!(delta_v, utils::extract_wheel_delta!(delta) => {
                assert_gt!(*delta, 0.0);
              });
              assert_eq!(*delta_h, None);
              assert_eq!(location, &utils::NO_MOVEMENT_LOCATION);
            });
        }
    }
}
