// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_ui_input2 as fidl_ui_input2,
    fidl_fuchsia_ui_input3::{KeyEvent, KeyEventType, Modifiers},
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    std::convert::TryInto,
};

pub struct ShortcutHandler {
    /// The proxy to the Shortcut manager service.
    manager: ui_shortcut::ManagerProxy,
}

#[async_trait]
impl InputHandler for ShortcutHandler {
    async fn handle_input_event(
        &mut self,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match &input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(keyboard_device_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Keyboard(_keyboard_device_descriptor),
                event_time,
            } => {
                let pressed_keys: Vec<(KeyEvent, fidl_ui_input2::KeyEvent)> = keyboard_device_event
                    .get_keys3(KeyEventType::Pressed)
                    .into_iter()
                    .map(|key| {
                        create_key_event(
                            &key,
                            KeyEventType::Pressed,
                            keyboard_device_event.modifiers3,
                            *event_time,
                        )
                    })
                    .zip(
                        keyboard_device_event
                            .get_keys2(fidl_ui_input2::KeyEventPhase::Pressed)
                            .into_iter()
                            .map(|key2| {
                                create_key2_event(
                                    &key2,
                                    fidl_ui_input2::KeyEventPhase::Pressed,
                                    keyboard_device_event.modifiers2,
                                )
                            })
                            .into_iter(),
                    )
                    .collect();
                let mut handled = self.handle_keys(pressed_keys).await;

                let released_keys: Vec<(KeyEvent, fidl_ui_input2::KeyEvent)> =
                    keyboard_device_event
                        .get_keys3(KeyEventType::Released)
                        .into_iter()
                        .map(|key| {
                            create_key_event(
                                &key,
                                KeyEventType::Released,
                                keyboard_device_event.modifiers3,
                                *event_time,
                            )
                        })
                        .zip(
                            keyboard_device_event
                                .get_keys2(fidl_ui_input2::KeyEventPhase::Released)
                                .into_iter()
                                .map(|key2| {
                                    create_key2_event(
                                        &key2,
                                        fidl_ui_input2::KeyEventPhase::Released,
                                        keyboard_device_event.modifiers2,
                                    )
                                })
                                .into_iter(),
                        )
                        .collect();
                handled = handled || self.handle_keys(released_keys).await;

                // If either pressed_keys or released_keys
                // triggered a shortcut, consume the event
                if handled {
                    return vec![];
                }
            }
            _ => return vec![input_event],
        }
        vec![input_event]
    }
}

impl ShortcutHandler {
    /// Creates a new [`ShortcutHandler`] and connects Keyboard and Shortcut services.
    pub fn new(shortcut_manager_proxy: ui_shortcut::ManagerProxy) -> Result<Self, Error> {
        let handler = ShortcutHandler { manager: shortcut_manager_proxy };
        Ok(handler)
    }

