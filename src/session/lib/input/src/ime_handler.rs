// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_ui_input as fidl_ui_input, fidl_fuchsia_ui_input2 as fidl_ui_input2,
    fidl_fuchsia_ui_input2::{Key, KeyEventPhase, Modifiers},
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_scenic as scenic,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    fuchsia_zircon::Time,
    futures::StreamExt,
    input_synthesis::usages::key_to_hid_usage,
};

/// [`ImeHandler`] is responsible for dispatching key events to the IME service, thus making sure
/// that key events are delivered to application runtimes (e.g., web, Flutter).
///
/// Key events are also sent to Scenic, as some application runtimes rely on Scenic to deliver
/// key events.
pub struct ImeHandler {
    /// The proxy to the IME service.
    ime_proxy: fidl_ui_input::ImeServiceProxy,

    /// The proxy to the keyboard service.
    _keyboard_proxy: fidl_ui_input2::KeyboardProxy,

    /// The key listener registered with the IME service. The ImeHandler sends keys to IME, and then
    /// listens for events on this request stream. This allows the ImeHandler to rely on the IME
    /// service to generate the proper keys to send to clients.
    key_listener: fidl_ui_input2::KeyListenerRequestStream,

    /// The Scenic session to send keyboard events to.
    scenic_session: scenic::SessionPtr,

    /// The id of the compositor used for the scene's layer stack. This is used when sending
    /// keyboard events to the `scenic_session`.
    scenic_compositor_id: u32,
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
                event_time: _,
            } => {
                let pressed_keys: Vec<Key> =
                    keyboard_device_event.get_keys(fidl_ui_input2::KeyEventPhase::Pressed);
                let pressed_key_events: Vec<fidl_ui_input::KeyboardEvent> = self
                    .dispatch_keys(
                        pressed_keys,
                        keyboard_device_event.modifiers,
                        fidl_ui_input2::KeyEventPhase::Pressed,
                    )
                    .await;

                let released_keys: Vec<Key> =
                    keyboard_device_event.get_keys(fidl_ui_input2::KeyEventPhase::Released);
                let released_key_events: Vec<fidl_ui_input::KeyboardEvent> = self
                    .dispatch_keys(
                        released_keys,
                        keyboard_device_event.modifiers,
                        fidl_ui_input2::KeyEventPhase::Released,
                    )
                    .await;

                let keyboard_events: Vec<fidl_ui_input::KeyboardEvent> =
                    pressed_key_events.into_iter().chain(released_key_events).collect();
                send_events_to_scenic(
                    keyboard_events,
                    self.scenic_session.clone(),
                    self.scenic_compositor_id,
                );

                // Consume the input event.
                vec![]
            }
            no_match => vec![no_match],
        }
    }
}

#[allow(dead_code)]
impl ImeHandler {
    /// Creates a new [`ImeHandler`] and connects to IME and Keyboard services.
    ///
    /// # Parameters
    /// `scenic_session`: The Scenic session to send keyboard events to.
    /// `scenic_compositor_id`: The id of the compositor used for the scene's layer stack.
    pub async fn new(
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
    ) -> Result<Self, Error> {
        let ime = connect_to_service::<fidl_ui_input::ImeServiceMarker>()?;
        let keyboard = connect_to_service::<fidl_ui_input2::KeyboardMarker>()?;
        let (listener_client_end, key_listener) =
            fidl::endpoints::create_request_stream::<fidl_ui_input2::KeyListenerMarker>()?;
        Self::new_handler(
            scenic_session,
            scenic_compositor_id,
            ime,
            keyboard,
            listener_client_end,
            key_listener,
        )
        .await
    }

    /// Creates a new [`ImeHandler`].
    ///
    /// # Parameters
    /// `scenic_session`: The Scenic session to send keyboard events to.
    /// `scenic_compositor_id`: The id of the compositor used for the scene's layer stack.
    /// `ime_proxy`: A proxy to the IME service.
    /// `keyboard_proxy`: A proxy to the Keyboard service.
    async fn new_handler(
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
        ime_proxy: fidl_ui_input::ImeServiceProxy,
        keyboard_proxy: fidl_ui_input2::KeyboardProxy,
        key_listener_proxy: fidl::endpoints::ClientEnd<fidl_ui_input2::KeyListenerMarker>,
        key_listener: fidl_ui_input2::KeyListenerRequestStream,
    ) -> Result<Self, Error> {
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
        keyboard_proxy.set_listener(view_ref, key_listener_proxy).await?;

        let handler = ImeHandler {
            ime_proxy,
            _keyboard_proxy: keyboard_proxy,
            key_listener,
            scenic_session,
            scenic_compositor_id,
        };

        Ok(handler)
    }

