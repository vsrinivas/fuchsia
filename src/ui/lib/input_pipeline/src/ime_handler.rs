// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::UnhandledInputHandler,
    crate::keyboard_binding,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_ui_input3::{self as fidl_ui_input3, LockState, Modifiers},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::{fx_log_debug, fx_log_err},
    fuchsia_zircon as zx,
    keymaps::{self, LockStateChecker, ModifierChecker},
    std::rc::Rc,
};

#[derive(Debug)]
pub struct FrozenLockState {
    lock_state: LockState,
}

impl From<LockState> for FrozenLockState {
    fn from(lock_state: LockState) -> Self {
        FrozenLockState { lock_state }
    }
}

impl LockStateChecker for FrozenLockState {
    fn test(&self, value: LockState) -> bool {
        self.lock_state.contains(value)
    }
}

/// Modifier state plus a tester method.
#[derive(Debug)]
pub struct FrozenModifierState {
    state: Modifiers,
}

impl From<fidl_fuchsia_ui_input3::Modifiers> for FrozenModifierState {
    fn from(state: Modifiers) -> Self {
        FrozenModifierState { state }
    }
}

impl ModifierChecker for FrozenModifierState {
    fn test(&self, value: Modifiers) -> bool {
        self.state.contains(value)
    }
}

