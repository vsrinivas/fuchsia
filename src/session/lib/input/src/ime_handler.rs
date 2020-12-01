// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::input_handler::InputHandler, anyhow::Error,
    async_trait::async_trait, fidl_fuchsia_input as fidl_input,
    fidl_fuchsia_ui_input as fidl_ui_input, fidl_fuchsia_ui_input3 as fidl_ui_input3,
    fuchsia_component::client::connect_to_service, fuchsia_scenic as scenic,
    fuchsia_syslog::fx_log_err, input_synthesis::keymaps::QWERTY_MAP,
    input_synthesis::usages::input3_key_to_hid_usage, std::convert::TryInto,
};

/// [`ImeHandler`] is responsible for dispatching key events to the IME service, thus making sure
/// that key events are delivered to application runtimes (e.g., web, Flutter).
///
/// Key events are also sent to Scenic, as some application runtimes rely on Scenic to deliver
/// key events.
pub struct ImeHandler {
    /// The proxy to the IME service.
    ime_proxy: fidl_ui_input::ImeServiceProxy,

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
                event_time,
            } => {
                let pressed_key_events: Vec<fidl_ui_input3::KeyEvent> = keyboard_device_event
                    .get_keys3(fidl_ui_input3::KeyEventType::Pressed)
                    .into_iter()
                    .map(|key| {
                        create_key_event(
                            &key,
                            fidl_ui_input3::KeyEventType::Pressed,
                            keyboard_device_event.modifiers3,
                            event_time,
                        )
                    })
                    .collect();
                let pressed_keyboard_events =
                    self.dispatch_keys(pressed_key_events, event_time).await;

                let released_key_events: Vec<fidl_ui_input3::KeyEvent> = keyboard_device_event
                    .get_keys3(fidl_ui_input3::KeyEventType::Released)
                    .into_iter()
                    .map(|key| {
                        create_key_event(
                            &key,
                            fidl_ui_input3::KeyEventType::Released,
                            keyboard_device_event.modifiers3,
                            event_time,
                        )
                    })
                    .collect();
                let released_keyboard_events =
                    self.dispatch_keys(released_key_events, event_time).await;

                let keyboard_events =
                    pressed_keyboard_events.into_iter().chain(released_keyboard_events).collect();

                // Dispatch KeyboardEvents to Scenic.
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

        Self::new_handler(scenic_session, scenic_compositor_id, ime).await
    }

    /// Creates a new [`ImeHandler`].
    ///
    /// # Parameters
    /// `scenic_session`: The Scenic session to send keyboard events to.
    /// `scenic_compositor_id`: The id of the compositor used for the scene's layer stack.
    /// `ime_proxy`: A proxy to the IME service.
    async fn new_handler(
        scenic_session: scenic::SessionPtr,
        scenic_compositor_id: u32,
        ime_proxy: fidl_ui_input::ImeServiceProxy,
    ) -> Result<Self, Error> {
        let handler = ImeHandler { ime_proxy, scenic_session, scenic_compositor_id };

        Ok(handler)
    }

    /// Dispatches key events to IME and returns KeyboardEvents for unhandled events.
    ///
    /// # Parameters
    /// `key_events`: The key events to dispatch.
    /// `event_time`: The time in nanoseconds when the events were first recorded.
    ///
    /// # Returns
    /// A vector of KeyboardEvents to be sent to the Scenic session.
    /// This will be removed once Scenic no longer handles keyboard events.
    async fn dispatch_keys(
        &mut self,
        key_events: Vec<fidl_ui_input3::KeyEvent>,
        event_time: input_device::EventTime,
    ) -> Vec<fidl_ui_input::KeyboardEvent> {
        let mut events: Vec<fidl_ui_input::KeyboardEvent> = vec![];
        for key_event in key_events {
            let keyboard_event = create_keyboard_event(&key_event, event_time);
            match self.ime_proxy.dispatch_key3(key_event).await {
                Ok(was_handled) => {
                    if !was_handled {
                        events.push(keyboard_event);
                    }
                }
                Err(err) => fx_log_err!("Failed to dispatch key to IME: {:?}", err),
            };
        }
        events
    }
}

