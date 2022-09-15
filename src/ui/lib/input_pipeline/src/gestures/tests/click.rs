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
        test_util::{assert_gt, assert_near},
    };

    const EPSILON: f32 = 1.0 / 100_000.0;

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
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button1,
                affected_buttons: affected_button1,
                ..
              },
            ),
          ..
          },
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button2,
                affected_buttons: affected_button2,
                ..
              },
            ),
          ..
          }
        ] => {
          assert_eq!(pressed_button1, &hashset! {1});
          assert_eq!(affected_button1, &hashset! {1});
          assert_eq!(pressed_button2, &hashset! {});
          assert_eq!(affected_button2, &hashset! {1});
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
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button1,
                affected_buttons: affected_button1,
                ..
              },
            ),
          ..
          },
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button2,
                affected_buttons: affected_button2,
                ..
              },
            ),
          ..
          }
        ] => {
          assert_eq!(pressed_button1, &hashset! {1});
          assert_eq!(affected_button1, &hashset! {1});
          assert_eq!(pressed_button2, &hashset! {});
          assert_eq!(affected_button2, &hashset! {1});
        });
    }

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
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button1,
                affected_buttons: affected_button1,
                ..
              },
            ),
          ..
          },
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button2,
                affected_buttons: affected_button2,
                ..
              },
            ),
          ..
          }
        ] => {
          assert_eq!(pressed_button1, &hashset! {1});
          assert_eq!(affected_button1, &hashset! {1});
          assert_eq!(pressed_button2, &hashset! {});
          assert_eq!(affected_button2, &hashset! {1});
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
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button1,
                affected_buttons: affected_button1,
                ..
              },
            ),
          ..
          },
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button2,
                affected_buttons: affected_button2,
                ..
              },
            ),
          ..
          }
        ] => {
          assert_eq!(pressed_button1, &hashset! {1});
          assert_eq!(affected_button1, &hashset! {1});
          assert_eq!(pressed_button2, &hashset! {});
          assert_eq!(affected_button2, &hashset! {1});
        });
        assert_eq!(got[3].as_slice(), []);
        assert_eq!(got[4].as_slice(), []);
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn click_then_motion() {
        let pos1 = Position { x: 2_000.0, y: 3_000.0 };
        let pos2 = Position {
            x: 2_000.0,
            y: 3_100.0 + args::SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM * 1_000.0,
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
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button1,
                affected_buttons: affected_button1,
                ..
              },
            ),
          ..
          },
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                pressed_buttons: pressed_button2,
                affected_buttons: affected_button2,
                ..
              },
            ),
          ..
          }
        ] => {
          assert_eq!(pressed_button1, &hashset! {1});
          assert_eq!(affected_button1, &hashset! {1});
          assert_eq!(pressed_button2, &hashset! {});
          assert_eq!(affected_button2, &hashset! {1});
        });
        assert_eq!(got[2].as_slice(), []);
        assert_matches!(got[3].as_slice(), [
          input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(
              mouse_binding::MouseEvent {
                location: mouse_binding::MouseLocation::Relative(location1),
                ..
              },
            ),
          ..
          },
        ] => {
          assert_near!(location1.millimeters.x, 0.0, EPSILON);
          assert_gt!(location1.millimeters.y, 0.0);
        });
    }
}
