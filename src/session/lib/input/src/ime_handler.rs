// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_input as fidl_input, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_input3 as fidl_ui_input3,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_err,
    input_synthesis::{keymaps, usages},
    std::convert::TryInto,
};

/// [`ImeHandler`] is responsible for dispatching key events to the IME service, thus making sure
/// that key events are delivered to application runtimes (e.g., web, Flutter).
pub struct ImeHandler {
    /// The proxy to the IME service.
    ime_proxy: fidl_ui_input::ImeServiceProxy,

    /// Tracks the state of shift keys, for keymapping purposes.
    modifier_tracker: keymaps::ModifierState,
}

#[async_trait]
impl InputHandler for ImeHandler {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(keyboard_device_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Keyboard(_keyboard_device_descriptor),
                event_time,
            } => {
                let _pressed_updates = keyboard_device_event
                    .get_keys(fidl_ui_input3::KeyEventType::Pressed)
                    .iter()
                    .for_each(|k| {
                        self.modifier_tracker.update(fidl_ui_input3::KeyEventType::Pressed, *k)
                    });
                let _released_updates = keyboard_device_event
                    .get_keys(fidl_ui_input3::KeyEventType::Released)
                    .iter()
                    .for_each(|k| {
                        self.modifier_tracker.update(fidl_ui_input3::KeyEventType::Released, *k);
                    });

                let pressed_key_events: Vec<fidl_ui_input3::KeyEvent> = keyboard_device_event
                    .get_keys(fidl_ui_input3::KeyEventType::Pressed)
                    .into_iter()
                    .map(|key| {
                        create_key_event(
                            &key,
                            fidl_ui_input3::KeyEventType::Pressed,
                            keyboard_device_event.modifiers,
                            event_time,
                            &self.modifier_tracker,
                        )
                    })
                    .collect();
                self.dispatch_keys(pressed_key_events).await;

                let released_key_events: Vec<fidl_ui_input3::KeyEvent> = keyboard_device_event
                    .get_keys(fidl_ui_input3::KeyEventType::Released)
                    .into_iter()
                    .map(|key| {
                        create_key_event(
                            &key,
                            fidl_ui_input3::KeyEventType::Released,
                            keyboard_device_event.modifiers,
                            event_time,
                            &self.modifier_tracker,
                        )
                    })
                    .collect();
                self.dispatch_keys(released_key_events).await;

                // Consume the input event.
                vec![]
            }
            no_match => vec![no_match],
        }
    }
}

#[allow(dead_code)]
impl ImeHandler {
    /// Creates a new [`ImeHandler`] and connects to the IME service.
    pub async fn new() -> Result<Self, Error> {
        let ime = connect_to_service::<fidl_ui_input::ImeServiceMarker>()?;

        Self::new_handler(ime).await
    }

    /// Creates a new [`ImeHandler`].
    ///
    /// # Parameters
    /// `ime_proxy`: A proxy to the IME service.
    async fn new_handler(ime_proxy: fidl_ui_input::ImeServiceProxy) -> Result<Self, Error> {
        let handler = ImeHandler { ime_proxy, modifier_tracker: Default::default() };

        Ok(handler)
    }

    /// Dispatches key events to IME and returns KeyboardEvents for unhandled events.
    ///
    /// # Parameters
    /// `key_events`: The key events to dispatch.
    /// `event_time`: The time in nanoseconds when the events were first recorded.
    async fn dispatch_keys(&mut self, key_events: Vec<fidl_ui_input3::KeyEvent>) {
        for key_event in key_events {
            match self.ime_proxy.dispatch_key3(key_event).await {
                Err(err) => fx_log_err!("Failed to dispatch key to IME: {:?}", err),
                _ => {}
            };
        }
    }
}

