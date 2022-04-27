// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_ui_input as uii,
    fuchsia_syslog::*,
    futures::lock::Mutex,
    std::sync::{Arc, Weak},
};

use crate::{
    keyboard::events::KeyEvent,
    legacy_ime::{ImeState, LegacyIme},
};

pub struct TextManagerState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<ImeState>>>,
}

/// The internal state of the `TextManager`, usually held behind an Arc<Mutex>
/// so it can be accessed from multiple places.
impl TextManagerState {
    pub fn update_keyboard_visibility(&mut self, visible: bool) {
        self.keyboard_visible = visible;
    }
}

/// Serves several public FIDL services: `ImeService` and
/// `TextInputContext`.
#[derive(Clone)]
pub struct TextManager {
    state: Arc<Mutex<TextManagerState>>,
}

impl TextManager {
    pub fn new() -> TextManager {
        TextManager {
            state: Arc::new(Mutex::new(TextManagerState {
                keyboard_visible: false,
                active_ime: None,
            })),
        }
    }

    /// Only updates the keyboard visibility if IME passed in is active
    pub async fn update_keyboard_visibility_from_ime<'a>(
        &'a self,
        check_ime: &'a Arc<Mutex<ImeState>>,
        visible: bool,
    ) {
        let mut state = self.state.lock().await;
        let active_ime_weak = match &state.active_ime {
            Some(val) => val,
            None => return,
        };
        let active_ime = match active_ime_weak.upgrade() {
            Some(val) => val,
            None => return,
        };
        if Arc::ptr_eq(check_ime, &active_ime) {
            state.update_keyboard_visibility(visible);
        }
    }

    pub async fn get_input_method_editor(
        &mut self,
        keyboard_type: uii::KeyboardType,
        action: uii::InputMethodAction,
        initial_state: uii::TextInputState,
        client: ClientEnd<uii::InputMethodEditorClientMarker>,
        editor: ServerEnd<uii::InputMethodEditorMarker>,
    ) {
        let client_proxy = match client.into_proxy() {
            Ok(v) => v,
            Err(_) => return,
        };
        let ime = LegacyIme::new(keyboard_type, action, initial_state, client_proxy, self.clone());
        let mut state = self.state.lock().await;
        let editor_stream = match editor.into_stream() {
            Ok(v) => v,
            Err(e) => {
                fx_log_err!("Failed to create stream: {}", e);
                return;
            }
        };
        state.active_ime = Some(ime.downgrade());
        ime.bind_ime(editor_stream);
    }

    pub async fn show_keyboard(&self) {
        self.state.lock().await.update_keyboard_visibility(true);
    }

    pub async fn hide_keyboard(&self) {
        self.state.lock().await.update_keyboard_visibility(false);
    }

    /// This is called by the operating system when input from the physical keyboard comes in.
    /// It also is called by legacy onscreen keyboards that just simulate physical keyboard input.
    pub(crate) async fn inject_input(&mut self, event: KeyEvent) -> Result<(), Error> {
        let state = self.state.lock().await;
        let ime = {
            let active_ime_weak = match state.active_ime {
                Some(ref v) => v,
                None => return Ok(()), // no currently active IME
            };
            match LegacyIme::upgrade(active_ime_weak) {
                Some(active_ime) => active_ime,
                None => return Ok(()), // IME no longer exists
            }
        };

        // Send the legacy ime a keystroke event to forward to connected clients. Even if a v2 input
        // method is connected, this ensures legacy text fields are able to still see key events;
        // something not yet provided by the new `TextField` API.
        ime.forward_event(&event).await?;

        // We allow the internal input method inside of `LegacyIme` to convert this key event into
        // an edit.
        ime.inject_input(event).await?;
        Ok(())
    }

    pub async fn handle_ime_service_msg(
        &mut self,
        msg: uii::ImeServiceRequest,
    ) -> Result<(), Error> {
        match msg {
            uii::ImeServiceRequest::GetInputMethodEditor {
                keyboard_type,
                action,
                initial_state,
                client,
                editor,
                ..
            } => {
                self.get_input_method_editor(keyboard_type, action, initial_state, client, editor)
                    .await;
            }
            uii::ImeServiceRequest::ShowKeyboard { .. } => {
                self.show_keyboard().await;
            }
            uii::ImeServiceRequest::HideKeyboard { .. } => {
                self.hide_keyboard().await;
            }
        }
        Ok(())
    }
}
