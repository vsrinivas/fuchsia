// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::keyboard,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_ui_input3 as fidl_ui_input3,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::{fx_log_debug, fx_log_err},
    keymaps,
    std::cell::RefCell,
    std::convert::TryInto,
    std::rc::Rc,
};

/// [`ImeHandler`] is responsible for dispatching key events to the IME service, thus making sure
/// that key events are delivered to application runtimes (e.g., web, Flutter).
pub struct ImeHandler {
    /// The FIDL proxy (client-side stub) to the service for key event injection.
    key_event_injector: fidl_ui_input3::KeyEventInjectorProxy,

    /// Tracks the state of shift keys, for keymapping purposes.
    modifier_tracker: RefCell<keymaps::ModifierState>,
}

#[async_trait(?Send)]
impl InputHandler for ImeHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(keyboard_device_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Keyboard(_keyboard_device_descriptor),
                event_time,
            } => {
                self.modifier_tracker
                    .borrow_mut()
                    .update(keyboard_device_event.event_type, keyboard_device_event.key);
                let key_event = create_key_event(
                    &keyboard_device_event,
                    event_time,
                    &self.modifier_tracker.borrow(),
                );
                self.dispatch_key(key_event).await;
                // Consume the input event.
                vec![]
            }
            no_match => vec![no_match],
        }
    }
}

#[allow(dead_code)]
impl ImeHandler {
    /// Creates a new [`ImeHandler`] by connecting out to the key event injector.
    pub async fn new() -> Result<Rc<Self>, Error> {
        let key_event_injector = connect_to_protocol::<fidl_ui_input3::KeyEventInjectorMarker>()?;

        Self::new_handler(key_event_injector).await
    }

    /// Creates a new [`ImeHandler`].
    ///
    /// # Parameters
    /// `key_event_injector`: A proxy (FIDL client-side stub) to the key event
    /// injector FIDL service.
    async fn new_handler(
        key_event_injector: fidl_ui_input3::KeyEventInjectorProxy,
    ) -> Result<Rc<Self>, Error> {
        let handler = ImeHandler { key_event_injector, modifier_tracker: Default::default() };

        Ok(Rc::new(handler))
    }

    /// Dispatches key events to IME and returns KeyboardEvents for unhandled events.
    ///
    /// # Parameters
    /// `key_events`: The key events to dispatch.
    /// `event_time`: The time in nanoseconds when the events were first recorded.
    async fn dispatch_key(self: &Rc<Self>, key_event: fidl_ui_input3::KeyEvent) {
        assert!(
            key_event.timestamp.is_some(),
            "dispatch_key: got a key_event without a timestamp: {:?}",
            &key_event
        );
        match self.key_event_injector.inject(key_event).await {
            Err(err) => fx_log_err!("Failed to dispatch key to IME: {:?}", err),
            _ => {}
        };
    }
}

/// Returns a KeyEvent with the given parameters.
///
/// # Parameters
/// * `event`: The keyboard event to process.
/// * `event_time`: The time in nanoseconds when the event was first recorded.
/// * `modifier_state`: The state of the monitored modifier keys (e.g. Shift, or CapsLock).
///   Used to determine, for example, whether a key press results in an `a` or an `A`.
fn create_key_event(
    event: &keyboard::KeyboardEvent,
    event_time: input_device::EventTime,
    modifier_state: &keymaps::ModifierState,
) -> fidl_ui_input3::KeyEvent {
    let (key, event_type, modifiers) = (&event.key, &event.event_type, &event.modifiers);
    fx_log_debug!(
        "ImeHandler::create_key_event: key:{:?}, modifier_state:{:?}, event_type: {:?}",
        key,
        &modifier_state,
        event_type
    );
    // Don't override the key meaning if already set, e.g. by prior stage.
    let key_meaning = event.key_meaning.or(keymaps::US_QWERTY.apply(*key, modifier_state));

    fidl_ui_input3::KeyEvent {
        timestamp: Some(event_time.try_into().unwrap_or_default()),
        type_: Some(*event_type),
        key: Some(*key),
        modifiers: *modifiers,
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

    fn handle_events(ime_handler: Rc<ImeHandler>, events: Vec<input_device::InputEvent>) {
        fasync::Task::local(async move {
            for event in events {
                let unhandled_events = ime_handler.clone().handle_input_event(event).await;
                assert_eq!(unhandled_events.len(), 0);
            }
        })
        .detach();
    }

    async fn assert_ime_receives_events(
        expected_events: Vec<fidl_ui_input3::KeyEvent>,
        mut request_stream: fidl_ui_input3::KeyEventInjectorRequestStream,
    ) {
        let mut expected_events_iter = expected_events.iter().peekable();
        while let Some(Ok(fidl_ui_input3::KeyEventInjectorRequest::Inject {
            key_event,
            responder,
            ..
        })) = request_stream.next().await
        {
            assert_eq!(&key_event, expected_events_iter.next().unwrap());

            // All the expected events have been received, so make sure no more events
            // are present before returning.
            if expected_events_iter.peek().is_none() {
                responder
                    .send(fidl_ui_input3::KeyEventStatus::Handled)
                    .expect("error responding to DispatchKey");
                return;
            }
            responder
                .send(fidl_ui_input3::KeyEventStatus::Handled)
                .expect("error responding to DispatchKey");
        }

        assert!(false);
    }

    fn connect_to_key_event_injector(
    ) -> (fidl_ui_input3::KeyEventInjectorProxy, fidl_ui_input3::KeyEventInjectorRequestStream)
    {
        fidl::endpoints::create_proxy_and_stream::<fidl_ui_input3::KeyEventInjectorMarker>()
            .expect("Failed to create proxy and stream for fuchsia.ui.input3.KeyEventInjector")
    }

    /// Tests that a pressed key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![testing_utilities::create_keyboard_event(
            fidl_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            None,
            event_time_u64,
            &device_descriptor,
            /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    /// Tests that a released key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn released_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![testing_utilities::create_keyboard_event(
            fidl_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            None,
            event_time_u64,
            &device_descriptor,
            /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    /// Tests that both pressed and released keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_and_released_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::B],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::B,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event_with_key_meaning(
                fidl_input::Key::C,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
                Some(fidl_fuchsia_ui_input3::KeyMeaning::Codepoint(42)),
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
                type_: Some(fidl_ui_input3::KeyEventType::Released),
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
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::C),
                modifiers: None,
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(42)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, request_stream).await;
    }
    /// Tests that modifier keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::CapsLock],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                fidl_input::Key::CapsLock,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::CapsLock,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn nonprintable_key_meanings_set_correctly() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

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
                fidl_input::Key::Enter,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::Tab,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::Backspace,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn tab() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

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
                fidl_input::Key::Tab,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn shift_shift_a() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                fidl_input::Key::LeftShift,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::RightShift,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn ctrl_tab() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            });
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                fidl_input::Key::LeftCtrl,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            testing_utilities::create_keyboard_event(
                fidl_input::Key::Tab,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
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
        assert_ime_receives_events(expected_events, request_stream).await;
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
