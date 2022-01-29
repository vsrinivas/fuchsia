// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements applying keymaps to hardware keyboard key events.
//!
//! See [KeymapHandler] for details.

use crate::input_device;
use crate::input_handler::UnhandledInputHandler;
use crate::keyboard_binding;
use async_trait::async_trait;
use fuchsia_syslog::fx_log_debug;
use fuchsia_zircon as zx;
use keymaps;
use std::cell::RefCell;
use std::rc::Rc;

/// `KeymapHandler` applies a keymap to a keyboard event, resolving each key press
/// to a sequence of Unicode code points.  This allows basic keymap application,
/// but does not lend itself to generalized text editing.
///
/// Create a new one with [KeymapHandler::new].
#[derive(Debug, Default)]
pub struct KeymapHandler {
    /// Tracks the state of the modifier keys.
    modifier_state: RefCell<keymaps::ModifierState>,

    /// Tracks the lock state (NumLock, CapsLock).
    lock_state: RefCell<keymaps::LockStateKeys>,
}

/// This trait implementation allows the [KeymapHandler] to be hooked up into the input
/// pipeline.
#[async_trait(?Send)]
impl UnhandledInputHandler for KeymapHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            // Decorate a keyboard event with key meaning.
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(event),
                device_descriptor,
                event_time,
            } => vec![input_device::InputEvent::from(self.process_keyboard_event(
                event,
                device_descriptor,
                event_time,
            ))],
            // Pass other events unchanged.
            _ => vec![input_device::InputEvent::from(input_event)],
        }
    }
}

impl KeymapHandler {
    /// Creates a new instance of the keymap handler.
    pub fn new() -> Rc<Self> {
        Rc::new(Default::default())
    }

    /// Attaches a key meaning to each passing keyboard event.
    fn process_keyboard_event(
        self: &Rc<Self>,
        event: keyboard_binding::KeyboardEvent,
        device_descriptor: input_device::InputDeviceDescriptor,
        event_time: zx::Time,
    ) -> input_device::UnhandledInputEvent {
        let (key, event_type) = (event.get_key(), event.get_event_type());
        fx_log_debug!(
            concat!(
                "Keymap::process_keyboard_event: key:{:?}, ",
                "modifier_state:{:?}, lock_state: {:?}, event_type: {:?}"
            ),
            key,
            self.modifier_state.borrow(),
            self.lock_state.borrow(),
            event_type
        );

        self.modifier_state.borrow_mut().update(event_type, key);
        self.lock_state.borrow_mut().update(event_type, key);
        let key_meaning = keymaps::select_keymap(&event.get_keymap()).apply(
            key,
            &*self.modifier_state.borrow(),
            &*self.lock_state.borrow(),
        );
        input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(
                event.clone().into_with_key_meaning(key_meaning),
            ),
            device_descriptor,
            event_time,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{consumer_controls_binding, testing_utilities};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use pretty_assertions::assert_eq;
    use std::convert::TryFrom as _;

    // A mod-specific version of `testing_utilities::create_keyboard_event`.
    fn create_unhandled_keyboard_event(
        key: fidl_fuchsia_input::Key,
        event_type: fidl_fuchsia_ui_input3::KeyEventType,
        keymap: Option<String>,
    ) -> input_device::UnhandledInputEvent {
        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![fidl_fuchsia_input::Key::A, fidl_fuchsia_input::Key::B],
            },
        );
        let (_, event_time_u64) = testing_utilities::event_times();
        input_device::UnhandledInputEvent::try_from(testing_utilities::create_keyboard_event(
            key,
            event_type,
            /* modifiers= */ None,
            event_time_u64,
            &device_descriptor,
            keymap,
        ))
        .unwrap()
    }

    // A mod-specific version of `testing_utilities::create_consumer_controls_event`.
    fn create_unhandled_consumer_controls_event(
        pressed_buttons: Vec<fidl_fuchsia_input_report::ConsumerControlButton>,
        event_time: zx::Time,
        device_descriptor: &input_device::InputDeviceDescriptor,
    ) -> input_device::UnhandledInputEvent {
        input_device::UnhandledInputEvent::try_from(
            testing_utilities::create_consumer_controls_event(
                pressed_buttons,
                event_time,
                device_descriptor,
            ),
        )
        .unwrap()
    }

    fn get_key_meaning(
        event: &input_device::InputEvent,
    ) -> Option<fidl_fuchsia_ui_input3::KeyMeaning> {
        match event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(event),
                ..
            } => event.get_key_meaning(),
            _ => None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_keymap_application() {
        // Not using test_case crate because it does not compose very well with
        // async test execution.
        #[derive(Debug)]
        struct TestCase {
            events: Vec<input_device::UnhandledInputEvent>,
            expected: Vec<Option<fidl_fuchsia_ui_input3::KeyMeaning>>,
        }
        let tests: Vec<TestCase> = vec![
            TestCase {
                events: vec![create_unhandled_keyboard_event(
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
                events: vec![create_unhandled_consumer_controls_event(
                    vec![],
                    zx::Time::ZERO,
                    &input_device::InputDeviceDescriptor::ConsumerControls(
                        consumer_controls_binding::ConsumerControlsDeviceDescriptor {
                            buttons: vec![],
                        },
                    ),
                )],
                expected: vec![None],
            },
            TestCase {
                events: vec![
                    create_unhandled_keyboard_event(
                        fidl_fuchsia_input::Key::LeftShift,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                    create_unhandled_keyboard_event(
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
                    create_unhandled_keyboard_event(
                        fidl_fuchsia_input::Key::Tab,
                        fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                        Some("US_QWERTY".into()),
                    ),
                    create_unhandled_keyboard_event(
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
            let handler = KeymapHandler::new();
            for event in &test.events {
                let mut result = handler
                    .clone()
                    .handle_unhandled_input_event(event.clone())
                    .await
                    .iter()
                    .map(get_key_meaning)
                    .collect();
                actual.append(&mut result);
            }
            assert_eq!(test.expected, actual);
        }
    }
}