/// [`ImeHandler`] is responsible for dispatching key events to the IME service, thus making sure
/// that key events are delivered to application runtimes (e.g., web, Flutter).
///
/// > NOTE: The [ImeHandler] requires [ModifierHandler] to be installed upstream to apply the keymaps correctly.
pub struct ImeHandler {
    /// The FIDL proxy (client-side stub) to the service for key event injection.
    key_event_injector: fidl_ui_input3::KeyEventInjectorProxy,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for ImeHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(ref keyboard_device_event),
                device_descriptor: input_device::InputDeviceDescriptor::Keyboard(_),
                event_time,
            } => {
                let key_event = create_key_event(&keyboard_device_event, event_time);
                self.dispatch_key(key_event).await;
                // Consume the input event.
                vec![input_device::InputEvent::from(unhandled_input_event).into_handled()]
            }
            _ => vec![input_device::InputEvent::from(unhandled_input_event)],
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
        let handler = ImeHandler { key_event_injector };

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
fn create_key_event(
    event: &keyboard_binding::KeyboardEvent,
    event_time: zx::Time,
) -> fidl_ui_input3::KeyEvent {
    let modifier_state: FrozenModifierState =
        event.get_modifiers().unwrap_or(Modifiers::from_bits_allow_unknown(0)).into();
    let lock_state: FrozenLockState =
        event.get_lock_state().unwrap_or(LockState::from_bits_allow_unknown(0)).into();
    fx_log_debug!(
        "ImeHandler::create_key_event: key:{:?}, modifier_state: {:?}, lock_state: {:?}, event_type: {:?}",
        event.get_key(),
        modifier_state,
        lock_state,
        event.get_event_type(),
    );
    // Don't override the key meaning if already set, e.g. by prior stage.
    let key_meaning = event.get_key_meaning().or(keymaps::US_QWERTY.apply(
        event.get_key(),
        &modifier_state,
        &lock_state,
    ));

    // Don't insert a spurious Some(0).
    let repeat_sequence = match event.get_repeat_sequence() {
        0 => None,
        s => Some(s),
    };

    fidl_ui_input3::KeyEvent {
        timestamp: Some(event_time.into_nanos()),
        type_: event.get_event_type().into(),
        key: event.get_key().into(),
        modifiers: event.get_modifiers(),
        lock_state: event.get_lock_state(),
        key_meaning,
        repeat_sequence,
        ..fidl_ui_input3::KeyEvent::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::keyboard_binding::{self, KeyboardEvent},
        crate::testing_utilities,
        assert_matches::assert_matches,
        fidl_fuchsia_input as fidl_input, fidl_fuchsia_ui_input3 as fidl_ui_input3,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt,
        std::convert::TryFrom as _,
        test_case::test_case,
    };

    fn handle_events(
        ime_handler: Rc<ImeHandler>,
        input_events: Vec<input_device::UnhandledInputEvent>,
    ) {
        fasync::Task::local(async move {
            for input_event in input_events {
                assert_matches!(
                    ime_handler.clone().handle_unhandled_input_event(input_event).await.as_slice(),
                    [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
                );
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
            pretty_assertions::assert_eq!(&key_event, expected_events_iter.next().unwrap());

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

    fn create_unhandled_keyboard_event(
        key: fidl_fuchsia_input::Key,
        event_type: fidl_fuchsia_ui_input3::KeyEventType,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: zx::Time,
        device_descriptor: &input_device::InputDeviceDescriptor,
        keymap: Option<String>,
    ) -> input_device::UnhandledInputEvent {
        create_unhandled_keyboard_event_with_key_meaning(
            key,
            event_type,
            modifiers,
            event_time,
            device_descriptor,
            keymap,
            /* key_meaning */ None,
        )
    }

    fn create_unhandled_keyboard_event_with_key_meaning(
        key: fidl_fuchsia_input::Key,
        event_type: fidl_fuchsia_ui_input3::KeyEventType,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: zx::Time,
        device_descriptor: &input_device::InputDeviceDescriptor,
        keymap: Option<String>,
        key_meaning: Option<fidl_fuchsia_ui_input3::KeyMeaning>,
    ) -> input_device::UnhandledInputEvent {
        input_device::UnhandledInputEvent::try_from(
            testing_utilities::create_keyboard_event_with_key_meaning(
                key,
                event_type,
                modifiers,
                event_time,
                device_descriptor,
                keymap,
                key_meaning,
            ),
        )
        .unwrap()
    }

    fn create_unhandled_input_event(
        keyboard_event: keyboard_binding::KeyboardEvent,
        device_descriptor: &input_device::InputDeviceDescriptor,
        event_time: zx::Time,
    ) -> input_device::UnhandledInputEvent {
        input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(keyboard_event),
            device_descriptor: device_descriptor.clone(),
            event_time,
        }
    }

    /// Tests that a pressed key event is dispatched.
    ///
    /// > NOTE: The `device_descriptor` used in this test case and elsewhere
    /// *must* be of type `KeyboardDeviceDescriptor` as this is required by the
    /// pattern matching in `ImeHandler`.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor { keys: vec![fidl_input::Key::A] },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![create_unhandled_keyboard_event(
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor { keys: vec![fidl_input::Key::A] },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![create_unhandled_keyboard_event(
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::B],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![
            create_unhandled_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Released,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
                fidl_input::Key::B,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event_with_key_meaning(
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
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Released),
                key: Some(fidl_input::Key::A),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::B),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(98)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::C),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(42)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    // Tests that modifier keys are dispatched appropriately.
    //
    // This test depends on the incoming event having correct modifier and lock
    // state.  Typically you'd do this by installing a ModifierHandler upstream
    // of this pipeline stage.
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let (proxy, request_stream) = connect_to_key_event_injector();
        let ime_handler =
            ImeHandler::new_handler(proxy).await.expect("Failed to create ImeHandler.");

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::A, fidl_input::Key::CapsLock],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![
            create_unhandled_input_event(
                KeyboardEvent::new(
                    fidl_input::Key::CapsLock,
                    fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                )
                .into_with_modifiers(Some(fidl_ui_input3::Modifiers::CAPS_LOCK))
                .into_with_lock_state(Some(fidl_ui_input3::LockState::CAPS_LOCK)),
                &device_descriptor,
                event_time_u64,
            ),
            create_unhandled_input_event(
                KeyboardEvent::new(
                    fidl_input::Key::A,
                    fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                )
                .into_with_modifiers(Some(fidl_ui_input3::Modifiers::CAPS_LOCK))
                .into_with_lock_state(Some(fidl_ui_input3::LockState::CAPS_LOCK)),
                &device_descriptor,
                event_time_u64,
            ),
            create_unhandled_input_event(
                KeyboardEvent::new(
                    fidl_input::Key::CapsLock,
                    fidl_fuchsia_ui_input3::KeyEventType::Released,
                )
                .into_with_lock_state(Some(fidl_ui_input3::LockState::CAPS_LOCK)),
                &device_descriptor,
                event_time_u64,
            ),
        ];

        let expected_events = vec![
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::CapsLock),
                modifiers: Some(fidl_ui_input3::Modifiers::CAPS_LOCK),
                lock_state: Some(fidl_ui_input3::LockState::CAPS_LOCK),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::A),
                modifiers: Some(fidl_ui_input3::Modifiers::CAPS_LOCK),
                lock_state: Some(fidl_ui_input3::LockState::CAPS_LOCK),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(65)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Released),
                key: Some(fidl_input::Key::CapsLock),
                lock_state: Some(fidl_ui_input3::LockState::CAPS_LOCK),
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![
                    fidl_input::Key::Enter,
                    fidl_input::Key::Tab,
                    fidl_input::Key::Backspace,
                ],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![
            create_unhandled_keyboard_event(
                fidl_input::Key::Enter,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
                fidl_input::Key::Tab,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
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
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Enter,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::Tab),
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![
                    fidl_input::Key::Enter,
                    fidl_input::Key::Tab,
                    fidl_input::Key::Backspace,
                ],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![create_unhandled_keyboard_event(
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![
            create_unhandled_keyboard_event(
                fidl_input::Key::LeftShift,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(Modifiers::LEFT_SHIFT | Modifiers::SHIFT),
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
                fidl_input::Key::RightShift,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(Modifiers::LEFT_SHIFT | Modifiers::RIGHT_SHIFT | Modifiers::SHIFT),
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
                fidl_input::Key::A,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                Some(Modifiers::LEFT_SHIFT | Modifiers::RIGHT_SHIFT | Modifiers::SHIFT),
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
                modifiers: Some(Modifiers::LEFT_SHIFT | Modifiers::SHIFT),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::RightShift),
                modifiers: Some(Modifiers::RIGHT_SHIFT | Modifiers::LEFT_SHIFT | Modifiers::SHIFT),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::A),
                modifiers: Some(Modifiers::RIGHT_SHIFT | Modifiers::LEFT_SHIFT | Modifiers::SHIFT),
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

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor {
                keys: vec![fidl_input::Key::LeftCtrl, fidl_input::Key::Tab],
            },
        );
        let (event_time_i64, event_time_u64) = testing_utilities::event_times();
        let input_events = vec![
            create_unhandled_keyboard_event(
                fidl_input::Key::LeftCtrl,
                fidl_fuchsia_ui_input3::KeyEventType::Pressed,
                None,
                event_time_u64,
                &device_descriptor,
                /* keymap= */ None,
            ),
            create_unhandled_keyboard_event(
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
                key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(0)),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
            fidl_ui_input3::KeyEvent {
                timestamp: Some(event_time_i64),
                type_: Some(fidl_ui_input3::KeyEventType::Pressed),
                key: Some(fidl_input::Key::Tab),
                key_meaning: Some(fidl_ui_input3::KeyMeaning::NonPrintableKey(
                    fidl_ui_input3::NonPrintableKey::Tab,
                )),
                ..fidl_ui_input3::KeyEvent::EMPTY
            },
        ];

        handle_events(ime_handler, input_events);
        assert_ime_receives_events(expected_events, request_stream).await;
    }

    #[test_case(
        keyboard_binding::KeyboardEvent::new(
            fidl_input::Key::A,
            fidl_ui_input3::KeyEventType::Pressed),
        zx::Time::from_nanos(42) => fidl_ui_input3::KeyEvent{
            timestamp: Some(42),
            type_: Some(fidl_ui_input3::KeyEventType::Pressed),
            key: Some(fidl_input::Key::A),
            key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
            ..fidl_ui_input3::KeyEvent::EMPTY}; "basic value copy")]
    #[test_case(
        keyboard_binding::KeyboardEvent::new(
            fidl_input::Key::A,
            fidl_ui_input3::KeyEventType::Pressed)
            .into_with_repeat_sequence(13),
        zx::Time::from_nanos(42) => fidl_ui_input3::KeyEvent{
            timestamp: Some(42),
            type_: Some(fidl_ui_input3::KeyEventType::Pressed),
            key: Some(fidl_input::Key::A),
            key_meaning: Some(fidl_ui_input3::KeyMeaning::Codepoint(97)),
            repeat_sequence: Some(13),
            ..fidl_ui_input3::KeyEvent::EMPTY}; "repeat_sequence is honored")]
    fn test_create_key_event(
        event: keyboard_binding::KeyboardEvent,
        event_time: zx::Time,
    ) -> fidl_ui_input3::KeyEvent {
        super::create_key_event(&event, event_time)
    }
}
