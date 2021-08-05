// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements applying keymaps to hardware keyboard key events.
//!
//! See [Handler] for details.

use crate::input_device;
use crate::input_handler::InputHandler;
use crate::keyboard;
use async_trait::async_trait;
use fuchsia_syslog::fx_log_debug;
use keymaps;
use std::cell::RefCell;
use std::rc::Rc;

/// `Handler` applies a keymap to a keyboard event, resolving each key press
/// to a sequence of Unicode code points.  This allows basic keymap application,
/// but does not lend itself to generalized text editing.
///
/// Create a new one with [Handler::new].
#[derive(Debug, Default)]
pub struct Handler {
    /// Tracks the state of the modifier keys.
    modifier_state: RefCell<keymaps::ModifierState>,
}

/// This trait implementation allows the [Handler] to be hooked up into the input
/// pipeline.
#[async_trait(?Send)]
impl InputHandler for Handler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            // Decorate a keyboard event with key meaning.
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(event),
                device_descriptor,
                event_time,
            } => self.process_keyboard_event(event, device_descriptor, event_time),
            // Pass other events unchanged.
            _ => vec![input_event],
        }
    }
}

impl Handler {
    /// Creates a new instance of the keymap handler.
    pub fn new() -> Rc<Self> {
        Rc::new(Default::default())
    }

    /// Attaches a key meaning to each passing keyboard event.
    fn process_keyboard_event(
        self: &Rc<Self>,
        event: keyboard::KeyboardEvent,
        device_descriptor: input_device::InputDeviceDescriptor,
        event_time: input_device::EventTime,
    ) -> Vec<input_device::InputEvent> {
        let (key, event_type) = (&event.key, &event.event_type);
        fx_log_debug!(
            concat!(
                "Keymap::process_keyboard_event: key:{:?}, ",
                "modifier_state:{:?}, event_type: {:?}"
            ),
            key,
            self.modifier_state.borrow(),
            event_type
        );

        self.modifier_state.borrow_mut().update(*event_type, *key);
        let mut new_event = event.clone();
        new_event.key_meaning =
            keymaps::select_keymap(&event.keymap).apply(*key, &*self.modifier_state.borrow());
        vec![input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(new_event),
            device_descriptor,
            event_time,
        }]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::{consumer_controls, testing_utilities};
    use fuchsia_async as fasync;

    // A mod-specific version of `testing_utilities::create_keyboard_event`.
    fn create_keyboard_event(
        key: fidl_fuchsia_input::Key,
        event_type: fidl_fuchsia_ui_input3::KeyEventType,
        keymap: Option<String>,
    ) -> input_device::InputEvent {
        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_fuchsia_input::Key::A, fidl_fuchsia_input::Key::B],
            });
        let (_, event_time_u64) = testing_utilities::event_times();
        testing_utilities::create_keyboard_event(
            key,
            event_type,
            /* modifiers= */ None,
            event_time_u64,
            &device_descriptor,
            keymap,
        )
    }

    fn get_key_meaning(
        event: &input_device::InputEvent,
    ) -> Option<fidl_fuchsia_ui_input3::KeyMeaning> {
        match event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(event),
                ..
            } => event.key_meaning,
            _ => None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_keymap_application() {
        // Not using test_case crate because it does not compose very well with
        // async test execution.
        #[derive(Debug)]
        struct TestCase {
            events: Vec<input_device::InputEvent>,
            expected: Vec<Option<fidl_fuchsia_ui_input3::KeyMeaning>>,
        }
        let tests: Vec<TestCase> = vec![
            TestCase {
                events: vec![create_keyboard_event(
                    fidl_fuchsia_input::Key::A,
                    fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                    Some("US_QWERTY".into()),
                )],
                expected: vec![
                    Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(97)), // a
                ],
            },
            TestCase {
                // A non-keyboard event.
                events: vec![testing_utilities::create_consumer_controls_event(
                    vec![],
                    0,
                    &input_device::InputDeviceDescriptor::ConsumerControls(
                        consumer_controls::ConsumerControlsDeviceDescriptor { buttons: vec![] },
                    ),
                )],
                expected: vec![None],
            },
            TestCase {
                events: vec![
                    create_keyboard_event(
                        fidl_fuchsia_input::Key::LeftShift,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                    create_keyboard_event(
                        fidl_fuchsia_input::Key::A,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                ],
                expected: vec![
                    Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(0)), // Shift...
                    Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(65)), // A
                ],
            },
            TestCase {
                events: vec![
                    create_keyboard_event(
                        fidl_fuchsia_input::Key::Tab,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                    create_keyboard_event(
                        fidl_fuchsia_input::Key::A,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                ],
                expected: vec![
                    Some(fidl_fuchsia_ui_input3::KeyMeaning::NonPrintableKey(
                        fidl_fuchsia_ui_input3::NonPrintableKey::Tab,
                    )),
                    Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(97)), // a
                ],
            },
        ];
        for test in &tests {
            let mut actual: Vec<Option<fidl_fuchsia_ui_input3::KeyMeaning>> = vec![];
            let handler = Handler::new();
            for event in &test.events {
                let mut result = handler
                    .clone()
                    .handle_input_event(event.clone())
                    .await
                    .iter()
                    .map(get_key_meaning)
                    .collect();
                actual.append(&mut result);
            }
            assert_eq!(
                test.expected, actual,
                "\n\texpected: {:?}\n\tactual: {:?}\n\ttest: {:?}",
                &test.expected, &actual, &test
            );
        }
    }
}
