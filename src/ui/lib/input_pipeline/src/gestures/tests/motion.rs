// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {
        super::super::utils,
        crate::{gestures::args, input_device, mouse_binding, touch_binding, Position},
        assert_matches::assert_matches,
        maplit::hashset,
        pretty_assertions::assert_eq,
        std::collections::HashSet,
        test_util::{assert_gt, assert_lt, assert_near},
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
    async fn motion_keep_contact() {
        let pos0_um = Position { x: 2_000.0, y: 3_000.0 };
        let pos1_um = Position { x: 2_100.0, y: 3_000.0 };
        let pos2_um = pos1_um
            + Position { x: 0.0, y: args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0 };
        let pos3_um = pos2_um.clone();
        let inputs = vec![
            touchpad_event(vec![pos0_um], hashset! {}),
            touchpad_event(vec![pos1_um], hashset! {}),
            touchpad_event(vec![pos2_um], hashset! {}),
            touchpad_event(vec![pos3_um], hashset! {}),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 4);
        assert_eq!(got[0].as_slice(), []);
        assert_eq!(got[1].as_slice(), []);
        assert_lt!(
            pos1_um.x - pos0_um.x,
            args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0
        );
        assert_matches!(got[2].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, location: location_a),
          utils::expect_mouse_event!(phase: phase_b, location: location_b),
        ] => {
          // the 2nd event movement < threshold but 3rd event movement > threshold,
          // then the 2nd event got unbuffered and recognized as a mouse move.
          assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
          assert_gt!(location_a.millimeters.x, 0.0);
          assert_near!(location_a.millimeters.y, 0.0, utils::EPSILON);
          assert_eq!(phase_b, &mouse_binding::MousePhase::Move);
          assert_near!(location_b.millimeters.x, 0.0, utils::EPSILON);
          assert_gt!(location_b.millimeters.y, 0.0);
        });
        assert_matches!(got[3].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, location: location_a),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
          assert_eq!(location_a, &utils::NO_MOVEMENT_LOCATION);
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn motion_then_lift() {
        let pos0_um = Position { x: 2_000.0, y: 3_000.0 };
        let pos1_um = pos0_um
            + Position {
                x: 0.0,
                y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
            };
        let inputs = vec![
            touchpad_event(vec![pos0_um], hashset! {}),
            touchpad_event(vec![pos1_um], hashset! {}),
            touchpad_event(vec![], hashset! {}),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 3);
        assert_eq!(got[0].as_slice(), []);
        assert_matches!(got[1].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, location: location_a),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
          assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
          assert_gt!(location_a.millimeters.y, 0.0);
        });
        // Does _not_ trigger tap.
        assert_eq!(got[2].as_slice(), []);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn motion_then_click() {
        let pos1 = Position { x: 2_000.0, y: 3_000.0 };
        let pos2 = pos1
            + Position {
                x: 0.0,
                y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
            };
        let inputs = vec![
            touchpad_event(vec![pos1], hashset! {}),
            touchpad_event(vec![pos2], hashset! {}),
            touchpad_event(vec![pos2], hashset! {1}),
            touchpad_event(vec![pos2], hashset! {}),
        ];
        let got = utils::run_gesture_arena_test(inputs).await;

        assert_eq!(got.len(), 4);
        assert_eq!(got[0].as_slice(), []);
        assert_matches!(got[1].as_slice(), [
          utils::expect_mouse_event!(phase: phase_a, location: location_a),
        ] => {
          assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
          assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
          assert_gt!(location_a.millimeters.y, 0.0);
        });
        assert_eq!(got[2].as_slice(), []);
        assert_matches!(got[3].as_slice(), [
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
        async fn motion_then_place_2nd_finger_then_lift() {
            let finger1_pos0_um = Position { x: 2_000.0, y: 3_000.0 };
            let finger1_pos1_um = finger1_pos0_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger1_pos2_um = finger1_pos1_um.clone();
            let finger2_pos2_um = Position { x: 5_000.0, y: 5_000.0 };
            let inputs = vec![
                touchpad_event(vec![finger1_pos0_um], hashset! {}),
                touchpad_event(vec![finger1_pos1_um], hashset! {}),
                touchpad_event(vec![finger1_pos2_um, finger2_pos2_um], hashset! {}),
                touchpad_event(vec![], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 4);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, location: location_a),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
              assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location_a.millimeters.y, 0.0);
            });
            assert_eq!(got[2].as_slice(), []);
            // Does _not_ trigger secondary-tap detector.
            assert_eq!(got[3].as_slice(), []);
        }

        // TODO(fxbug.dev/99510): motion then 2 finger click should generate secondary click.
        #[fuchsia::test(allow_stalls = false)]
        async fn motion_then_place_2nd_finger_then_click() {
            let finger1_pos0_um = Position { x: 2_000.0, y: 3_000.0 };
            let finger1_pos1_um = finger1_pos0_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger1_pos2_um = finger1_pos1_um.clone();
            let finger2_pos2_um = Position { x: 5_000.0, y: 5_000.0 };
            let finger1_pos3_um = finger1_pos2_um.clone();
            let finger2_pos3_um = finger2_pos2_um.clone();
            let inputs = vec![
                touchpad_event(vec![finger1_pos0_um], hashset! {}),
                touchpad_event(vec![finger1_pos1_um], hashset! {}),
                touchpad_event(vec![finger1_pos2_um, finger2_pos2_um], hashset! {1}),
                touchpad_event(vec![finger1_pos3_um, finger2_pos3_um], hashset! {}),
                touchpad_event(vec![], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 5);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, location: location_a),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
              assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location_a.millimeters.y, 0.0);
            });
            assert_eq!(got[2].as_slice(), []);
            assert_eq!(got[3].as_slice(), []);
            assert_eq!(got[4].as_slice(), []);
        }

        #[fuchsia::test(allow_stalls = false)]
        async fn motion_then_place_2nd_finger_then_scroll() {
            let finger1_pos0_um = Position { x: 2_000.0, y: 3_000.0 };
            let finger1_pos1_um = finger1_pos0_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger1_pos2_um = finger1_pos1_um.clone();
            let finger2_pos2_um = Position { x: 5_000.0, y: 5_000.0 };
            let finger1_pos3_um = finger1_pos2_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger2_pos3_um = finger2_pos2_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let inputs = vec![
                touchpad_event(vec![finger1_pos0_um], hashset! {}),
                touchpad_event(vec![finger1_pos1_um], hashset! {}),
                touchpad_event(vec![finger1_pos2_um, finger2_pos2_um], hashset! {}),
                touchpad_event(vec![finger1_pos3_um, finger2_pos3_um], hashset! {}),
                touchpad_event(vec![], hashset! {}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 5);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, location: location_a),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
              assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location_a.millimeters.y, 0.0);
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

        #[fuchsia::test(allow_stalls = false)]
        async fn motion_then_one_finger_drag() {
            let finger1_pos0_um = Position { x: 2_000.0, y: 3_000.0 };
            let finger1_pos1_um = finger1_pos0_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let finger1_pos2_um = finger1_pos1_um.clone();
            let finger1_pos3_um = finger1_pos2_um
                + Position {
                    x: 0.0,
                    y: 1_000.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
                };
            let inputs = vec![
                touchpad_event(vec![finger1_pos0_um], hashset! {}),
                touchpad_event(vec![finger1_pos1_um], hashset! {}),
                touchpad_event(vec![finger1_pos2_um], hashset! {1}),
                touchpad_event(vec![finger1_pos3_um], hashset! {1}),
            ];
            let got = utils::run_gesture_arena_test(inputs).await;

            assert_eq!(got.len(), 4);
            assert_eq!(got[0].as_slice(), []);
            assert_matches!(got[1].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, location: location_a),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Move);
              assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location_a.millimeters.y, 0.0);
            });
            assert_eq!(got[2].as_slice(), []);
            assert_matches!(got[3].as_slice(), [
              utils::expect_mouse_event!(phase: phase_a, pressed_buttons: pressed_button_a, affected_buttons: affected_button_a, location: location_a),
            ] => {
              assert_eq!(phase_a, &mouse_binding::MousePhase::Down);
              assert_eq!(pressed_button_a, &hashset! {1});
              assert_eq!(affected_button_a, &hashset! {1});
              assert_near!(location_a.millimeters.x, 0.0, utils::EPSILON);
              assert_gt!(location_a.millimeters.y, 0.0);
            });
        }
    }
}
