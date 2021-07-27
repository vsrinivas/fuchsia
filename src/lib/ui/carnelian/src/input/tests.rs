// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    drawing::DisplayRotation,
    geometry::{IntPoint, IntSize},
};
use euclid::{point2, size2};
use fidl_fuchsia_input_report as hid_input_report;

mod mouse_tests {
    use super::*;
    use crate::input::{mouse, Button, ButtonSet, Event, EventType};

    pub fn create_test_mouse_event(button: u8) -> Event {
        let mouse_event = mouse::Event {
            buttons: ButtonSet::default(),
            phase: mouse::Phase::Down(Button(button)),
            location: IntPoint::zero(),
        };
        Event {
            event_time: 0,
            device_id: device_id_tests::create_test_device_id(),
            event_type: EventType::Mouse(mouse_event),
        }
    }
}

mod touch_tests {
    use super::*;
    use crate::input::touch;

    pub fn create_test_contact() -> touch::Contact {
        touch::Contact {
            contact_id: touch::ContactId(100),
            phase: touch::Phase::Down(IntPoint::zero(), IntSize::zero()),
        }
    }
}

mod pointer_tests {
    use super::*;
    use crate::input::{pointer, EventType};

    #[test]
    fn test_pointer_from_mouse() {
        for button in 1..3 {
            let event = mouse_tests::create_test_mouse_event(button);
            match event.event_type {
                EventType::Mouse(mouse_event) => {
                    let pointer_event =
                        pointer::Event::new_from_mouse_event(&event.device_id, &mouse_event);
                    assert_eq!(pointer_event.is_some(), button == 1);
                }
                _ => panic!("I asked for a mouse event"),
            }
        }
    }

    #[test]
    fn test_pointer_from_contact() {
        let contact = touch_tests::create_test_contact();
        let pointer_event = pointer::Event::new_from_contact(&contact);
        match pointer_event.phase {
            pointer::Phase::Down(location) => {
                assert_eq!(location, IntPoint::zero());
            }
            _ => panic!("This should have been a down pointer event"),
        }
    }
}

mod device_id_tests {
    use crate::input::DeviceId;

    pub(crate) fn create_test_device_id() -> DeviceId {
        DeviceId("test-device-id-1".to_string())
    }
}

mod input_report_tests {
    use super::*;
    use crate::{
        app::strategies::framebuffer::AutoRepeatTimer,
        input::{
            consumer_control, keyboard,
            report::InputReportHandler,
            report::TouchScale,
            touch, DeviceId, {mouse, EventType},
        },
    };
    use itertools::assert_equal;

    struct TestAutoRepeatContext;

    impl AutoRepeatTimer for TestAutoRepeatContext {
        fn schedule_autorepeat_timer(&mut self, _device_id: &DeviceId) {}
        fn continue_autorepeat_timer(&mut self, _device_id: &DeviceId) {}
    }

