// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ime::{Ime, ImeState};
use failure::ResultExt;
use fidl::endpoints::{ClientEnd, RequestStream, ServerEnd};
use fidl_fuchsia_ui_input as uii;
use fuchsia_syslog::fx_log_err;
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::{Arc, Weak};

pub struct ImeServiceState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<ImeState>>>,
    pub visibility_listeners: Vec<uii::ImeVisibilityServiceControlHandle>,
}

/// The internal state of the IMEService, usually held behind an Arc<Mutex>
/// so it can be accessed from multiple places.
impl ImeServiceState {
    pub fn update_keyboard_visibility(&mut self, visible: bool) {
        self.keyboard_visible = visible;

        self.visibility_listeners.retain(|listener| {
            // drop listeners if they error on send
            listener
                .send_on_keyboard_visibility_changed(visible)
                .is_ok()
        });
    }
}

/// The ImeService is a central, discoverable service responsible for creating new IMEs when a new
/// text field receives focus. It also advertises the ImeVisibilityService, which allows a client
/// (usually a soft keyboard container) to receive updates when the keyboard has been requested to
/// be shown or hidden.
#[derive(Clone)]
pub struct ImeService(Arc<Mutex<ImeServiceState>>);

impl ImeService {
    pub fn new() -> ImeService {
        ImeService(Arc::new(Mutex::new(ImeServiceState {
            keyboard_visible: false,
            active_ime: None,
            visibility_listeners: Vec::new(),
        })))
    }

    /// Only updates the keyboard visibility if IME passed in is active
    pub fn update_keyboard_visibility_from_ime(
        &self, check_ime: &Arc<Mutex<ImeState>>, visible: bool,
    ) {
        let mut state = self.0.lock();
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

    fn get_input_method_editor(
        &mut self, keyboard_type: uii::KeyboardType, action: uii::InputMethodAction,
        initial_state: uii::TextInputState, client: ClientEnd<uii::InputMethodEditorClientMarker>,
        editor: ServerEnd<uii::InputMethodEditorMarker>,
    ) {
        if let Ok(client_proxy) = client.into_proxy() {
            let ime = Ime::new(
                keyboard_type,
                action,
                initial_state,
                client_proxy,
                self.clone(),
            );
            let mut state = self.0.lock();
            state.active_ime = Some(ime.downgrade());
            if let Ok(chan) = fuchsia_async::Channel::from_channel(editor.into_channel()) {
                ime.bind_ime(chan);
            }
        }
    }

    pub fn show_keyboard(&self) {
        self.0.lock().update_keyboard_visibility(true);
    }

    pub fn hide_keyboard(&self) {
        self.0.lock().update_keyboard_visibility(false);
    }

    fn inject_input(&mut self, event: uii::InputEvent) {
        let ime = {
            let state = self.0.lock();
            let active_ime_weak = match state.active_ime {
                Some(ref v) => v,
                None => return, // no currently active IME
            };
            match Ime::upgrade(active_ime_weak) {
                Some(active_ime) => active_ime,
                None => return, // IME no longer exists
            }
        };
        ime.inject_input(event);
    }

    pub fn bind_ime_service(&self, chan: fuchsia_async::Channel) {
        let mut self_clone = self.clone();
        fuchsia_async::spawn(
            async move {
                let mut stream = uii::ImeServiceRequestStream::from_channel(chan);
                while let Some(msg) = await!(stream.try_next())
                    .context("error reading value from IME service request stream")?
                {
                    match msg {
                        uii::ImeServiceRequest::GetInputMethodEditor {
                            keyboard_type,
                            action,
                            initial_state,
                            client,
                            editor,
                            ..
                        } => {
                            self_clone.get_input_method_editor(
                                keyboard_type,
                                action,
                                initial_state,
                                client,
                                editor,
                            );
                        }
                        uii::ImeServiceRequest::ShowKeyboard { .. } => {
                            self_clone.show_keyboard();
                        }
                        uii::ImeServiceRequest::HideKeyboard { .. } => {
                            self_clone.hide_keyboard();
                        }
                        uii::ImeServiceRequest::InjectInput { event, .. } => {
                            self_clone.inject_input(event);
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
        );
    }

    pub fn bind_ime_visibility_service(&self, chan: fuchsia_async::Channel) {
        let self_clone = self.clone();
        fuchsia_async::spawn(
            async move {
                let stream = uii::ImeVisibilityServiceRequestStream::from_channel(chan);
                let control_handle = stream.control_handle();
                let mut state = self_clone.0.lock();
                if control_handle
                    .send_on_keyboard_visibility_changed(state.keyboard_visible)
                    .is_ok()
                {
                    state.visibility_listeners.push(control_handle);
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
        );
    }
}
