// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use {
    anyhow::{format_err, Result},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3, fuchsia_zircon as zx,
    futures::{
        stream::{self, StreamExt, TryStreamExt},
        FutureExt,
    },
};

pub fn default_state() -> ui_input::TextInputState {
    ui_input::TextInputState {
        revision: 1,
        text: "".to_string(),
        selection: ui_input::TextSelection {
            base: 0,
            extent: 0,
            affinity: ui_input::TextAffinity::Upstream,
        },
        composing: ui_input::TextRange { start: -1, end: -1 },
    }
}

// Measure text in utf16 code units.
pub fn measure_utf16(s: &str) -> usize {
    s.chars().map(|c| c.len_utf16()).sum::<usize>()
}

// Setup IME text edit state using contents and selection indices.
pub async fn setup_ime(
    ime: &ui_input::InputMethodEditorProxy,
    text: &str,
    base: i64,
    extent: i64,
) -> Result<()> {
    let mut state = default_state();
    state.text = text.to_string();
    state.selection.base = base;
    state.selection.extent = extent;

    ime.set_state(&mut state).map_err(Into::into)
}

// Bind a new IME to the service.
pub fn bind_editor(
    ime_service: &ui_input::ImeServiceProxy,
) -> Result<(ui_input::InputMethodEditorProxy, ui_input::InputMethodEditorClientRequestStream)> {
    let (ime, ime_server_end) =
        fidl::endpoints::create_proxy::<ui_input::InputMethodEditorMarker>().unwrap();
    let (editor_client_end, editor_request_stream) =
        fidl::endpoints::create_request_stream().unwrap();
    ime_service.get_input_method_editor(
        ui_input::KeyboardType::Text,
        ui_input::InputMethodAction::Done,
        &mut default_state(),
        editor_client_end,
        ime_server_end,
    )?;

    Ok((ime, editor_request_stream))
}

/// Provides a method for dispatching keys, in case the tests need to vary
/// them.
#[async_trait]
pub trait KeyDispatcher {
    async fn dispatch(&self, event: ui_input3::KeyEvent) -> Result<bool>;
}

/// A [KeyDispatcher] that uses `fuchsia.ui.input.InputMethodEditor` to dispatch keypresses.
pub struct InputMethodEditorDispatcher<'a> {
    pub ime: &'a ui_input::InputMethodEditorProxy,
}

#[async_trait]
impl<'a> KeyDispatcher for InputMethodEditorDispatcher<'a> {
    async fn dispatch(&self, event: ui_input3::KeyEvent) -> Result<bool> {
        Ok(self.ime.dispatch_key3(event).await?)
    }
}

/// A [KeyDispatcher] that uses `fuchsia.ui.input3.KeyEventInjector` to dispatch keypresses.
pub struct KeyEventInjectorDispatcher<'a> {
    pub key_event_injector: &'a ui_input3::KeyEventInjectorProxy,
}

#[async_trait]
impl<'a> KeyDispatcher for KeyEventInjectorDispatcher<'a> {
    async fn dispatch(&self, event: ui_input3::KeyEvent) -> Result<bool> {
        Ok(self.key_event_injector.inject(event).await? == ui_input3::KeyEventStatus::Handled)
    }
}

/// A fixture that can use different services to send key events.  Use [KeySimulator::new] to
/// create a new instance.
///
/// # Example
///
/// ```ignore
/// let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
///     .context("Failed to connect to IME Service")?;
/// let key_dispatcher = test_helpers::ImeServiceKeyDispatcher { ime_service: &ime_service };
/// let key_simulator = test_helpers::KeySimulator::new(&key_dispatcher);
/// // Thereafter...
/// key_simulator.dispatch(fidl_fuchsia_ui_input3::KeyEvent{...}).await?;
/// ```
pub struct KeySimulator<'a> {
    dispatcher: &'a dyn KeyDispatcher,
}

impl<'a> KeySimulator<'a> {
    pub fn new(dispatcher: &'a dyn KeyDispatcher) -> Self {
        KeySimulator { dispatcher }
    }

    pub async fn dispatch(&self, event: ui_input3::KeyEvent) -> Result<bool> {
        self.dispatcher.dispatch(event).await
    }