/// Returns a KeyEvent with the given parameters.
///
/// # Parameters
/// `key`: The key associated with the KeyEvent.
/// `event_type`: The type of key, either pressed or released.
/// `modifiers`: The modifiers associated the KeyEvent.
/// `event_time`: The time in nanoseconds when the event was first recorded.
fn create_key_event(
    key: &fidl_input::Key,
    event_type: fidl_ui_input3::KeyEventType,
    modifiers: Option<fidl_ui_input3::Modifiers>,
    event_time: input_device::EventTime,
) -> fidl_ui_input3::KeyEvent {
    fidl_ui_input3::KeyEvent {
        timestamp: Some(event_time.try_into().unwrap_or_default()),
        type_: Some(event_type),
        key: Some(*key),
        modifiers,
        ..fidl_ui_input3::KeyEvent::EMPTY
    }
}

/// Returns a Keyboard with the given parameters.
///
/// # Parameters
/// `event`: The KeyEvent to create the KeyboardEvent from.
/// `event_time`: The time in nanoseconds when the event was first recorded.
fn create_keyboard_event(
    event: &fidl_ui_input3::KeyEvent,
    event_time: input_device::EventTime,
) -> fidl_ui_input::KeyboardEvent {
    let phase = match event.type_ {
        Some(fidl_ui_input3::KeyEventType::Released) => fidl_ui_input::KeyboardEventPhase::Released,
        _ => fidl_ui_input::KeyboardEventPhase::Pressed,
    };

    let modifiers: u32 = event
        .modifiers
        .map(|modifiers| modifiers.bits())
        .unwrap_or_default()
        .try_into()
        .unwrap_or_default();
    let hid_usage = event.key.map(input3_key_to_hid_usage).unwrap_or_default();
    let code_point = hid_usage_to_codepoint(hid_usage).unwrap_or(0);

    fidl_ui_input::KeyboardEvent {
        event_time,
        device_id: 1,
        phase,
        hid_usage,
        code_point,
        modifiers,
    }
}

/// Returns a Unicode codepoint that corresponds with the USB HID code.
///
/// This assumes a standard "qwerty" keymap. Note that this only returns
/// lowercase characters.
///
/// # Parameters
/// `hid_usage`: The USB HID code for a physical key.
fn hid_usage_to_codepoint(hid_usage: u32) -> Option<u32> {
    let hid_usage_size = hid_usage as usize;
    if hid_usage_size < QWERTY_MAP.len() {
        QWERTY_MAP[hid_usage_size].map(|entry| entry.0 as u32)
    } else {
        None
    }
}

