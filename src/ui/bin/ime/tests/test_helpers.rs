// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
// Not all helper methods are used in each test.
#![allow(dead_code)]

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3,
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
) -> Result<(), Error> {
    let mut state = default_state();
    state.text = text.to_string();
    state.selection.base = base;
    state.selection.extent = extent;

    ime.set_state(&mut state).map_err(Into::into)
}

// Bind a new IME to the service.
pub fn bind_editor(
    ime_service: &ui_input::ImeServiceProxy,
) -> Result<(ui_input::InputMethodEditorProxy, ui_input::InputMethodEditorClientRequestStream), Error>
{
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

// Simulate keypress by injecting into IME service.
pub async fn simulate_keypress(
    ime_service: &ui_input::ImeServiceProxy,
    key: input::Key,
) -> Result<(), Error> {
    ime_service
        .dispatch_key3(ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;
    ime_service
        .dispatch_key3(ui_input3::KeyEvent {
            type_: Some(ui_input3::KeyEventType::Released),
            key: Some(key),
            ..ui_input3::KeyEvent::EMPTY
        })
        .await?;

    Ok(())
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
            ime.dispatch_key3(ui_input3::KeyEvent {
                type_: Some(type_),
                key: Some(key),
                ..ui_input3::KeyEvent::EMPTY
            })
            .map(|_| ())
        })
        .await;
}

// Get next IME message, assuming it's a `InputMethodEditorClientRequest::DidUpdateState`.
pub async fn get_state_update(
    editor_stream: &mut ui_input::InputMethodEditorClientRequestStream,
) -> Result<(ui_input::TextInputState, Option<ui_input::KeyboardEvent>), Error> {
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
) -> Result<ui_input::InputMethodAction, Error> {
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
