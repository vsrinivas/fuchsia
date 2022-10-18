// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device, crate::input_handler::UnhandledInputHandler, anyhow::Error,
    async_trait::async_trait, fidl_fuchsia_ui_input3::KeyEvent,
    fidl_fuchsia_ui_shortcut as ui_shortcut, std::rc::Rc,
};

pub struct ShortcutHandler {
    /// The proxy to the Shortcut manager service.
    manager: ui_shortcut::ManagerProxy,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for ShortcutHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(ref keyboard_event),
                device_descriptor: input_device::InputDeviceDescriptor::Keyboard(_),
                event_time,
                trace_id: _,
            } => {
                // If either pressed_keys or released_keys triggered a shortcut, consume the event
                let handled = handle_key_event(
                    keyboard_event.from_key_event_at_time(event_time),
                    &self.manager,
                )
                .await;
                vec![input_device::InputEvent::from(unhandled_input_event).into_handled_if(handled)]
            }
            _ => vec![input_device::InputEvent::from(unhandled_input_event)],
        }
    }
}

impl ShortcutHandler {
    /// Creates a new [`ShortcutHandler`] and connects Keyboard and Shortcut services.
    pub fn new(shortcut_manager_proxy: ui_shortcut::ManagerProxy) -> Result<Rc<Self>, Error> {
        let handler = ShortcutHandler { manager: shortcut_manager_proxy };
        Ok(Rc::new(handler))
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
        super::*, crate::keyboard_binding, crate::testing_utilities,
        assert_matches::assert_matches, fidl_fuchsia_ui_input3 as fidl_ui_input3,
        fuchsia_async as fasync, fuchsia_zircon as zx, futures::StreamExt,
        pretty_assertions::assert_eq,
    };

    /// Creates an [`ShortcutHandler`] for tests.
    fn create_shortcut_handler(
        key_event_consumed_response: bool,
        _key2_event_consumed_response: bool,
    ) -> Rc<ShortcutHandler> {
        let (shortcut_manager_proxy, mut shortcut_manager_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ui_shortcut::ManagerMarker>()
                .expect("Failed to create ShortcutManagerProxy and stream");

        fuchsia_async::Task::local(async move {
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

    fn create_unhandled_keyboard_event(
        key3: fidl_fuchsia_input::Key,
        event_type: fidl_fuchsia_ui_input3::KeyEventType,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: zx::Time,
    ) -> input_device::UnhandledInputEvent {
        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor { keys: vec![key3], ..Default::default() },
        );
        let keyboard_event =
            keyboard_binding::KeyboardEvent::new(key3, event_type).into_with_modifiers(modifiers);
        input_device::UnhandledInputEvent {
            device_event: input_device::InputDeviceEvent::Keyboard(keyboard_event),
            device_descriptor,
            event_time,
            trace_id: None,
        }
    }

    /// Sends a pressed key event to the ShortcutHandler.
    async fn press_key(
        pressed_key3: fidl_fuchsia_input::Key,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: zx::Time,
        shortcut_handler: Rc<ShortcutHandler>,
    ) -> Vec<input_device::InputEvent> {
        let input_event = create_unhandled_keyboard_event(
            pressed_key3,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            modifiers,
            event_time,
        );
        shortcut_handler.handle_unhandled_input_event(input_event).await
    }

    /// Sends a release key event to the ShortcutHandler.
    async fn release_key(
        released_key3: fidl_fuchsia_input::Key,
        modifiers: Option<fidl_ui_input3::Modifiers>,
        event_time: zx::Time,
        shortcut_handler: Rc<ShortcutHandler>,
    ) -> Vec<input_device::InputEvent> {
        let input_event = create_unhandled_keyboard_event(
            released_key3,
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            modifiers,
            event_time,
        );
        shortcut_handler.handle_unhandled_input_event(input_event).await
    }

    /// Tests that a press key event is not consumed if it is not a shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_no_shortcut() {
        let shortcut_handler = create_shortcut_handler(false, false);
        let modifiers = None;
        let key3 = fidl_fuchsia_input::Key::A;
        let event_time = zx::Time::get_monotonic();

        let handle_result = press_key(key3, modifiers, event_time, shortcut_handler).await;
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::No, .. }]
        );

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor { keys: vec![key3], ..Default::default() },
        );
        let input_event = testing_utilities::create_keyboard_event(
            key3,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );

        assert_eq!(handle_result[0], input_event);
    }

    /// Tests that a press key shortcut is consumed.
    #[fasync::run_singlethreaded(test)]
    async fn press_key_activates_shortcut() {
        let event_time = zx::Time::get_monotonic();
        let shortcut_handler = create_shortcut_handler(true, false);
        let handle_result = press_key(
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input3::Modifiers::CAPS_LOCK),
            event_time,
            shortcut_handler,
        )
        .await;
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
        );
    }

    /// Tests that a release key event is not consumed if it is not a shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn release_key_no_shortcut() {
        let shortcut_handler = create_shortcut_handler(false, false);
        let key3 = fidl_fuchsia_input::Key::A;
        let modifiers = None;
        let event_time = zx::Time::get_monotonic();

        let handle_result = release_key(key3, modifiers, event_time, shortcut_handler).await;
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::No, .. }]
        );

        let device_descriptor = input_device::InputDeviceDescriptor::Keyboard(
            keyboard_binding::KeyboardDeviceDescriptor { keys: vec![key3], ..Default::default() },
        );
        let input_event = testing_utilities::create_keyboard_event(
            key3,
            fidl_fuchsia_ui_input3::KeyEventType::Released,
            modifiers,
            event_time,
            &device_descriptor,
            /* keymap= */ None,
        );

        assert_eq!(handle_result[0], input_event);
    }

    /// Tests that a release key event triggers a registered shortcut.
    #[fasync::run_singlethreaded(test)]
    async fn release_key_triggers_shortcut() {
        let shortcut_handler = create_shortcut_handler(true, false);
        let event_time = zx::Time::get_monotonic();

        let handle_result = release_key(
            fidl_fuchsia_input::Key::CapsLock,
            Some(fidl_ui_input3::Modifiers::CAPS_LOCK),
            event_time,
            shortcut_handler,
        )
        .await;
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
        );
    }
}
