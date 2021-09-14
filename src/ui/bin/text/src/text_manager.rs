// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    core::convert::TryInto,
    fidl::endpoints::{ClientEnd, RequestStream, ServerEnd},
    fidl_fuchsia_ui_input as uii, fidl_fuchsia_ui_text as txt,
    fuchsia_syslog::fx_log_err,
    futures::{lock::Mutex, prelude::*},
    std::sync::{Arc, Weak},
};

use crate::{
    keyboard::events::KeyEvent,
    legacy_ime::{ImeState, LegacyIme},
    multiplex::TextFieldMultiplexer,
};

pub struct TextManagerState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<ImeState>>>,
    pub visibility_listeners: Vec<uii::ImeVisibilityServiceControlHandle>,
    pub multiplexer: Option<TextFieldMultiplexer>,

    /// `TextInputContext` is a service provided to input methods that want to edit text. Whenever
    /// a new text field is focused, we provide a TextField interface to any connected `TextInputContext`s,
    /// which are listed here.
    pub text_input_context_clients: Vec<txt::TextInputContextLegacyControlHandle>,
}

/// The internal state of the `TextManager`, usually held behind an Arc<Mutex>
/// so it can be accessed from multiple places.
impl TextManagerState {
    pub fn update_keyboard_visibility(&mut self, visible: bool) {
        self.keyboard_visible = visible;

        self.visibility_listeners.retain(|listener| {
            // drop listeners if they error on send
            listener.send_on_keyboard_visibility_changed(visible).is_ok()
        });
    }
}

/// Serves several public FIDL services: `ImeService`, `ImeVisibilityService`, and
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
                multiplexer: None,
                visibility_listeners: Vec::new(),
                text_input_context_clients: Vec::new(),
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
        let (txt_proxy, txt_request_stream) =
            match fidl::endpoints::create_proxy_and_stream::<txt::TextFieldLegacyMarker>() {
                Ok(v) => v,
                Err(e) => {
                    fx_log_err!("Failed to create TextField proxy and stream: {}", e);
                    return;
                }
            };
        state.active_ime = Some(ime.downgrade());
        ime.bind_ime(editor_stream);
        ime.bind_text_field(txt_request_stream);
        let multiplexer = TextFieldMultiplexer::new(txt_proxy);
        state.text_input_context_clients.retain(|listener| {
            // drop listeners if they error on send
            bind_new_text_field(&multiplexer, &listener).is_ok()
        });
        state.multiplexer = Some(multiplexer);
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
        let mut state = self.state.lock().await;
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

        // Send the key event to any listening `TextInputContext` clients. If at least one still
        // exists, we assume it handled it and converted it into an edit sent via its handle to the
        // `TextField` protocol.
        state.text_input_context_clients.retain(|listener| {
            // drop listeners if they error on send
            listener.send_on_key3_event(event.clone().try_into().unwrap()).is_ok()
        });

        // If no `TextInputContext` clients handled the input event, or if there are none connected,
        // we allow the internal input method inside of `LegacyIme` to convert this key event into
        // an edit.
        if state.text_input_context_clients.len() == 0 {
            ime.inject_input(event).await?;
        }
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

    pub fn bind_ime_visibility_service(&self, stream: uii::ImeVisibilityServiceRequestStream) {
        let self_clone = self.clone();
        fuchsia_async::Task::spawn(
            async move {
                let control_handle = stream.control_handle();
                let mut state = self_clone.state.lock().await;
                if control_handle
                    .send_on_keyboard_visibility_changed(state.keyboard_visible)
                    .is_ok()
                {
                    state.visibility_listeners.push(control_handle);
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        )
        .detach();
    }

    pub fn bind_text_input_context(&self, mut stream: txt::TextInputContextLegacyRequestStream) {
        let self_clone = self.clone();
        fuchsia_async::Task::spawn(
            async move {
                let control_handle = stream.control_handle();
                {
                    let mut state = self_clone.state.lock().await;

                    if let Some(multiplexer) = &state.multiplexer {
                        if let Err(e) = bind_new_text_field(multiplexer, &control_handle) {
                            fx_log_err!("Error when binding text field for newly connected TextInputContext: {}", e);
                        }
                    }
                    state.text_input_context_clients.push(control_handle)
                }
                while let Some(msg) = stream.try_next().await
                    .context("error reading value from text input context request stream")?
                {
                    match msg {
                        txt::TextInputContextLegacyRequest::HideKeyboard { .. } => {
                            self_clone.hide_keyboard().await;
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        ).detach();
    }
}

pub fn bind_new_text_field(
    multiplexer: &TextFieldMultiplexer,
    control_handle: &txt::TextInputContextLegacyControlHandle,
) -> Result<(), fidl::Error> {
    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream::<txt::TextFieldLegacyMarker>()
            .expect("Failed to create text field request stream");
    multiplexer.add_request_stream(request_stream);
    control_handle.send_on_focus(client_end)
}