/// Sends `keyboard_events` to the Scenic session.
///
/// This will be removed once Scenic no longer handles keyboard events.
///
/// # Parameters
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
        super::*, crate::keyboard, crate::testing_utilities, fidl_fuchsia_input as fidl_input,
        fidl_fuchsia_ui_input2 as fidl_ui_input2, fidl_fuchsia_ui_input3 as fidl_ui_input3,
        fidl_fuchsia_ui_scenic as fidl_ui_scenic, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt,
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

        fuchsia_async::Task::spawn(async move {
            loop {
                match ime_request_stream.next().await {
                    Some(Ok(fidl_ui_input::ImeServiceRequest::DispatchKey3 {
                        event: _,
                        responder,
                        ..
                    })) => {
                        // Respond that the event was not handled so that all events are
                        // dispatched to Scenic.
                        responder.send(false).expect("error responding to DispatchKey");
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        ImeHandler::new_handler(scenic_session, COMPOSITOR_ID, ime_proxy)
            .await
            .expect("Failed to create ImeHandler.")
    }

    /// Validates the first event in `cmds` against `expected_event`.
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
                            event_time,
                            device_id,
                            phase,
                            hid_usage,
                            code_point,
                            modifiers,
                        },
                },
            )) => {
                assert_eq!(event_time, expected_event.event_time);
                assert_eq!(device_id, expected_event.device_id);
                assert_eq!(phase, expected_event.phase);
                assert_eq!(hid_usage, expected_event.hid_usage);
                assert_eq!(code_point, expected_event.code_point);
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
                keys2: vec![fidl_ui_input2::Key::A],
                keys3: vec![fidl_input::Key::A],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![fidl_ui_input2::Key::A],
            vec![fidl_input::Key::A],
            vec![],
            vec![],
            None,
            None,
            event_time,
            &device_descriptor,
        )];

        let expected_commands = vec![fidl_ui_input::KeyboardEvent {
            event_time: event_time,
            device_id: 1,
            phase: fidl_ui_input::KeyboardEventPhase::Pressed,
            hid_usage: input3_key_to_hid_usage(fidl_input::Key::A),
            code_point: 'a' as u32,
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
                keys2: vec![fidl_ui_input2::Key::A],
                keys3: vec![fidl_input::Key::A],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_keyboard_event(
            vec![],
            vec![],
            vec![fidl_ui_input2::Key::A],
            vec![fidl_input::Key::A],
            None,
            None,
            event_time,
            &device_descriptor,
        )];

        let expected_commands = vec![fidl_ui_input::KeyboardEvent {
            event_time: event_time,
            device_id: 1,
            phase: fidl_ui_input::KeyboardEventPhase::Released,
            hid_usage: input3_key_to_hid_usage(fidl_input::Key::A),
            code_point: 'a' as u32,
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
                keys2: vec![fidl_ui_input2::Key::A, fidl_ui_input2::Key::B],
                keys3: vec![fidl_input::Key::A, fidl_input::Key::B],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_input::Key::A],
                vec![],
                vec![],
                None,
                None,
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::B],
                vec![fidl_input::Key::B],
                vec![fidl_ui_input2::Key::A],
                vec![fidl_input::Key::A],
                None,
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
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::A),
                code_point: 'a' as u32,
                modifiers: 0,
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::B),
                code_point: 'b' as u32,
                modifiers: 0,
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Released,
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::A),
                code_point: 'a' as u32,
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
                keys2: vec![fidl_ui_input2::Key::A, fidl_ui_input2::Key::CapsLock],
                keys3: vec![fidl_input::Key::A, fidl_input::Key::CapsLock],
            });
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events: Vec<input_device::InputEvent> = vec![
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::CapsLock],
                vec![fidl_input::Key::CapsLock],
                vec![],
                vec![],
                Some(fidl_ui_input2::Modifiers::CapsLock),
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![fidl_ui_input2::Key::A],
                vec![fidl_input::Key::A],
                vec![],
                vec![],
                Some(fidl_ui_input2::Modifiers::CapsLock),
                Some(fidl_ui_input3::Modifiers::CapsLock),
                event_time,
                &device_descriptor,
            ),
            testing_utilities::create_keyboard_event(
                vec![],
                vec![],
                vec![fidl_ui_input2::Key::CapsLock],
                vec![fidl_input::Key::CapsLock],
                None,
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
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::CapsLock),
                code_point: 0,
                modifiers: 1, // fidl_ui_input3::Modifiers::CapsLock
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Pressed,
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::A),
                code_point: 'a' as u32,
                modifiers: 1, // fidl_ui_input3::Modifiers::CapsLock
            },
            fidl_ui_input::KeyboardEvent {
                event_time: event_time,
                device_id: 1,
                phase: fidl_ui_input::KeyboardEventPhase::Released,
                hid_usage: input3_key_to_hid_usage(fidl_input::Key::CapsLock),
                code_point: 0,
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
