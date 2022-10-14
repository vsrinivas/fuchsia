// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::autorepeater;
use crate::input_device;
use crate::input_handler::UnhandledInputHandler;
use anyhow::{Context, Error, Result};
use async_trait::async_trait;
use fidl_fuchsia_input as finput;
use fidl_fuchsia_settings as fsettings;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_debug, fx_log_err};
use futures::{TryFutureExt, TryStreamExt};
use std::cell::RefCell;
use std::rc::Rc;

use async_utils::hanging_get::client::HangingGetStream;

/// The text settings handler instance. Refer to as `text_settings_handler::TextSettingsHandler`.
/// Its task is to decorate an input event with the keymap identifier.  The instance can
/// be freely cloned, each clone is thread-safely sharing data with others.
#[derive(Debug)]
pub struct TextSettingsHandler {
    /// Stores the currently active keymap identifier, if present.  Wrapped
    /// in an refcell as it can be changed out of band through
    /// `fuchsia.input.keymap.Configuration/SetLayout`.
    keymap_id: RefCell<Option<finput::KeymapId>>,

    /// Stores the currently active autorepeat settings.
    autorepeat_settings: RefCell<Option<autorepeater::Settings>>,
}

#[async_trait(?Send)]
impl UnhandledInputHandler for TextSettingsHandler {
    async fn handle_unhandled_input_event(
        self: Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent> {
        match unhandled_input_event {
            input_device::UnhandledInputEvent {
                device_event: input_device::InputDeviceEvent::Keyboard(mut event),
                device_descriptor,
                event_time,
                trace_id: _,
            } => {
                let keymap_id = self.get_keymap_name();
                fx_log_debug!(
                    "text_settings_handler::Instance::handle_unhandled_input_event: keymap_id = {:?}",
                    &keymap_id
                );
                event = event
                    .into_with_keymap(keymap_id)
                    .into_with_autorepeat_settings(self.get_autorepeat_settings());
                vec![input_device::InputEvent {
                    device_event: input_device::InputDeviceEvent::Keyboard(event),
                    device_descriptor,
                    event_time,
                    handled: input_device::Handled::No,
                    trace_id: None,
                }]
            }
            // Pass a non-keyboard event through.
            _ => vec![input_device::InputEvent::from(unhandled_input_event)],
        }
    }
}

impl TextSettingsHandler {
    /// Creates a new text settings handler instance.
    ///
    /// `initial_*` contain the desired initial values to be served.  Usually
    /// you want the defaults.
    pub fn new(
        initial_keymap: Option<finput::KeymapId>,
        initial_autorepeat: Option<autorepeater::Settings>,
    ) -> Rc<Self> {
        Rc::new(Self {
            keymap_id: RefCell::new(initial_keymap),
            autorepeat_settings: RefCell::new(initial_autorepeat),
        })
    }

    /// Processes requests for keymap change from `stream`.
    pub async fn process_keymap_configuration_from(
        self: &Rc<Self>,
        proxy: fsettings::KeyboardProxy,
    ) -> Result<(), Error> {
        let mut stream = HangingGetStream::new(proxy, fsettings::KeyboardProxy::watch);
        loop {
            match stream
                .try_next()
                .await
                .context("while waiting on fuchsia.settings.Keyboard/Watch")?
            {
                Some(fsettings::KeyboardSettings { keymap, autorepeat, .. }) => {
                    self.set_keymap_id(keymap);
                    self.set_autorepeat_settings(autorepeat.map(|e| e.into()));
                    fx_log_debug!("keymap ID set to: {:?}", self.get_keymap_id());
                }
                e => {
                    fx_log_err!("exiting - unexpected response: {:?}", &e);
                    break;
                }
            }
        }
        Ok(())
    }

    /// Starts reading events from the stream.  Does not block.
    pub fn serve(self: Rc<Self>, proxy: fsettings::KeyboardProxy) {
        fasync::Task::local(
            async move { self.process_keymap_configuration_from(proxy).await }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("can't run: {:?}", e)),
        )
        .detach();
    }

    fn set_keymap_id(self: &Rc<Self>, keymap_id: Option<finput::KeymapId>) {
        *(self.keymap_id.borrow_mut()) = keymap_id;
    }

    fn set_autorepeat_settings(self: &Rc<Self>, autorepeat: Option<autorepeater::Settings>) {
        *(self.autorepeat_settings.borrow_mut()) = autorepeat.map(|s| s.into());
    }

    /// Gets the currently active keymap ID.
    pub fn get_keymap_id(&self) -> Option<finput::KeymapId> {
        self.keymap_id.borrow().clone()
    }

    /// Gets the currently active autorepeat settings.
    pub fn get_autorepeat_settings(&self) -> Option<autorepeater::Settings> {
        self.autorepeat_settings.borrow().clone()
    }

