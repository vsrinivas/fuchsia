// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device;
use crate::input_handler::InputHandler;
use anyhow::{Context, Error, Result};
use async_trait::async_trait;
use fidl_fuchsia_input_keymap as fkeymap;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_debug, fx_log_err};
use futures::{TryFutureExt, TryStreamExt};
use std::cell::RefCell;
use std::rc::Rc;

/// The text settings handler instance. Refer to as `text_settings::Handler`.
/// Its task is to decorate an input event with the keymap identifier.  The instance can
/// be freely cloned, each clone is thread-safely sharing data with others.
#[derive(Debug)]
pub struct Handler {
    /// Stores the currently active keymap identifier, if present.  Wrapped
    /// in an refcell as it can be changed out of band through
    /// `fuchsia.input.keymap.Configuration/SetLayout`.
    keymap_id: RefCell<Option<fkeymap::Id>>,
}

#[async_trait(?Send)]
impl InputHandler for Handler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(mut event),
                device_descriptor,
                event_time,
            } => {
                // Maybe instead just pass in the keymap ID directly?
                let keymap_id = match *self.keymap_id.borrow() {
                    Some(ref id) => match id {
                        fkeymap::Id::FrAzerty => Some("FR_AZERTY".to_owned()),
                        fkeymap::Id::UsDvorak => Some("US_DVORAK".to_owned()),
                        fkeymap::Id::UsQwerty | fkeymap::IdUnknown!() => {
                            Some("US_QWERTY".to_owned())
                        }
                    },
                    None => Some("US_QWERTY".to_owned()),
                };
                fx_log_debug!(
                    "text_settings_handler::Instance::handle_input_event: keymap_id = {:?}",
                    &keymap_id
                );
                event.keymap = keymap_id;
                vec![input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Keyboard(event),
                    device_descriptor,
                    event_time,
                }]
            }
            // Pass a non-keyboard event through.
            _ => vec![input_event],
        }
    }
}

impl Handler {
    /// Creates a new text settings handler instance.
    /// `initial` contains the desired initial keymap value to be served.
    /// Usually you want this to be `None`.
    pub fn new(initial: Option<fkeymap::Id>) -> Rc<Self> {
        Rc::new(Handler { keymap_id: RefCell::new(initial) })
    }

    /// Processes requests for keymap change from `stream`.
    pub async fn process_keymap_configuration_from(
        self: &Rc<Self>,
        mut stream: fkeymap::ConfigurationRequestStream,
    ) -> Result<(), Error> {
        while let Some(fkeymap::ConfigurationRequest::SetLayout { keymap, responder, .. }) = stream
            .try_next()
            .await
            .context("while trying to serve fuchsia.input.keymap.Configuration")?
        {
            fx_log_debug!("keymap ID set to: {:?}", &keymap);
            let mut data = self.keymap_id.borrow_mut();
            *data = Some(keymap);
            responder.send()?;
        }
        Ok(())
    }

    /// Gets the currently active keymap ID.
    pub async fn get_keymap_id(&self) -> fkeymap::Id {
        self.keymap_id.borrow().unwrap_or(fkeymap::Id::UsQwerty)
    }

    /// Returns the function that can be used to serve
    /// `fuchsia.input.keymap.Configuration` from
    /// `fuchsia_component::server::ServiceFs::add_fidl_service`.
    pub fn get_serving_fn(
        self: Rc<Self>,
    ) -> Box<dyn FnMut(fkeymap::ConfigurationRequestStream) -> ()> {
        Box::new(move |stream| {
            let handler = self.clone();
            fasync::Task::local(
                async move { handler.process_keymap_configuration_from(stream).await }
                    .unwrap_or_else(|e: anyhow::Error| fx_log_err!("can't run: {:?}", e)),
            )
            .detach();
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::input_device;
    use crate::keyboard;
    use crate::testing_utilities;
    use fidl_fuchsia_input;
    use fidl_fuchsia_ui_input3;
    use fuchsia_async as fasync;

    fn get_fake_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Keyboard(keyboard::KeyboardDeviceDescriptor {
            keys: vec![],
        })
    }

    fn get_fake_key_event(keymap: Option<String>) -> input_device::InputEvent {
        let descriptor = get_fake_descriptor();
        testing_utilities::create_keyboard_event(
            fidl_fuchsia_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
            /* modifiers= */ None,
            42 as input_device::EventTime,
            &descriptor,
            keymap,
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn keymap_id_setting() {
        #[derive(Debug)]
        struct Test {
            keymap_id: Option<fkeymap::Id>,
            expected: Option<String>,
        }
        let tests = vec![
            Test { keymap_id: None, expected: Some("US_QWERTY".to_owned()) },
            Test { keymap_id: Some(fkeymap::Id::UsQwerty), expected: Some("US_QWERTY".to_owned()) },
            Test { keymap_id: Some(fkeymap::Id::FrAzerty), expected: Some("FR_AZERTY".to_owned()) },
            Test { keymap_id: Some(fkeymap::Id::UsDvorak), expected: Some("US_DVORAK".to_owned()) },
        ];
        for test in tests {
            let handler = Handler::new(test.keymap_id.clone());
            let expected = get_fake_key_event(test.expected.clone());
            let result = handler.handle_input_event(get_fake_key_event(None)).await;
            assert_eq!(vec![expected], result, "for: {:?}", &test);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_call_processing() {
        let handler = Handler::new(None);

        let (client_end, server_end) =
            fidl::endpoints::create_proxy_and_stream::<fkeymap::ConfigurationMarker>().unwrap();

        // Start an asynchronous handler that processes keymap configuration calls
        // incoming from `server_end`.
        handler.clone().get_serving_fn()(server_end);

        // Send one input event and verify that it is properly decorated.
        let result = handler.clone().handle_input_event(get_fake_key_event(None)).await;
        let expected = get_fake_key_event(Some("US_QWERTY".to_owned()));
        assert_eq!(vec![expected], result);

        // Now change the configuration, send another input event and verify
        // that a modified keymap has been attached to the event.
        client_end.set_layout(fkeymap::Id::FrAzerty).await.unwrap();
        let result = handler.handle_input_event(get_fake_key_event(None)).await;
        let expected = get_fake_key_event(Some("FR_AZERTY".to_owned()));
        assert_eq!(vec![expected], result);
    }
}