    /// Dispatches key events to IME.
    ///
    /// # Parameters
    /// `keys`: The keys to dispatch.
    /// `modifiers`: The modifiers associated with the keys to dispatch.
    /// `phase`: The phase of the key event to dispatch.
    ///
    /// # Returns
    /// A vector of KeyboardEvents to be sent to the Scenic session. This will be removed once
    /// Scenic no longer handles keyboard events.
    async fn dispatch_keys(
        &mut self,
        keys: Vec<Key>,
        modifiers: Option<Modifiers>,
        phase: fidl_ui_input2::KeyEventPhase,
    ) -> Vec<fidl_ui_input::KeyboardEvent> {
        let mut events: Vec<fidl_ui_input::KeyboardEvent> = vec![];
        for key in keys {
            let key_event: fidl_ui_input2::KeyEvent = create_key_event(&key, phase, modifiers);
            let keyboard_event: Option<fidl_ui_input::KeyboardEvent> =
                dispatch_key_event(key_event, &self.ime_proxy, &mut self.key_listener).await;
            if let Some(event) = keyboard_event {
                events.push(event);
            }
        }

        events
    }
}

/// Returns a KeyEvent with the given parameters.
///
/// # Parameters
/// `key`: The key associated with the KeyEvent.
/// `phase`: The phase of key, either pressed or released.
/// `modifiers`: The modifiers associated the KeyEvent.
fn create_key_event(
    key: &Key,
    phase: KeyEventPhase,
    modifiers: Option<Modifiers>,
) -> fidl_ui_input2::KeyEvent {
    fidl_ui_input2::KeyEvent {
        key: Some(*key),
        phase: Some(phase),
        modifiers: modifiers,
        semantic_key: None,
        physical_key: Some(*key),
    }
}

/// Returns a KeyboardEvent from the given parameters.
///
/// Note: A new timestamp is generated for the returned KeyboardEvent because
/// `fidl_ui_input2::KeyEvent`s don't propagate timestamps.
///
/// # Parameters
/// `event`: The KeyEvent to create the KeyboardEvent from.
fn create_keyboard_event(event: fidl_ui_input2::KeyEvent) -> fidl_ui_input::KeyboardEvent {
    let phase: fidl_ui_input::KeyboardEventPhase = match event.phase {
        Some(fidl_ui_input2::KeyEventPhase::Released) => {
            fidl_ui_input::KeyboardEventPhase::Released
        }
        _ => fidl_ui_input::KeyboardEventPhase::Pressed,
    };

    let code_point = event
        .semantic_key
        .map(|semantic_key| match semantic_key {
            fidl_ui_input2::SemanticKey::Symbol(symbol) => {
                symbol.chars().next().unwrap_or_default() as u32
            }
            _ => 0,
        })
        .unwrap_or_default();

    let modifiers = event.modifiers.map(|modifiers| modifiers.bits()).unwrap_or_default();
    let hid_usage = event.physical_key.map(key_to_hid_usage).unwrap_or_default();

    fidl_ui_input::KeyboardEvent {
        event_time: Time::get_monotonic().into_nanos() as input_device::EventTime,
        device_id: 1,
        phase,
        hid_usage,
        code_point,
        modifiers,
    }
}

/// Returns a KeyboardEvent after dispatching `key_event` to IME.
///
/// The Scenic session currently consumes the returned KeyboardEvent. In the future, IME will
/// handle all key events directly.
///
/// # Parameters
/// `key_event`: The KeyEvent to dispatch to IME.
/// `ime`: The proxy to the IME service.
/// `key_listener`: The keyboard listener request stream.
async fn dispatch_key_event(
    key_event: fidl_ui_input2::KeyEvent,
    ime: &fidl_ui_input::ImeServiceProxy,
    key_listener: &mut fidl_ui_input2::KeyListenerRequestStream,
) -> Option<fidl_ui_input::KeyboardEvent> {
    let fut = ime.dispatch_key(key_event);
    fasync::Task::spawn(async move {
        let _ = fut.await;
    })
    .detach();

    match key_listener.next().await {
        Some(Ok(fidl_ui_input2::KeyListenerRequest::OnKeyEvent { event, responder, .. })) => {
            let keyboard_event = create_keyboard_event(event);

            responder
                .send(fidl_ui_input2::Status::NotHandled)
                .expect("Keyboard listener failed to send respond.");
            return Some(keyboard_event);
        }
        _ => fx_log_err!("Did not get response from IME after dispatching key."),
    }

    None
}

