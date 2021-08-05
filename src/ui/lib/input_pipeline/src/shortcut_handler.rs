// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_ui_input3::{KeyEvent, KeyEventType, Modifiers},
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    std::convert::TryInto,
    std::rc::Rc,
};

pub struct ShortcutHandler {
    /// The proxy to the Shortcut manager service.
    manager: ui_shortcut::ManagerProxy,
}

#[async_trait(?Send)]
impl InputHandler for ShortcutHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match &input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(keyboard_device_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Keyboard(_keyboard_device_descriptor),
                event_time,
            } => {
                let key_event = create_key_event(
                    &keyboard_device_event.key,
                    keyboard_device_event.event_type,
                    keyboard_device_event.modifiers,
                    *event_time,
                );
                // If either pressed_keys or released_keys
                // triggered a shortcut, consume the event
                if handle_key_event(key_event, &self.manager).await {
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
    pub fn new(shortcut_manager_proxy: ui_shortcut::ManagerProxy) -> Result<Rc<Self>, Error> {
        let handler = ShortcutHandler { manager: shortcut_manager_proxy };
        Ok(Rc::new(handler))
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

/// Returns true if the Shortcut Manager handles the `key_event`.
///
/// # Parameters
/// `key_event`: The KeyEvent to handle by the Shortcut Manager.
/// `shortcut_manager`: The Shortcut Manager
async fn handle_key_event(
    key_event: KeyEvent,
    shortcut_manager: &ui_shortcut::ManagerProxy,
) -> bool {
    match shortcut_manager.handle_key3_event(key_event).await {
        Ok(true) => true,
        Ok(false) | Err(_) => false,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::keyboard, crate::testing_utilities,
        fidl_fuchsia_ui_input3 as fidl_ui_input3, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt,
    };

    /// Creates an [`ShortcutHandler`] for tests.
    fn create_shortcut_handler(
        key_event_consumed_response: bool,
        _key2_event_consumed_response: bool,
    ) -> Rc<ShortcutHandler> {
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
                    _ => assert!(false),
                }
            }
        })
        .detach();

        ShortcutHandler::new(shortcut_manager_proxy).expect("Failed to create ShortcutHandler.")
    }

    /// Sends a pressed key event to the ShortcutHandler.
    async fn press_key(
        pressed_key3: fidl_fuchsia_input::Key,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: input_device::EventTime,
        shortcut_handler: Rc<ShortcutHandler>,
    ) -> Vec<input_device::InputEvent> {
        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![pressed_key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            pressed_key3,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );
        shortcut_handler.handle_input_event(input_event).await
    }

    /// Sends a release key event to the ShortcutHandler.
    async fn release_key(
        released_key3: fidl_fuchsia_input::Key,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: input_device::EventTime,
        shortcut_handler: Rc<ShortcutHandler>,
    ) -> Vec<input_device::InputEvent> {
        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![released_key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            released_key3,
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );
        shortcut_handler.handle_input_event(input_event).await
    }

    /// Tests that a press key event is not consumed if it is not a shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_no_shortcut() {
        let shortcut_handler = create_shortcut_handler(false, false);
        let modifiers = None;
        let key3 = fidl_fuchsia_input::Key::A;
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled = press_key(key3, modifiers, event_time, shortcut_handler).await;
        assert_eq!(was_handled.len(), 1);

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            key3,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );

        assert_eq!(input_event, was_handled[0]);
    }

    /// Tests that a press key shortcut is consumed.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_activates_shortcut() {
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let shortcut_handler = create_shortcut_handler(true, false);
        let was_handled = press_key(
            fidl_fuchsia_input::Key::CapsLock,
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
        let key3 = fidl_fuchsia_input::Key::A;
        let modifiers = None;
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled = release_key(key3, modifiers, event_time, shortcut_handler).await;
        assert_eq!(was_handled.len(), 1);

        let device_descriptor =
            input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
                keys: vec![key3],
            });
        let input_event = testing_utilities::create_keyboard_event(
            key3,
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );

        assert_eq!(input_event, was_handled[0]);
    }

    /// Tests that a release key event triggers a registered shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn release_key_triggers_shortcut() {
        let shortcut_handler = create_shortcut_handler(true, false);
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;

        let was_handled = release_key(
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input3::Modifiers::CapsLock),
            event_time,
            shortcut_handler,
        )
        .await;

        assert_eq!(was_handled.len(), 0);
    }
}