    /// Handle key events in Shortcut.
    ///
    /// # Parameters
    /// `keys`: The KeyEvents to handle.
    ///
    /// # Returns
    /// A bool that's true if any of the `keys` activated a shortcut.
    async fn handle_keys(&mut self, keys: Vec<(KeyEvent, fidl_ui_input2::KeyEvent)>) -> bool {
        for (key, key2) in keys {
            if handle_key_event(key, key2, &self.manager).await {
                return true;
            }
        }
        false
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
    key: &Key,
    event_type: KeyEventType,
    modifiers: Option<Modifiers>,
    event_time: input_device::EventTime,
) -> KeyEvent {
    KeyEvent {
        timestamp: Some(event_time.try_into().unwrap_or_default()),
        type_: Some(event_type),
        key: Some(*key),
        modifiers,
        ..KeyEvent::EMPTY
    }
}

/// Returns an input2 KeyEvent with the given parameters.
///
/// # Parameters
/// `key`: The key associated with the KeyEvent.
/// `phase`: The phase of key, either pressed or released.
/// `modifiers`: The modifiers associated the KeyEvent.
fn create_key2_event(
    key: &fidl_ui_input2::Key,
    phase: fidl_ui_input2::KeyEventPhase,
    modifiers: Option<fidl_ui_input2::Modifiers>,
) -> fidl_ui_input2::KeyEvent {
    fidl_ui_input2::KeyEvent {
        key: Some(*key),
        phase: Some(phase),
        modifiers: modifiers,
        semantic_key: None,
        physical_key: Some(*key),
        ..fidl_ui_input2::KeyEvent::EMPTY
    }
}

/// Returns a KeyboardEvent after handling `key_event` in Shortcut.
///
/// # Parameters
/// `key_event`: The KeyEvent to handle by the Shortcut Manager.
/// `shortcut_manager`: The Shortcut Manager
async fn handle_key_event(
    key_event: KeyEvent,
    key2_event: fidl_ui_input2::KeyEvent,
    shortcut_manager: &ui_shortcut::ManagerProxy,
) -> bool {
    match shortcut_manager.handle_key3_event(key_event).await {
        Ok(true) => return true,
        Ok(false) | Err(_) => {}
    };
    match shortcut_manager.handle_key_event(key2_event).await {
        Ok(true) => true,
        Ok(false) | Err(_) => false,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::keyboard, crate::testing_utilities,
        fidl_fuchsia_ui_input2 as fidl_ui_input2, fidl_fuchsia_ui_input3 as fidl_ui_input3,
        fuchsia_async as fasync, fuchsia_zircon as zx, futures::StreamExt,
    };

    /// Creates an [`ShortcutHandler`] for tests.
    fn create_shortcut_handler(
        key_event_consumed_response: bool,
        key2_event_consumed_response: bool,
    ) -> ShortcutHandler {
        let (shortcut_manager_proxy, mut shortcut_manager_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_shortcut::ManagerMarker>()
                .expect("Failed to create ShortcutManagerProxy and stream");

        fuchsia_async::Task::spawn(async move {
            loop {
                match shortcut_manager_request_stream.next().await {
                    Some(Ok(ui_shortcut::ManagerRequest::HandleKey3Event {
                        event: _,
                        responder,
                        ..
                    })) => {
                        responder
                            .send(key_event_consumed_response)
                            .expect("error responding to HandleKeyEvent");
                    }
                    Some(Ok(ui_shortcut::ManagerRequest::HandleKeyEvent {
                        event: _,
                        responder,
                        ..
                    })) => {
                        responder
                            .send(key2_event_consumed_response)
                            .expect("error responding to HandleKeyEvent");
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        ShortcutHandler::new(shortcut_manager_proxy).expect("Failed to create ShortcutHandler.")
    }

    /// Sends a pressed key event to the ShortcutHandler.
    async fn press_key(
        pressed_key2: fidl_ui_input2::Key,
        pressed_key3: fidl_fuchsia_input::Key,
        modifiers2: Option<fidl_ui_input2::Modifiers>,
        modifiers3: Option<fidl_ui_input3::Modifiers>,
        event_time: input_device::EventTime,
        mut shortcut_handler: ShortcutHandler,
    ) -> Vec<input_device::InputEvent> {
        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys2: vec![pressed_key2],
                keys3: vec![pressed_key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            vec![pressed_key2],
            vec![pressed_key3],
            vec![],
            vec![],
            modifiers2,
            modifiers3,
            event_time,
            &device_descriptor,
        );
        shortcut_handler.handle_input_event(input_event).await
    }

    /// Sends a release key event to the ShortcutHandler.
    async fn release_key(
        released_key2: fidl_ui_input2::Key,
        released_key3: fidl_fuchsia_input::Key,
        modifiers2: Option<fidl_ui_input2::Modifiers>,
        modifiers3: Option<fidl_ui_input3::Modifiers>,
        event_time: input_device::EventTime,
        mut shortcut_handler: ShortcutHandler,
    ) -> Vec<input_device::InputEvent> {
        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys2: vec![released_key2],
                keys3: vec![released_key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            vec![],
            vec![],
            vec![released_key2],
            vec![released_key3],
            modifiers2,
            modifiers3,
            event_time,
            &device_descriptor,
        );
        shortcut_handler.handle_input_event(input_event).await
    }

    /// Tests that a press key event is not consumed if it is not a shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_no_shortcut() {
        let shortcut_handler = create_shortcut_handler(false, false);
        let modifiers2 = None;
        let modifiers3 = None;
        let key2 = fidl_ui_input2::Key::A;
        let key3 = fidl_fuchsia_input::Key::A;
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled =
            press_key(key2, key3, modifiers2, modifiers3, event_time, shortcut_handler).await;
        assert_eq!(was_handled.len(), 1);

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys2: vec![key2],
                keys3: vec![key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            vec![key2],
            vec![key3],
            vec![],
            vec![],
            modifiers2,
            modifiers3,
            event_time,
            &device_descriptor,
        );

        assert_eq!(input_event, was_handled[0]);
    }

    /// Tests that a press key shortcut is consumed.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_activates_shortcut() {
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let shortcut_handler = create_shortcut_handler(true, false);
        let was_handled = press_key(
            fidl_ui_input2::Key::CapsLock,
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input2::Modifiers::CapsLock),
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time,
            shortcut_handler,
        )
        .await;
        assert_eq!(was_handled.len(), 0);
    }

    /// Tests that a release key event is not consumed if it is not a shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn release_key_no_shortcut() {
        let shortcut_handler = create_shortcut_handler(false, false);
        let key2 = fidl_ui_input2::Key::A;
        let key3 = fidl_fuchsia_input::Key::A;
        let modifiers2 = None;
        let modifiers3 = None;
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled =
            release_key(key2, key3, modifiers2, modifiers3, event_time, shortcut_handler).await;
        assert_eq!(was_handled.len(), 1);

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys2: vec![key2],
                keys3: vec![key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            vec![],
            vec![],
            vec![key2],
            vec![key3],
            modifiers2,
            modifiers3,
            event_time,
            &device_descriptor,
        );

        assert_eq!(input_event, was_handled[0]);
    }

    /// Tests that a release key event triggers a registered shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn release_key_triggers_shortcut() {
        let shortcut_handler = create_shortcut_handler(true, false);
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled = release_key(
            fidl_ui_input2::Key::CapsLock,
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input2::Modifiers::CapsLock),
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time,
            shortcut_handler,
        )
        .await;

        assert_eq!(was_handled.len(), 0);
    }

    /// Tests that a key press is consumed by an input2 shortcut handler.
    #[fasync::run_singlethreaded(test)]
    async fn shortcut2_service_handles_events() {
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let shortcut_handler = create_shortcut_handler(false, true);
        let was_handled = press_key(
            fidl_ui_input2::Key::CapsLock,
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input2::Modifiers::CapsLock),
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time,
            shortcut_handler,
        )
        .await;
        assert_eq!(was_handled.len(), 0);
    }
}
