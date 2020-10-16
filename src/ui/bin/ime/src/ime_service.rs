// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_helpers::clone_keyboard_event;
use crate::legacy_ime::ImeState;
use crate::legacy_ime::LegacyIme;
use crate::multiplex::TextFieldMultiplexer;
use anyhow::{Context as _, Error};
use fidl::endpoints::{ClientEnd, RequestStream, ServerEnd};
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_text as txt;
use fuchsia_syslog::{fx_log_err, fx_vlog};
use futures::lock::Mutex;
use futures::prelude::*;
use std::sync::{Arc, Weak};

pub struct ImeServiceState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<ImeState>>>,
    pub visibility_listeners: Vec<uii::ImeVisibilityServiceControlHandle>,
    pub multiplexer: Option<TextFieldMultiplexer>,

    /// `TextInputContext` is a service provided to input methods that want to edit text. Whenever
    /// a new text field is focused, we provide a TextField interface to any connected `TextInputContext`s,
    /// which are listed here.
    pub text_input_context_clients: Vec<txt::TextInputContextControlHandle>,
}

/// The internal state of the IMEService, usually held behind an Arc<Mutex>
/// so it can be accessed from multiple places.
impl ImeServiceState {
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
pub struct ImeService {
    state: Arc<Mutex<ImeServiceState>>,
}

impl ImeService {
    pub fn new() -> ImeService {
        ImeService {
            state: Arc::new(Mutex::new(ImeServiceState {
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
            match fidl::endpoints::create_proxy_and_stream::<txt::TextFieldMarker>() {
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
    async fn inject_input(&mut self, mut event: uii::InputEvent) {
        let keyboard_event = match &event {
            uii::InputEvent::Keyboard(e) => clone_keyboard_event(e),
            _ => return,
        };
        let mut state = self.state.lock().await;
        let ime = {
            let active_ime_weak = match state.active_ime {
                Some(ref v) => v,
                None => return, // no currently active IME
            };
            match LegacyIme::upgrade(active_ime_weak) {
                Some(active_ime) => active_ime,
                None => return, // IME no longer exists
            }
        };

        // Send the legacy ime a keystroke event to forward to connected clients. Even if a v2 input
        // method is connected, this ensures legacy text fields are able to still see key events;
        // something not yet provided by the new `TextField` API.
        ime.forward_event(clone_keyboard_event(&keyboard_event)).await;

        // Send the key event to any listening `TextInputContext` clients. If at least one still
        // exists, we assume it handled it and converted it into an edit sent via its handle to the
        // `TextField` protocol.
        state.text_input_context_clients.retain(|listener| {
            // drop listeners if they error on send
            listener.send_on_input_event(&mut event).is_ok()
        });

        // If no `TextInputContext` clients handled the input event, or if there are none connected,
        // we allow the internal input method inside of `LegacyIme` to convert this key event into
        // an edit.
        if state.text_input_context_clients.len() == 0 {
            ime.inject_input(keyboard_event).await;
        }
    }

    pub fn bind_ime_service(&self, mut stream: uii::ImeServiceRequestStream) {
        let mut self_clone = self.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) = stream
                    .try_next()
                    .await
                    .context("error reading value from IME service request stream")?
                {
                    self_clone
                        .handle_ime_service_msg(msg)
                        .await
                        .context("Handle IME service messages")?
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        )
        .detach();
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
            uii::ImeServiceRequest::InjectInput { event, .. } => {
                fx_vlog!(tag: "ime", 1, "InjectInput triggered: {:?}", event);
                self.inject_input(event).await;
            }
            uii::ImeServiceRequest::DispatchKey { .. }
            | uii::ImeServiceRequest::DispatchKey3 { .. }
            | uii::ImeServiceRequest::ViewFocusChanged { .. } => {
                // Transitional: DispatchKey should be handled by keyboard/Service.
                // See Service.spawn_ime_service() for handing DispatchKey.
                // In future, Keyboard service will receive keys directly.
                panic!("Should be handled by keyboard service");
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

    pub fn bind_text_input_context(&self, mut stream: txt::TextInputContextRequestStream) {
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
                        txt::TextInputContextRequest::HideKeyboard { .. } => {
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
    control_handle: &txt::TextInputContextControlHandle,
) -> Result<(), fidl::Error> {
    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream::<txt::TextFieldMarker>()
            .expect("Failed to create text field request stream");
    multiplexer.add_request_stream(request_stream);
    control_handle.send_on_focus(client_end)
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fidl_helpers::default_state;
    use crate::legacy_ime::{HID_USAGE_KEY_ENTER, HID_USAGE_KEY_LEFT};
    use fidl;
    use fidl_fuchsia_ui_input as uii;
    use fuchsia_async as fasync;
    use pin_utils::pin_mut;

    async fn get_state_update(
        editor_stream: &mut uii::InputMethodEditorClientRequestStream,
    ) -> (uii::TextInputState, Option<uii::KeyboardEvent>) {
        let msg = editor_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("ime should have sent message");
        if let uii::InputMethodEditorClientRequest::DidUpdateState { state, event, .. } = msg {
            let keyboard_event = event.map(|e| {
                if let uii::InputEvent::Keyboard(keyboard_event) = *e {
                    keyboard_event
                } else {
                    panic!("expected DidUpdateState to only send Keyboard events");
                }
            });
            (state, keyboard_event)
        } else {
            panic!("request should be DidUpdateState");
        }
    }

    fn async_service_test<T, F>(test_fn: T)
    where
        T: FnOnce(uii::ImeServiceProxy, uii::ImeVisibilityServiceProxy) -> F,
        F: Future,
    {
        let mut executor = fasync::Executor::new()
            .expect("Creating fuchsia_async executor for IME service tests failed");
        let ime_service = ImeService::new();
        let ime_service_proxy = {
            let (service_proxy, ime_stream) =
                fidl::endpoints::create_proxy_and_stream::<uii::ImeServiceMarker>().unwrap();
            ime_service.bind_ime_service(ime_stream);
            service_proxy
        };
        let visibility_service_proxy = {
            let (service_proxy, ime_vis_stream) =
                fidl::endpoints::create_proxy_and_stream::<uii::ImeVisibilityServiceMarker>()
                    .unwrap();
            ime_service.bind_ime_visibility_service(ime_vis_stream);
            service_proxy
        };
        let done = test_fn(ime_service_proxy, visibility_service_proxy);
        pin_mut!(done);
        // this will return a non-ready future if the tests stall
        let res = executor.run_until_stalled(&mut done);
        assert!(res.is_ready());
    }

    fn bind_ime_for_test(
        ime_service: &uii::ImeServiceProxy,
    ) -> (uii::InputMethodEditorProxy, uii::InputMethodEditorClientRequestStream) {
        let (ime_proxy, ime_server_end) =
            fidl::endpoints::create_proxy::<uii::InputMethodEditorMarker>().unwrap();
        let (editor_client_end, editor_request_stream) =
            fidl::endpoints::create_request_stream().unwrap();
        ime_service
            .get_input_method_editor(
                uii::KeyboardType::Text,
                uii::InputMethodAction::Done,
                &mut default_state(),
                editor_client_end,
                ime_server_end,
            )
            .unwrap();

        (ime_proxy, editor_request_stream)
    }

    fn simulate_keypress(ime_service: &uii::ImeServiceProxy, code_point: u32, hid_usage: u32) {
        ime_service
            .inject_input(&mut uii::InputEvent::Keyboard(uii::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: uii::KeyboardEventPhase::Pressed,
                hid_usage: hid_usage,
                code_point: code_point,
                modifiers: 0,
            }))
            .unwrap();
        ime_service
            .inject_input(&mut uii::InputEvent::Keyboard(uii::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: uii::KeyboardEventPhase::Released,
                hid_usage: hid_usage,
                code_point: code_point,
                modifiers: 0,
            }))
            .unwrap();
    }

    #[test]
    fn test_visibility_service_sends_updates() {
        async_service_test(|ime_service, visibility_service| {
            async move {
                let mut ev_stream = visibility_service.take_event_stream();

                // expect initial update with current status
                let msg = ev_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);

                // expect asking for keyboard to reclose results in another message
                ime_service.hide_keyboard().unwrap();
                let msg = ev_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);

                // expect asking for keyboard to to open in another message
                ime_service.show_keyboard().unwrap();
                let msg = ev_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, true);

                // expect asking for keyboard to close/open from IME works
                let (ime, _editor_stream) = bind_ime_for_test(&ime_service);
                ime.hide().unwrap();
                let msg = ev_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);
                ime.show().unwrap();
                let msg = ev_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, true);
            }
        });
    }

    #[test]
    fn test_inject_input_updates_ime() {
        async_service_test(|ime_service, _visibility_service| {
            async move {
                // expect asking for keyboard to close/open from IME works
                let (_ime, mut editor_stream) = bind_ime_for_test(&ime_service);

                // type 'a'
                simulate_keypress(&ime_service, 'a'.into(), 0);

                // get first message with keypress event but no state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                let event = event.expect("expected event to be set");
                assert_eq!(event.phase, uii::KeyboardEventPhase::Pressed);
                assert_eq!(event.code_point, 97);
                assert_eq!(state.text, "");

                // get second message with state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                assert!(event.is_none());
                assert_eq!(state.text, "a");
                assert_eq!(state.selection.base, 1);
                assert_eq!(state.selection.extent, 1);

                // get third message with keyrelease event but no state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                let event = event.expect("expected event to be set");
                assert_eq!(event.phase, uii::KeyboardEventPhase::Released);
                assert_eq!(event.code_point, 97);
                assert_eq!(state.text, "a");

                // press left arrow
                simulate_keypress(&ime_service, 0, HID_USAGE_KEY_LEFT);

                // get first message with keypress event but no state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                let event = event.expect("expected event to be set");
                assert_eq!(event.phase, uii::KeyboardEventPhase::Pressed);
                assert_eq!(event.code_point, 0);
                assert_eq!(event.hid_usage, HID_USAGE_KEY_LEFT);
                assert_eq!(state.text, "a");

                // get second message with state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                assert!(event.is_none());
                assert_eq!(state.text, "a");
                assert_eq!(state.selection.base, 0);
                assert_eq!(state.selection.extent, 0);

                // get first message with keyrelease event but no state update
                let (state, event) = get_state_update(&mut editor_stream).await;
                let event = event.expect("expected event to be set");
                assert_eq!(event.phase, uii::KeyboardEventPhase::Released);
                assert_eq!(event.code_point, 0);
                assert_eq!(event.hid_usage, HID_USAGE_KEY_LEFT);
                assert_eq!(state.text, "a");
            }
        });
    }

    #[test]
    fn test_inject_input_sends_action() {
        async_service_test(|ime_service, _visibility_service| {
            async move {
                let (_ime, mut editor_stream) = bind_ime_for_test(&ime_service);

                // send key events
                simulate_keypress(&ime_service, 0, HID_USAGE_KEY_ENTER);

                // get first message with keypress event
                let (_state, event) = get_state_update(&mut editor_stream).await;
                let event = event.expect("expected event to be set");
                assert_eq!(event.phase, uii::KeyboardEventPhase::Pressed);
                assert_eq!(event.code_point, 0);
                assert_eq!(event.hid_usage, HID_USAGE_KEY_ENTER);

                // get second message with onaction event
                let msg = editor_stream
                    .try_next()
                    .await
                    .expect("expected working event stream")
                    .expect("ime should have sent message");
                if let uii::InputMethodEditorClientRequest::OnAction { action, .. } = msg {
                    assert_eq!(action, uii::InputMethodAction::Done);
                } else {
                    panic!("request should be OnAction");
                }
            }
        })
    }
}