/// Returns a KeyEvent with the given parameters.
///
/// # Parameters
/// * `key`: The key associated with the KeyEvent.
/// * `event_type`: The type of key, either pressed or released.
/// * `modifiers`: The modifiers associated the KeyEvent.
/// * `event_time`: The time in nanoseconds when the event was first recorded.
/// * `modifier_state`: The state of the monitored modifier keys (e.g. Shift, or CapsLock).
///   Used to determine, for example, whether a key press results in an `a` or an `A`.
fn create_key_event(
    key: &fidl_input::Key,
    event_type: fidl_ui_input3::KeyEventType,
    modifiers: Option<fidl_ui_input3::Modifiers>,
    event_time: input_device::EventTime,
    modifier_state: &keymaps::ModifierState,
) -> fidl_ui_input3::KeyEvent {
    // TODO(fxbug.dev/73295): Conversion to key meaning should be the task of a keymapper input
    // pipeline stage.  It is not implemented yet.  Until it is, we compute the key meaning based
    // on the US QWERTY key map.
    let hid_usage = usages::input3_key_to_hid_usage(*key);
    let key_meaning = match key {
        // Nonprintable keys get their own key meaning.
        fidl_input::Key::Enter => Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
            fidl_ui_input3::NonPrintableKey::Enter,
        )),
        fidl_input::Key::Tab => {
            Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(fidl_ui_input3::NonPrintableKey::Tab))
        }
        fidl_input::Key::Backspace => Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
            fidl_ui_input3::NonPrintableKey::Backspace,
        )),
        // Printable keys get code points as key meanings.
        _ => keymaps::hid_usage_to_code_point(hid_usage, modifier_state)
            .map(fidl_ui_input3::KeyMeaning::Codepoint)
            .map_err(|e| {
                fx_log_err!(
                    "ImeHandler::create_key_event: Could not convert HID usage to code point: {:?}",
                    &hid_usage
                );
                e
            })
            .ok(),
    };

    fidl_ui_input3::KeyEvent {
        timestamp: Some(event_time.try_into().unwrap_or_default()),
        type_: Some(event_type),
        key: Some(*key),
        modifiers,
        key_meaning,
        ..fidl_ui_input3::KeyEvent::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::keyboard, crate::testing_utilities, fidl_fuchsia_input as fidl_input,
        fidl_fuchsia_ui_input3 as fidl_ui_input3, fuchsia_async as fasync, futures::StreamExt,
    };

    fn handle_events(mut ime_handler: ImeHandler, events: Vec<input_device::InputEvent>) {
        fasync::Task::spawn(async move {
            for event in events {
                let unhandled_events = ime_handler.handle_input_event(event).await;
                assert_eq!(unhandled_events.len(), 0);
            }
        })
        .detach();
    }

    async fn assert_ime_receives_events(
        expected_events: Vec<fidl_ui_input3::KeyEvent>,
        mut ime_request_stream: fidl_ui_input::ImeServiceRequestStream,
    ) {
        let mut expected_events_iter = expected_events.iter().peekable();
        while let Some(Ok(fidl_ui_input::ImeServiceRequest::DispatchKey3 {
            event,
            responder,
            ..
        })) = ime_request_stream.next().await
        {
            assert_eq!(&event, expected_events_iter.next().unwrap());

            // All the expected events have been received, so make sure no more events
            // are present before returning.
            if expected_events_iter.peek().is_none() {
                responder.send(true).expect("error responding to DispatchKey");
                return;
            }
            responder.send(true).expect("error responding to DispatchKey");
        }

        assert!(false);
    }

    /// Tests that a pressed key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![fidl_input::Key::A],
            vec![],
            None,
            event_time_u64,
            &device_descriptor,
        )];

        let expected_events = vec![fidl_ui_input3::KeyEvent {
            timestamp: Some(event_time_i64),
            type_: Some(fidl_ui_input3::KeyEventType::Pressed),
            key: Some(fidl_input::Key::A),
            modifiers: None,
            // A key "A" without shift is a lowercase 'a'.
            key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
            ..fidl_ui_input3::KeyEvent::EMPTY
        }];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    /// Tests that a released key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn released_key() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![],
            vec![fidl_input::Key::A],
            None,
            event_time_u64,
            &device_descriptor,
        )];

        let expected_events = vec![fidl_ui_input3::KeyEvent {
            timestamp: Some(event_time_i64),
            type_: Some(fidl_ui_input3::KeyEventType::Released),
            key: Some(fidl_input::Key::A),
            modifiers: None,
            key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
            ..fidl_ui_input3::KeyEvent::EMPTY
        }];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    /// Tests that both pressed and released keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_and_released_key() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::B],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::A],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::B],
                vec![fidl_input::Key::A],
                None,
                event_time_u64,
                &device_descriptor,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::A),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::B),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(98)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Released),
                key: Some(fidl_input::Key::A),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }
    /// Tests that modifier keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::CapsLock],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::CapsLock],
                vec![],
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::A],
                vec![],
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![fidl_input::Key::CapsLock],
                None,
                event_time_u64,
                &device_descriptor,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::CapsLock),
                modifiers: Some(fidl_ui_input3::Modifiers::CapsLock),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::A),
                modifiers: Some(fidl_ui_input3::Modifiers::CapsLock),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(65)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Released),
                key: Some(fidl_input::Key::CapsLock),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn nonprintable_key_meanings_set_correctly() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![
                    fidl_input::Key::Enter,
                    fidl_input::Key::Tab,
                    fidl_input::Key::Backspace,
                ],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::Enter],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::Tab],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![fidl_input::Key::Backspace],
                None,
                event_time_u64,
                &device_descriptor,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::Enter),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Enter,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::Tab),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Tab,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                // Test that things also work when a key is released.
                type_: Some(fidl_ui_input3::KeyEventType::Released),
                key: Some(fidl_input::Key::Backspace),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Backspace,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn tab() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![
                    fidl_input::Key::Enter,
                    fidl_input::Key::Tab,
                    fidl_input::Key::Backspace,
                ],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> =
            vec![testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::Tab],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            )];

        let expected_events = vec![fidl_ui_input3::KeyEvent {
            timestamp: Some(event_time_i64),
            type_: Some(fidl_ui_input3::KeyEventType::Pressed),
            key: Some(fidl_input::Key::Tab),
            modifiers: None,
            key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                fidl_ui_input3::NonPrintableKey::Tab,
            )),
            ..fidl_ui_input3::KeyEvent::EMPTY
        }];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn shift_shift_a() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::LeftShift],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::RightShift],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::A],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::LeftShift),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::RightShift),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::A),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(65)), // "A"
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn ctrl_tab() {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");
        let ime_handler =
            ImeHandler::new_handler(ime_proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::LeftCtrl],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_input::Key::Tab],
                vec![],
                None,
                event_time_u64,
                &device_descriptor,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::LeftCtrl),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::Tab),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Tab,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, ime_request_stream).await;
    }

    /// If this test fails to compile, it means that a new value is added to the FIDL enum
    /// NonPrintableKey.  If you see this test fail to compile, please make sure that the match
    /// statement in `create_key_event` also contains all non-printable key values, since its
    /// correctness depends on handling all the defined enums.
    #[test]
    fn guard_nonprintable_key_enums() {
        let key = fidl_ui_input3::NonPrintableKey::Enter;
        assert_eq!(
            true,
            match key {
                // This match is intentionally made to fail to compile if a new enum is added to
                // NonPrintableKey. See comment at the top of this test before adding a new
                // match arm here.
                fidl_ui_input3::NonPrintableKey::Backspace => true,
                fidl_ui_input3::NonPrintableKey::Tab => true,
                fidl_ui_input3::NonPrintableKey::Enter => true,
                fidl_ui_input3::NonPrintableKeyUnknown!() => true,
            }
        );
    }
}