    // Simulate a key press and release of `key`.
    async fn simulate_keypress(&self, key: input::Key) -> Result<()> {
        self.dispatch(ui_input3::KeyEvent {
            timestamp: Some(0),
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;
        self.dispatch(ui_input3::KeyEvent {
            timestamp: Some(0),
            type_: Some(ui_input3::KeyEventType::Released),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;
        Ok(())
    }

    // Simulate keypress with `held_keys` pressed down.
    async fn simulate_ime_keypress_with_held_keys(
        &self,
        key: input::Key,
        held_keys: Vec<input::Key>,
    ) {
        let held_keys_down =
            held_keys.iter().map(|k| (ui_input3::KeyEventType::Pressed, *k)).into_iter();
        let held_keys_up =
            held_keys.iter().map(|k| (ui_input3::KeyEventType::Released, *k)).into_iter();
        let key_press_and_release =
            vec![(ui_input3::KeyEventType::Pressed, key), (ui_input3::KeyEventType::Released, key)]
                .into_iter();
        let sequence = held_keys_down.chain(key_press_and_release).chain(held_keys_up);
        stream::iter(sequence)
            .for_each(|(type_, key)| {
                self.dispatch(ui_input3::KeyEvent {
                    timestamp: Some(0),
                    type_: Some(type_),
                    key: Some(key),
                    ..ui_input3::KeyEvent::EMPTY
                })
                .map(|_| ())
            })
            .await;
    }
}

// Simulate keypress by injecting an event into supplied key event injector.
pub async fn simulate_keypress(
    key_event_injector: &ui_input3::KeyEventInjectorProxy,
    key: input::Key,
) -> Result<()> {
    let key_dispatcher = KeyEventInjectorDispatcher { key_event_injector: &key_event_injector };
    let key_simulator = KeySimulator::new(&key_dispatcher);
    key_simulator.simulate_keypress(key).await
}

// Simulate keypress by injecting into IME.
pub async fn simulate_ime_keypress(ime: &ui_input::InputMethodEditorProxy, key: input::Key) {
    simulate_ime_keypress_with_held_keys(ime, key, Vec::new()).await
}

// Simulate keypress by injecting into IME, with `held_keys` pressed down.
pub async fn simulate_ime_keypress_with_held_keys(
    ime: &ui_input::InputMethodEditorProxy,
    key: input::Key,
    held_keys: Vec<input::Key>,
) {
    let key_dispatcher = InputMethodEditorDispatcher { ime: &ime };
    let key_simulator = KeySimulator::new(&key_dispatcher);
    key_simulator.simulate_ime_keypress_with_held_keys(key, held_keys).await
}

// Get next IME message, assuming it's a `InputMethodEditorClientRequest::DidUpdateState`.
pub async fn get_state_update(
    editor_stream: &mut ui_input::InputMethodEditorClientRequestStream,
) -> Result<(ui_input::TextInputState, Option<ui_input::KeyboardEvent>)> {
    editor_stream
        .map(|request| match request {
            Ok(ui_input::InputMethodEditorClientRequest::DidUpdateState {
                state, event, ..
            }) => {
                let keyboard_event = event.map(|e| {
                    if let ui_input::InputEvent::Keyboard(keyboard_event) = *e {
                        keyboard_event
                    } else {
                        panic!("expected DidUpdateState to only send Keyboard events");
                    }
                });
                Ok((state, keyboard_event))
            }
            Ok(msg) => Err(format_err!("request should be DidUpdateState, got {:?}", msg)),
            Err(err) => Err(Into::into(err)),
        })
        .try_next()
        .await
        .map(|maybe_msg| maybe_msg.ok_or(format_err!("ime should have sent message")))?
}

// Get next IME message, assuming it's a `InputMethodEditorClientRequest::OnAction`.
pub async fn get_action(
    editor_stream: &mut ui_input::InputMethodEditorClientRequestStream,
) -> Result<ui_input::InputMethodAction> {
    editor_stream
        .map(|request| match request {
            Ok(ui_input::InputMethodEditorClientRequest::OnAction { action, .. }) => Ok(action),
            Ok(msg) => Err(format_err!("request should be OnAction, got {:?}", msg)),
            Err(err) => Err(Into::into(err)),
        })
        .try_next()
        .await
        .map(|maybe_msg| maybe_msg.ok_or(format_err!("ime should have sent message")))?
}

/// Used to reduce verbosity of instantiating `KeyMeaning`s.
pub struct KeyMeaningWrapper(Option<ui_input3::KeyMeaning>);

impl From<ui_input3::KeyMeaning> for KeyMeaningWrapper {
    fn from(src: ui_input3::KeyMeaning) -> Self {
        KeyMeaningWrapper(src.into())
    }
}

impl From<Option<ui_input3::KeyMeaning>> for KeyMeaningWrapper {
    fn from(src: Option<ui_input3::KeyMeaning>) -> Self {
        KeyMeaningWrapper(src)
    }
}

impl From<KeyMeaningWrapper> for Option<ui_input3::KeyMeaning> {
    fn from(src: KeyMeaningWrapper) -> Self {
        src.0
    }
}

impl From<char> for KeyMeaningWrapper {
    fn from(src: char) -> Self {
        Some(ui_input3::KeyMeaning::Codepoint(src as u32)).into()
    }
}

impl From<ui_input3::NonPrintableKey> for KeyMeaningWrapper {
    fn from(src: ui_input3::NonPrintableKey) -> Self {
        Some(ui_input3::KeyMeaning::NonPrintableKey(src)).into()
    }
}

/// Creates a `KeyEvent` with the given parameters.
pub fn create_key_event(
    timestamp: zx::Time,
    event_type: ui_input3::KeyEventType,
    key: impl Into<Option<input::Key>>,
    modifiers: impl Into<Option<ui_input3::Modifiers>>,
    key_meaning: impl Into<KeyMeaningWrapper>,
) -> ui_input3::KeyEvent {
    let key_meaning: KeyMeaningWrapper = key_meaning.into();
    ui_input3::KeyEvent {
        timestamp: Some(timestamp.into_nanos()),
        type_: Some(event_type),
        key: key.into(),
        modifiers: modifiers.into(),
        key_meaning: key_meaning.into(),
        ..ui_input3::KeyEvent::EMPTY
    }
}