    fn get_keymap_name(&self) -> Option<String> {
        // Maybe instead just pass in the keymap ID directly?
        match *self.keymap_id.borrow() {
            Some(id) => match id {
                finput::KeymapId::FrAzerty => Some("FR_AZERTY".to_owned()),
                finput::KeymapId::UsDvorak => Some("US_DVORAK".to_owned()),
                finput::KeymapId::UsColemak => Some("US_COLEMAK".to_owned()),
                finput::KeymapId::UsQwerty | finput::KeymapIdUnknown!() => {
                    Some("US_QWERTY".to_owned())
                }
            },
            None => Some("US_QWERTY".to_owned()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::input_device;
    use crate::keyboard_binding;
    use crate::testing_utilities;
    use fidl_fuchsia_input;
    use fidl_fuchsia_ui_input3;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use pretty_assertions::assert_eq;
    use std::convert::TryFrom as _;

    fn input_event_from(
        keyboard_event: keyboard_binding::KeyboardEvent,
    ) -> input_device::InputEvent {
        testing_utilities::create_input_event(
            keyboard_event,
            &input_device::InputDeviceDescriptor::Fake,
            zx::Time::from_nanos(42),
            input_device::Handled::No,
        )
    }

    fn key_event_with_settings(
        keymap: Option<String>,
        settings: autorepeater::Settings,
    ) -> input_device::InputEvent {
        let keyboard_event = keyboard_binding::KeyboardEvent::new(
            fidl_fuchsia_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
        )
        .into_with_keymap(keymap)
        .into_with_autorepeat_settings(Some(settings));
        input_event_from(keyboard_event)
    }

    fn key_event(keymap: Option<String>) -> input_device::InputEvent {
        let keyboard_event = keyboard_binding::KeyboardEvent::new(
            fidl_fuchsia_input::Key::A,
            fidl_fuchsia_ui_input3::KeyEventType::Pressed,
        )
        .into_with_keymap(keymap);
        input_event_from(keyboard_event)
    }

    fn unhandled_key_event() -> input_device::UnhandledInputEvent {
        input_device::UnhandledInputEvent::try_from(key_event(None)).unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn keymap_id_setting() {
        #[derive(Debug)]
        struct Test {
            keymap_id: Option<finput::KeymapId>,
            expected: Option<String>,
        }
        let tests = vec![
            Test { keymap_id: None, expected: Some("US_QWERTY".to_owned()) },
            Test {
                keymap_id: Some(finput::KeymapId::UsQwerty),
                expected: Some("US_QWERTY".to_owned()),
            },
            Test {
                keymap_id: Some(finput::KeymapId::FrAzerty),
                expected: Some("FR_AZERTY".to_owned()),
            },
            Test {
                keymap_id: Some(finput::KeymapId::UsDvorak),
                expected: Some("US_DVORAK".to_owned()),
            },
            Test {
                keymap_id: Some(finput::KeymapId::UsColemak),
                expected: Some("US_COLEMAK".to_owned()),
            },
        ];
        for test in tests {
            let handler = TextSettingsHandler::new(test.keymap_id.clone(), None);
            let expected = key_event(test.expected.clone());
            let result = handler.handle_unhandled_input_event(unhandled_key_event()).await;
            assert_eq!(vec![expected], result, "for: {:?}", &test);
        }
    }

    fn serve_into(
        mut server_end: fsettings::KeyboardRequestStream,
        keymap: Option<finput::KeymapId>,
        autorepeat: Option<fsettings::Autorepeat>,
    ) {
        fasync::Task::local(async move {
            if let Ok(Some(fsettings::KeyboardRequest::Watch { responder, .. })) =
                server_end.try_next().await
            {
                let settings = fsettings::KeyboardSettings {
                    keymap,
                    autorepeat,
                    ..fsettings::KeyboardSettings::EMPTY
                };
                responder.send(settings).expect("response sent");
            }
        })
        .detach();
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_call_processing() {
        let handler = TextSettingsHandler::new(None, None);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fsettings::KeyboardMarker>().unwrap();

        // Serve a specific keyboard setting.
        serve_into(
            stream,
            Some(finput::KeymapId::FrAzerty),
            Some(fsettings::Autorepeat { delay: 43, period: 44 }),
        );

        // Start an asynchronous handler that processes keymap configuration calls
        // incoming from `server_end`.
        handler.clone().serve(proxy);

        // Setting the keymap with a hanging get that does not synchronize with the "main"
        // task of the handler inherently races with `handle_input_event`.  So, the only
        // way to test it correctly is to verify that we get a changed setting *eventually*
        // after asking the server to hand out the modified settings.  So, we loop with an
        // expectation that at some point the settings get applied.  To avoid a long timeout
        // we quit the loop if nothing happened after a generous amount of time.
        let deadline = fuchsia_async::Time::after(zx::Duration::from_seconds(5));
        let autorepeat: autorepeater::Settings = Default::default();
        loop {
            let result = handler.clone().handle_unhandled_input_event(unhandled_key_event()).await;
            let expected = key_event_with_settings(
                Some("FR_AZERTY".to_owned()),
                autorepeat
                    .clone()
                    .into_with_delay(zx::Duration::from_nanos(43))
                    .into_with_period(zx::Duration::from_nanos(44)),
            );
            if vec![expected] == result {
                break;
            }
            fuchsia_async::Timer::new(fuchsia_async::Time::after(zx::Duration::from_millis(10)))
                .await;
            let now = fuchsia_async::Time::now();
            assert!(now < deadline, "the settings did not get applied, was: {:?}", &result);
        }
    }
}