/// Sends `keybaord_event` to the Scenic session. This will be removed once Scenic no longer handles
/// keyboard events.
///
/// `keyboard_events`: The events to send to the Scenic session.
/// `scenic_session`: The Scenic session.
/// `scenic_compositor_id`: The id of the compositor used for the scene's layer stack.
fn send_events_to_scenic(
    keyboard_events: Vec<fidl_ui_input::KeyboardEvent>,
    scenic_session: scenic::SessionPtr,
    scenic_compositor_id: u32,
) {
    for keyboard_event in keyboard_events {
        let command =
            fidl_ui_input::Command::SendKeyboardInput(fidl_ui_input::SendKeyboardInputCmd {
                compositor_id: scenic_compositor_id,
                keyboard_event,
            });
        scenic_session.lock().enqueue(fidl_fuchsia_ui_scenic::Command::Input(command));
        scenic_session.lock().flush()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::keyboard, crate::testing_utilities,
        fidl_fuchsia_ui_scenic as fidl_ui_scenic, fuchsia_zircon as zx,
    };
    const COMPOSITOR_ID: u32 = 1;

    /// Creates an [`ImeHandler`] for tests.
    ///
    /// This routes key events from the IME service to a key listener.
    ///
    /// # Parameters
    /// - `scenic_session`: The Scenic session to send keyboard events to.
    async fn create_ime_handler(scenic_session: scenic::SessionPtr) -> ImeHandler {
        let (ime_proxy, mut ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()
                .expect("Failed to create ImeProxy and stream.");

        let (keyboard_proxy, mut keyboard_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input2::KeyboardMarker>()
                .expect("Failed to create KeyboardProxy and stream.");

        fuchsia_async::Task::spawn(async move {
            match keyboard_request_stream.next().await {
                Some(Ok(fidl_ui_input2::KeyboardRequest::SetListener {
                    view_ref: _,
                    listener: _,
                    responder,
                })) => {
                    responder.send().expect("keyboard service set listener");
                }
                _ => assert!(false),
            }
        })
        .detach();

        let (key_listener_client_end, key_listener) =
            fidl::endpoints::create_request_stream::<fidl_ui_input2::KeyListenerMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            let key_listener_client = key_listener_client_end.into_proxy().unwrap().clone();
            loop {
                match ime_request_stream.next().await {
                    Some(Ok(fidl_ui_input::ImeServiceRequest::DispatchKey {
                        event,
                        responder,
                        ..
                    })) => {
                        let _ = key_listener_client.clone().on_key_event(event).await;
                        responder.send(true).expect("error responding to DispatchKey");
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        // This dummy key listener is passed to [`Keyboard.SetListener()`] but not used.
        let (dummy_key_listener_client_end, _dummy_key_listener) =
            fidl::endpoints::create_request_stream::<fidl_ui_input2::KeyListenerMarker>().unwrap();

        ImeHandler::new_handler(
            scenic_session,
            COMPOSITOR_ID,
            ime_proxy,
            keyboard_proxy,
            dummy_key_listener_client_end,
            key_listener,
        )
        .await
        .expect("Failed to create ImeHandler.")
    }

    /// Validates the first event in `cmds` against `expected_event`.
    ///
    /// Note: `event_time` will not match `expected_event.event_time`. The latter is
    /// generated by this handler because `fidl_ui_input2::KeyEvent`s don't propagate timestamps.
    ///
    ///
    /// # Parameters
    /// - `commands`: The commands received by the Scenic session.
    /// - `expected_event`: The expected event.
    fn verify_keyboard_event(
        command: fidl_ui_scenic::Command,
        expected_event: fidl_ui_input::KeyboardEvent,
    ) {
        match command {
            fidl_ui_scenic::Command::Input(fidl_ui_input::Command::SendKeyboardInput(
                fidl_ui_input::SendKeyboardInputCmd {
                    compositor_id: _,
                    keyboard_event:
                        fidl_ui_input::KeyboardEvent {
                            event_time: _,
                            device_id,
                            phase,
                            hid_usage,
                            code_point: _,
                            modifiers,
                        },
                },
            )) => {
                assert_eq!(device_id, expected_event.device_id);
                assert_eq!(phase, expected_event.phase);
                assert_eq!(hid_usage, expected_event.hid_usage);
                assert_eq!(modifiers, expected_event.modifiers);
            }
            _ => {
                assert!(false);
            }
        }
    }

    /// Tests that a pressed key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_key() {
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut ime_handler = create_ime_handler(scenic_session.clone()).await;

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![Key::A],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![Key::A],
            vec![],
            None,
            event_time,
            &device_descriptor,
        )];

        let expected_commands = vec![fidl_ui_input::KeyboardEvent {
            event_time: event_time,
            device_id: 1,
            phase: fidl_ui_input::KeyboardEventPhase::Pressed,
            hid_usage: key_to_hid_usage(Key::A),
            code_point: 1,
            modifiers: 0,
        }];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: ime_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_keyboard_event,
        );
    }

    /// Tests that a released key event is dispatched.
    #[fasync::run_singlethreaded(test)]
    async fn released_key() {
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut ime_handler = create_ime_handler(scenic_session.clone()).await;

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![Key::A],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![],
            vec![Key::A],
            None,
            event_time,
            &device_descriptor,
        )];

        let expected_commands = vec![fidl_ui_input::KeyboardEvent {
            event_time: event_time,
            device_id: 1,
            phase: fidl_ui_input::KeyboardEventPhase::Released,
            hid_usage: key_to_hid_usage(Key::A),
            code_point: 1,
            modifiers: 0,
        }];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: ime_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_keyboard_event,
        );
    }

    /// Tests that both pressed and released keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn pressed_and_released_key() {
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut ime_handler = create_ime_handler(scenic_session.clone()).await;

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![Key::A, Key::B],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![Key::A],
                vec![],
                None,
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![Key::B],
                vec![Key::A],
                None,
                event_time,
                &device_descriptor,
            ),
        ];

        let expected_commands = vec![
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: key_to_hid_usage(Key::A),
                code_point: 1,
                modifiers: 0,
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: key_to_hid_usage(Key::B),
                code_point: 1,
                modifiers: 0,
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Released,
                hid_usage: key_to_hid_usage(Key::A),
                code_point: 1,
                modifiers: 0,
            },
        ];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: ime_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_keyboard_event,
        );
    }

    /// Tests that modifier keys are dispatched appropriately.
    #[fasync::run_singlethreaded(test)]
    async fn repeated_modifier_key() {
        let (session_proxy, mut session_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_scenic::SessionMarker>()
                .expect("Failed to create ScenicProxy and stream.");
        let scenic_session: scenic::SessionPtr = scenic::Session::new(session_proxy);
        let mut ime_handler = create_ime_handler(scenic_session.clone()).await;

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![Key::A, Key::LeftShift],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![Key::LeftShift],
                vec![],
                Some(Modifiers::Shift | Modifiers::LeftShift),
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![Key::A],
                vec![],
                Some(Modifiers::Shift | Modifiers::LeftShift),
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![Key::LeftShift],
                None,
                event_time,
                &device_descriptor,
            ),
        ];

        let expected_commands = vec![
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: key_to_hid_usage(Key::LeftShift),
                code_point: 1,
                modifiers: 3, // Modifiers::Shift | Modifiers::LeftShift
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: key_to_hid_usage(Key::A),
                code_point: 1,
                modifiers: 3, // Modifiers::Shift | Modifiers::LeftShift
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Released,
                hid_usage: key_to_hid_usage(Key::LeftShift),
                code_point: 1,
                modifiers: 0,
            },
        ];

        assert_input_event_sequence_generates_scenic_events!(
            input_handler: ime_handler,
            input_events: input_events,
            expected_commands: expected_commands,
            scenic_session_request_stream: session_request_stream,
            assert_command: verify_keyboard_event,
        );
    }
}
