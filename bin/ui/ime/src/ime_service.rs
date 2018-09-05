// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ime::IME;
use fidl::endpoints2::{ClientEnd, ServerEnd};
use fidl_fuchsia_ui_input as uii;
use futures::future;
use parking_lot::Mutex;
use std::sync::{Arc, Weak};

pub struct ImeServiceState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<IME>>>,
    pub visibility_listeners: Vec<uii::ImeVisibilityServiceControlHandle>,
}

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

    pub fn update_keyboard_visibility(&self, visible: bool) {
        let mut state = self.0.lock();
        state.keyboard_visible = visible;

        state.visibility_listeners.retain(|listener| {
            // drop listeners if they error on send
            listener
                .send_on_keyboard_visibility_changed(visible)
                .is_ok()
        });
    }
}

impl uii::ImeService for ImeService {
    type OnOpenFut = future::Ready<()>;
    fn on_open(&mut self, _control_handle: uii::ImeServiceControlHandle) -> Self::OnOpenFut {
        future::ready(())
    }

    type GetInputMethodEditorFut = future::Ready<()>;
    fn get_input_method_editor(
        &mut self, keyboard_type: uii::KeyboardType, action: uii::InputMethodAction,
        initial_state: uii::TextInputState, client: ClientEnd<uii::InputMethodEditorClientMarker>,
        editor: ServerEnd<uii::InputMethodEditorMarker>,
        _control_handle: uii::ImeServiceControlHandle,
    ) -> Self::GetInputMethodEditorFut {
        if let Ok(edit_stream) = editor.into_stream() {
            if let Ok(client_proxy) = client.into_proxy() {
                let ime_ref = Arc::downgrade(
                    &IME::new(keyboard_type, action, initial_state, client_proxy).bind(edit_stream),
                );
                let mut state = self.0.lock();
                state.active_ime = Some(ime_ref);
            }
        }
        future::ready(())
    }

    type ShowKeyboardFut = future::Ready<()>;
    fn show_keyboard(
        &mut self, _control_handle: uii::ImeServiceControlHandle,
    ) -> Self::ShowKeyboardFut {
        self.update_keyboard_visibility(true);
        future::ready(())
    }

    type HideKeyboardFut = future::Ready<()>;
    fn hide_keyboard(
        &mut self, _control_handle: uii::ImeServiceControlHandle,
    ) -> Self::HideKeyboardFut {
        self.update_keyboard_visibility(false);
        future::ready(())
    }

    type InjectInputFut = future::Ready<()>;
    fn inject_input(
        &mut self, event: uii::InputEvent, _control_handle: uii::ImeServiceControlHandle,
    ) -> Self::InjectInputFut {
        let ime = {
            let state = self.0.lock();
            let active_ime_weak = match state.active_ime {
                Some(ref v) => v,
                None => return future::ready(()), // no currently active IME
            };
            let active_ime = match active_ime_weak.upgrade() {
                Some(v) => v,
                None => return future::ready(()), // IME no longer exists
            };
            active_ime
        };
        ime.lock().inject_input(event);
        future::ready(())
    }
}

impl uii::ImeVisibilityService for ImeService {
    type OnOpenFut = future::Ready<()>;
    fn on_open(
        &mut self, control_handle: uii::ImeVisibilityServiceControlHandle,
    ) -> Self::OnOpenFut {
        let mut state = self.0.lock();

        // send the current state on first connect, even if it hasn't changed
        if control_handle
            .send_on_keyboard_visibility_changed(state.keyboard_visible)
            .is_ok()
        {
            state.visibility_listeners.push(control_handle);
        }

        future::ready(())
    }
}