    fn make_input_handler() -> InputReportHandler<'static> {
        let test_size = size2(1024, 768);
        let touch_scale = TouchScale {
            target_size: test_size,
            x: fidl_fuchsia_input_report::Range { min: 0, max: 4095 },
            x_span: 4095.0,
            y: fidl_fuchsia_input_report::Range { min: 0, max: 4095 },
            y_span: 4095.0,
        };
        InputReportHandler::new_with_scale(
            device_id_tests::create_test_device_id(),
            test_size,
            DisplayRotation::Deg0,
            Some(touch_scale),
            &keymaps::US_QWERTY,
        )
    }

    #[test]
    fn test_typed_string() {
        let reports = test_data::hello_world_keyboard_reports();

        let mut context = TestAutoRepeatContext;

        let mut input_handler = make_input_handler();

        let device_id = DeviceId("keyboard-1".to_string());
        let chars_from_events = reports
            .iter()
            .map(|input_report| {
                input_handler.handle_input_report(&device_id, input_report, &mut context)
            })
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => keyboard_event
                        .code_point
                        .and_then(|code_point| Some(code_point as u8 as char)),
                    _ => None,
                },
                _ => None,
            });

        assert_equal("Hello World".chars(), chars_from_events);
    }

    #[test]
    fn test_touch_drag() {
        let reports = test_data::touch_drag_input_reports();

        let device_id = DeviceId("touch-1".to_string());

        let mut input_handler = make_input_handler();

        let mut context = TestAutoRepeatContext;

        let events = reports
            .iter()
            .map(|input_report| {
                input_handler.handle_input_report(&device_id, input_report, &mut context)
            })
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        for event in events {
            match event.event_type {
                EventType::Touch(touch_event) => {
                    let contact = touch_event.contacts.iter().nth(0).expect("first contact");
                    match contact.phase {
                        touch::Phase::Down(location, _) => {
                            start_point = location;
                        }
                        touch::Phase::Moved(location, _) => {
                            end_point = location;
                            move_count += 1;
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }

        assert_eq!(start_point, point2(302, 491));
        assert_eq!(end_point, point2(637, 21));
        assert_eq!(move_count, 15);
    }

    #[test]
    fn test_mouse_drag() {
        let reports = test_data::mouse_drag_input_reports();

        let device_id = DeviceId("touch-1".to_string());

        let mut context = TestAutoRepeatContext;

        let mut input_handler = make_input_handler();
        let events = reports
            .iter()
            .map(|input_report| {
                input_handler.handle_input_report(&device_id, input_report, &mut context)
            })
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        let mut down_button = None;
        for event in events {
            match event.event_type {
                EventType::Mouse(mouse_event) => match mouse_event.phase {
                    mouse::Phase::Down(button) => {
                        assert!(down_button.is_none());
                        assert!(button.is_primary());
                        start_point = mouse_event.location;
                        down_button = Some(button);
                    }
                    mouse::Phase::Moved => {
                        end_point = mouse_event.location;
                        move_count += 1;
                    }
                    mouse::Phase::Up(button) => {
                        assert!(button.is_primary());
                    }
                },
                _ => (),
            }
        }

        assert!(down_button.expect("down_button").is_primary());
        assert_eq!(start_point, point2(129, 44));
        assert_eq!(end_point, point2(616, 213));
        assert_eq!(move_count, 181);
    }

    #[test]
    fn test_consumer_control() {
        use hid_input_report::ConsumerControlButton::{VolumeDown, VolumeUp};
        let reports = test_data::consumer_control_input_reports();

        let device_id = DeviceId("cc-1".to_string());

        let mut context = TestAutoRepeatContext;

        let mut input_handler = make_input_handler();
        let events: Vec<(consumer_control::Phase, hid_input_report::ConsumerControlButton)> =
            reports
                .iter()
                .map(|input_report| {
                    input_handler.handle_input_report(&device_id, input_report, &mut context)
                })
                .flatten()
                .filter_map(|event| match event.event_type {
                    EventType::ConsumerControl(consumer_control_event) => {
                        Some((consumer_control_event.phase, consumer_control_event.button))
                    }
                    _ => None,
                })
                .collect();

        let expected = [
            (consumer_control::Phase::Down, VolumeUp),
            (consumer_control::Phase::Up, VolumeUp),
            (consumer_control::Phase::Down, VolumeDown),
            (consumer_control::Phase::Up, VolumeDown),
        ];
        assert_eq!(events, expected);
    }
}

mod scenic_input_tests {
    use super::*;
    use crate::input::{
        keyboard,
        scenic::ScenicInputHandler,
        touch, {mouse, EventType},
    };
    use itertools::assert_equal;

    #[test]
    fn test_typed_string() {
        let scenic_events = test_data::hello_world_scenic_input_events();

        let mut scenic_input_handler = ScenicInputHandler::new();
        let chars_from_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_key_event(event))
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => keyboard_event
                        .code_point
                        .and_then(|code_point| Some(code_point as u8 as char)),
                    _ => None,
                },
                _ => None,
            });

        assert_equal("Hello World".chars(), chars_from_events);
    }

    #[test]
    fn test_control_r() {
        let scenic_events = test_data::control_r_scenic_events();

        // make sure there's one and only one keydown even of the r
        // key with the control modifier set.
        let mut scenic_input_handler = ScenicInputHandler::new();
        let expected_event_count: usize = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_key_event(event))
            .flatten()
            .filter_map(|event| match event.event_type {
                EventType::Keyboard(keyboard_event) => match keyboard_event.phase {
                    keyboard::Phase::Pressed => {
                        if keyboard_event.hid_usage == keymaps::usages::Usages::HidUsageKeyR as u32
                            && keyboard_event.modifiers.control
                        {
                            Some(())
                        } else {
                            None
                        }
                    }
                    _ => None,
                },
                _ => None,
            })
            .count();

        assert_eq!(expected_event_count, 1);
    }

    #[test]
    fn test_touch_drag() {
        let scenic_events = test_data::touch_drag_scenic_events();
        let mut scenic_input_handler = ScenicInputHandler::new();
        let metrics = size2(1.0, 1.0);
        let input_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_input_event(&metrics, event))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        for event in input_events {
            match event.event_type {
                EventType::Touch(touch_event) => {
                    let contact = touch_event.contacts.iter().nth(0).expect("first contact");
                    match contact.phase {
                        touch::Phase::Down(location, _) => {
                            start_point = location;
                        }
                        touch::Phase::Moved(location, _) => {
                            end_point = location;
                            move_count += 1;
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }

        assert_eq!(start_point, point2(193, 107));
        assert_eq!(end_point, point2(269, 157));
        assert_eq!(move_count, 8);
    }

    #[test]
    fn test_mouse_drag() {
        let scenic_events = test_data::mouse_drag_scenic_events();
        let mut scenic_input_handler = ScenicInputHandler::new();
        let metrics = size2(1.0, 1.0);
        let input_events = scenic_events
            .iter()
            .map(|event| scenic_input_handler.handle_scenic_input_event(&metrics, event))
            .flatten();

        let mut start_point = IntPoint::zero();
        let mut end_point = IntPoint::zero();
        let mut move_count = 0;
        let mut down_button = None;
        for event in input_events {
            match event.event_type {
                EventType::Mouse(mouse_event) => match mouse_event.phase {
                    mouse::Phase::Down(button) => {
                        assert!(down_button.is_none());
                        assert!(button.is_primary());
                        start_point = mouse_event.location;
                        down_button = Some(button);
                    }
                    mouse::Phase::Moved => {
                        end_point = mouse_event.location;
                        move_count += 1;
                    }
                    mouse::Phase::Up(button) => {
                        assert!(button.is_primary());
                    }
                },
                _ => (),
            }
        }

        assert!(down_button.expect("down_button").is_primary());
        assert_eq!(start_point, point2(67, 62));
        assert_eq!(end_point, point2(128, 136));
        assert_eq!(move_count, 36);
    }
}

mod test_data {
    pub fn consumer_control_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{
            ConsumerControlButton::{VolumeDown, VolumeUp},
            ConsumerControlInputReport, InputReport,
        };
        vec![
            InputReport {
                event_time: Some(66268999833),
                mouse: None,
                trace_id: Some(2),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([VolumeUp].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(66434561666),
                mouse: None,
                trace_id: Some(3),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(358153537000),
                mouse: None,
                trace_id: Some(4),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([VolumeDown].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(358376260958),
                mouse: None,
                trace_id: Some(5),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: Some(ConsumerControlInputReport {
                    pressed_buttons: Some([].to_vec()),
                    ..ConsumerControlInputReport::EMPTY
                }),
                ..InputReport::EMPTY
            },
        ]
    }
    pub fn hello_world_keyboard_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use {
            fidl_fuchsia_input::Key::*,
            fidl_fuchsia_input_report::{InputReport, KeyboardInputReport},
        };
        vec![
            InputReport {
                event_time: Some(85446402710730),
                mouse: None,
                trace_id: Some(189),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![LeftShift]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446650713601),
                mouse: None,
                trace_id: Some(191),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![LeftShift, H]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446738712880),
                mouse: None,
                trace_id: Some(193),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![LeftShift]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446794702907),
                mouse: None,
                trace_id: Some(195),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85446970709193),
                mouse: None,
                trace_id: Some(197),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![E]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447090710657),
                mouse: None,
                trace_id: Some(199),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447330708990),
                mouse: None,
                trace_id: Some(201),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![L]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447394712460),
                mouse: None,
                trace_id: Some(203),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447508813465),
                mouse: None,
                trace_id: Some(205),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![L]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447618712982),
                mouse: None,
                trace_id: Some(207),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447810705156),
                mouse: None,
                trace_id: Some(209),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![O]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85447898703263),
                mouse: None,
                trace_id: Some(211),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450082706011),
                mouse: None,
                trace_id: Some(213),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![Space]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450156060503),
                mouse: None,
                trace_id: Some(215),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450418710803),
                mouse: None,
                trace_id: Some(217),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![LeftShift]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450594712232),
                mouse: None,
                trace_id: Some(219),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![LeftShift, W]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450746707982),
                mouse: None,
                trace_id: Some(221),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![W]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450794706822),
                mouse: None,
                trace_id: Some(223),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85450962706591),
                mouse: None,
                trace_id: Some(225),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![O]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451050703903),
                mouse: None,
                trace_id: Some(227),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451282710803),
                mouse: None,
                trace_id: Some(229),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![R]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451411293149),
                mouse: None,
                trace_id: Some(231),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451842714565),
                mouse: None,
                trace_id: Some(233),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![L]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85451962704075),
                mouse: None,
                trace_id: Some(235),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85452906710709),
                mouse: None,
                trace_id: Some(237),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![D]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85453034711602),
                mouse: None,
                trace_id: Some(239),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85454778708461),
                mouse: None,
                trace_id: Some(241),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![Enter]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(85454858706151),
                mouse: None,
                trace_id: Some(243),
                sensor: None,
                touch: None,
                keyboard: Some(KeyboardInputReport {
                    pressed_keys3: Some(vec![]),
                    ..KeyboardInputReport::EMPTY
                }),
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn touch_drag_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{ContactInputReport, InputReport, TouchInputReport};

        vec![
            InputReport {
                event_time: Some(2129689875195),
                mouse: None,
                trace_id: Some(294),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1211),
                        position_y: Some(2621),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129715875833),
                mouse: None,
                trace_id: Some(295),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1211),
                        position_y: Some(2621),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129741874822),
                mouse: None,
                trace_id: Some(296),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1223),
                        position_y: Some(2607),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129767876545),
                mouse: None,
                trace_id: Some(297),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1267),
                        position_y: Some(2539),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129793872236),
                mouse: None,
                trace_id: Some(298),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1391),
                        position_y: Some(2300),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129818875839),
                mouse: None,
                trace_id: Some(299),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1523),
                        position_y: Some(2061),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129844873276),
                mouse: None,
                trace_id: Some(300),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1675),
                        position_y: Some(1781),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129870884557),
                mouse: None,
                trace_id: Some(301),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1743),
                        position_y: Some(1652),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129896870474),
                mouse: None,
                trace_id: Some(302),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(1875),
                        position_y: Some(1399),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129922876931),
                mouse: None,
                trace_id: Some(303),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2015),
                        position_y: Some(1174),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129948875990),
                mouse: None,
                trace_id: Some(304),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2143),
                        position_y: Some(935),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129973877732),
                mouse: None,
                trace_id: Some(305),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2275),
                        position_y: Some(682),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2129998870634),
                mouse: None,
                trace_id: Some(306),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2331),
                        position_y: Some(566),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130023872212),
                mouse: None,
                trace_id: Some(307),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2439),
                        position_y: Some(314),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130048871365),
                mouse: None,
                trace_id: Some(308),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2551),
                        position_y: Some(116),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130071873308),
                mouse: None,
                trace_id: Some(309),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![ContactInputReport {
                        contact_id: Some(0),
                        position_x: Some(2643),
                        position_y: Some(54),
                        pressure: None,
                        contact_width: None,
                        contact_height: None,
                        ..ContactInputReport::EMPTY
                    }]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(2130110871653),
                mouse: None,
                trace_id: Some(310),
                sensor: None,
                touch: Some(TouchInputReport {
                    contacts: Some(vec![]),
                    pressed_buttons: Some(vec![]),
                    ..TouchInputReport::EMPTY
                }),
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn mouse_drag_input_reports() -> Vec<fidl_fuchsia_input_report::InputReport> {
        use fidl_fuchsia_input_report::{InputReport, MouseInputReport};
        vec![
            InputReport {
                event_time: Some(101114216676),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(1),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101122479286),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(3),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101130223338),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(4),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101139198674),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(5),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101154621806),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(6),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101162221969),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(7),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101170222632),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(8),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101178218319),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(9),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101195538881),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(10),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101202218423),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(11),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101210236557),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(12),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101218244736),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(13),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101226633284),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(14),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101235789939),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(15),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101242227234),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(16),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101250552651),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(17),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101258523666),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(18),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101266879375),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(19),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101279470078),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(20),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101282237222),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(21),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101290229686),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(22),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101298227434),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(23),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101306236833),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(24),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101314225440),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(25),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101322221224),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(26),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101330220567),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(27),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101338229995),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(28),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101346226157),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(29),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101354223947),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(30),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101362223006),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(31),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101370218719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(32),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101378220583),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(33),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101386213038),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(34),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101394217453),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(35),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101402219904),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(36),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101410221107),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(37),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101418222560),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(38),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101434218357),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(39),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101442218953),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(40),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101450217289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(41),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101458214227),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(42),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101466225708),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(43),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101474215177),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(44),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101482221526),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(45),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101490219532),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(46),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101498222281),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(47),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101506214971),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(48),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101514219490),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(49),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101522217217),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(50),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101530217381),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(51),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101538212289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(52),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(101554216328),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(53),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103242211673),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(54),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103330219916),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(55),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103338210706),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(56),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103346224236),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(57),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103354212884),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(58),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103362215662),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(59),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103370214381),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(11),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(60),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103378214091),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(11),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(61),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103386209918),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(62),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103394217896),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(63),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103402213295),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(13),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(64),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103410215085),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(13),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(65),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103418219723),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(66),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103426211988),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(67),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103434211330),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(12),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(68),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103442219232),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(69),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103450211768),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(70),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103458211814),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(71),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103466216581),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(72),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103474215898),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(73),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103482215147),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(74),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103490216112),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(75),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103498215973),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(76),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103506213277),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(77),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103514218088),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(78),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103522217065),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(79),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103530210262),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(80),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103578218194),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(81),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103586213104),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(82),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103594216835),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(83),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103602487409),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(84),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103610212817),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(85),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103618214151),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(86),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103626214410),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(87),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103634212067),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(88),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103642212545),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(89),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103650212962),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(90),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103658212822),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(91),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103666210198),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(92),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103674217073),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(93),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103682212701),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(94),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103690210927),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(95),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103698216512),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(96),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103706213176),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(97),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103714212778),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(98),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103722213889),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(99),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103730210581),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(100),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103738214789),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(101),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103746216817),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(102),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103754216490),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(103),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103762214303),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(104),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103770212491),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(105),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103778217308),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(106),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103786212710),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(107),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103794217315),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(108),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103802211383),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(109),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103810216190),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(110),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103834222367),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(111),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103954219855),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(112),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103962217418),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(113),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103970214839),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(114),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103978214040),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(115),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103986213448),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(116),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(103994211708),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(117),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104002212585),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(118),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104010210902),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(119),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104018211093),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(120),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104026216997),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(121),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104034211539),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(122),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104042222246),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(123),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104050216094),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(10),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(124),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104058215037),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(125),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104066221081),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(126),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104074216757),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(127),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104082216368),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(128),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104090217281),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(9),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(129),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104098212452),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(130),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104106216109),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(131),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104114266027),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(132),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104122212879),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(133),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104130216506),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(134),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104138217516),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(135),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104146210328),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(136),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104154216601),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(137),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104162216056),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(138),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104170215445),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(139),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104178211471),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(140),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104186213147),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(141),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104194212256),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(142),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104202213946),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(143),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104210212892),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(144),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104218214234),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(145),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104226215241),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(146),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104234215524),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(147),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104282215440),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(148),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104290210105),
                mouse: Some(MouseInputReport {
                    movement_x: Some(1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(149),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104298226745),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(150),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104306215865),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(151),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104314217045),
                mouse: Some(MouseInputReport {
                    movement_x: Some(4),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(152),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104322334192),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(153),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104330216276),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(154),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104338214799),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(155),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104346215946),
                mouse: Some(MouseInputReport {
                    movement_x: Some(10),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(156),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104354214863),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(157),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104362215296),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(158),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104370214666),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(159),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104378215593),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(160),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104386215460),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(161),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104394217072),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(162),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104402213289),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(163),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104410215719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(164),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104418216898),
                mouse: Some(MouseInputReport {
                    movement_x: Some(9),
                    movement_y: Some(-8),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(165),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104426215292),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(166),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104434215345),
                mouse: Some(MouseInputReport {
                    movement_x: Some(8),
                    movement_y: Some(-7),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(167),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104442217176),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-6),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(168),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104450214083),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-5),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(169),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104458213546),
                mouse: Some(MouseInputReport {
                    movement_x: Some(7),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(170),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104466216290),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(171),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104474215684),
                mouse: Some(MouseInputReport {
                    movement_x: Some(5),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(172),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104482216348),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(173),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104490211575),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-4),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(174),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104498215305),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(175),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104506212563),
                mouse: Some(MouseInputReport {
                    movement_x: Some(6),
                    movement_y: Some(-3),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(176),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104514213178),
                mouse: Some(MouseInputReport {
                    movement_x: Some(3),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(177),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104522213190),
                mouse: Some(MouseInputReport {
                    movement_x: Some(2),
                    movement_y: Some(-2),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(178),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104530216023),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(-1),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![1]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(179),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(104866221719),
                mouse: Some(MouseInputReport {
                    movement_x: Some(0),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(180),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105266217002),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(181),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105274246358),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-1),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(182),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105282216030),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(183),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
            InputReport {
                event_time: Some(105290214427),
                mouse: Some(MouseInputReport {
                    movement_x: Some(-2),
                    movement_y: Some(0),
                    scroll_v: Some(0),
                    scroll_h: None,
                    pressed_buttons: Some(vec![]),
                    position_x: None,
                    position_y: None,
                    ..MouseInputReport::EMPTY
                }),
                trace_id: Some(184),
                sensor: None,
                touch: None,
                keyboard: None,
                consumer_control: None,
                ..InputReport::EMPTY
            },
        ]
    }

    pub fn hello_world_scenic_input_events() -> Vec<fidl_fuchsia_ui_input3::KeyEvent> {
        use fidl_fuchsia_input::Key::*;
        use fidl_fuchsia_ui_input3::{
            KeyEvent,
            KeyEventType::{Pressed, Released},
        };
        vec![
            KeyEvent {
                timestamp: Some(3264387612285),
                type_: Some(Pressed),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265130500125),
                type_: Some(Pressed),
                key: Some(H),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265266507731),
                type_: Some(Released),
                key: Some(H),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3265370503901),
                type_: Some(Released),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3266834499940),
                type_: Some(Pressed),
                key: Some(E),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3266962508842),
                type_: Some(Released),
                key: Some(E),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267154500453),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267219444859),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267346499392),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267458502427),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267690502669),
                type_: Some(Pressed),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3267858501367),
                type_: Some(Released),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275178512511),
                type_: Some(Pressed),
                key: Some(Space),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275274501635),
                type_: Some(Pressed),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275298499697),
                type_: Some(Released),
                key: Some(Space),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275474504423),
                type_: Some(Pressed),
                key: Some(W),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275586502431),
                type_: Some(Released),
                key: Some(LeftShift),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275634500151),
                type_: Some(Released),
                key: Some(W),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275714502408),
                type_: Some(Pressed),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275834561768),
                type_: Some(Released),
                key: Some(O),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3275858499854),
                type_: Some(Pressed),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276018509754),
                type_: Some(Released),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276114540325),
                type_: Some(Pressed),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276282504845),
                type_: Some(Released),
                key: Some(L),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276578503737),
                type_: Some(Pressed),
                key: Some(D),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(3276706501366),
                type_: Some(Released),
                key: Some(D),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
        ]
    }

    pub fn control_r_scenic_events() -> Vec<fidl_fuchsia_ui_input3::KeyEvent> {
        use fidl_fuchsia_input::Key::*;
        use fidl_fuchsia_ui_input3::{
            KeyEvent,
            KeyEventType::{Pressed, Released},
        };
        vec![
            KeyEvent {
                timestamp: Some(4453530520711),
                type_: Some(Pressed),
                key: Some(LeftCtrl),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454138519645),
                type_: Some(Pressed),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454730534107),
                type_: Some(Released),
                key: Some(R),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
            KeyEvent {
                timestamp: Some(4454738498944),
                type_: Some(Released),
                key: Some(LeftCtrl),
                modifiers: None,
                ..KeyEvent::EMPTY
            },
        ]
    }

    pub fn touch_drag_scenic_events() -> Vec<fidl_fuchsia_ui_input::InputEvent> {
        use fidl_fuchsia_ui_input::{
            FocusEvent,
            InputEvent::{Focus, Pointer},
            PointerEvent,
            PointerEventPhase::{Add, Down, Move, Remove, Up},
            PointerEventType::Touch,
        };
        vec![
            Pointer(PointerEvent {
                event_time: 3724420542810,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Add,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Focus(FocusEvent { event_time: 3724420796330, focused: true }),
            Pointer(PointerEvent {
                event_time: 3724420542810,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Down,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724446545561,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 193.06534,
                y: 107.604416,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724472567434,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 194.81122,
                y: 108.20114,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724498537301,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 200.63081,
                y: 112.29306,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724524543861,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 219.8355,
                y: 125.67702,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724550551818,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 239.04019,
                y: 137.86748,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724575547592,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 254.17116,
                y: 149.5465,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724601536497,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 260.57272,
                y: 153.04164,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724627538012,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Move,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724669535009,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Up,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
            Pointer(PointerEvent {
                event_time: 3724669535009,
                device_id: 0,
                pointer_id: 0,
                type_: Touch,
                phase: Remove,
                x: 269.8841,
                y: 157.13356,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 0,
            }),
        ]
    }

    pub fn mouse_drag_scenic_events() -> Vec<fidl_fuchsia_ui_input::InputEvent> {
        use fidl_fuchsia_ui_input::{
            FocusEvent,
            InputEvent::{Focus, Pointer},
            PointerEvent,
            PointerEventPhase::{Down, Move, Up},
            PointerEventType::Mouse,
        };
        vec![
            Focus(FocusEvent { event_time: 112397259832, focused: true }),
            Pointer(PointerEvent {
                event_time: 112396994735,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Down,
                x: 67.49091,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112508984750,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 68.07273,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112516989437,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 69.818184,
                y: 62.25455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112524990631,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 72.72728,
                y: 64.58183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112532991020,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 74.47273,
                y: 66.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112541018566,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 76.8,
                y: 69.81819,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112548984575,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 79.12728,
                y: 72.72728,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112556985463,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 82.03637,
                y: 76.80002,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112564990769,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 84.94546,
                y: 79.70911,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112572989372,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 87.85455,
                y: 83.78183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112580993049,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 90.18182,
                y: 86.69092,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112588991675,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 92.509094,
                y: 90.18182,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112596989208,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 95.41819,
                y: 93.672745,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112604989384,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 97.74546,
                y: 96.000015,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112612996959,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 100.07273,
                y: 98.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112620989830,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 101.818184,
                y: 100.65456,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112628994663,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 103.563644,
                y: 102.40001,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112636989146,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 104.72728,
                y: 104.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112644983252,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 105.890915,
                y: 105.3091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112652985951,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 106.47273,
                y: 105.890915,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112660993261,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 106.47273,
                y: 107.05455,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112669007012,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 107.63637,
                y: 108.8,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112676986758,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 108.8,
                y: 109.96365,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112684990368,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 109.38182,
                y: 111.12729,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112692990815,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 111.12727,
                y: 114.03638,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112700991008,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 112.29092,
                y: 116.36365,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112708991160,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 114.61819,
                y: 119.272736,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112716985316,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 116.36364,
                y: 122.76364,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112724995834,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 118.69091,
                y: 125.09091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112732988629,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 120.43637,
                y: 128.00002,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112740984820,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 122.18182,
                y: 129.74547,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112748990041,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 123.34546,
                y: 130.9091,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112756988188,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 124.509094,
                y: 132.07274,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112764986974,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 125.67273,
                y: 133.23637,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112772992141,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 126.25455,
                y: 133.81819,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112780992743,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 126.836365,
                y: 134.98183,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112844991890,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Move,
                x: 128.0,
                y: 136.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
            Pointer(PointerEvent {
                event_time: 112924985017,
                device_id: 0,
                pointer_id: 0,
                type_: Mouse,
                phase: Up,
                x: 128.0,
                y: 136.14546,
                radius_major: 0.0,
                radius_minor: 0.0,
                buttons: 1,
            }),
        ]
    }
}
