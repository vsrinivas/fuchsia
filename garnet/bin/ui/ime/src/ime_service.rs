// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::legacy_ime::Ime;
use crate::legacy_ime::ImeState;
use failure::ResultExt;
use fidl::endpoints::{ClientEnd, RequestStream, ServerEnd};
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_text as txt;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::prelude::*;
use std::sync::{Arc, Weak};

pub struct ImeServiceState {
    pub keyboard_visible: bool,
    pub active_ime: Option<Weak<Mutex<ImeState>>>,
    pub visibility_listeners: Vec<uii::ImeVisibilityServiceControlHandle>,

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
            text_input_context_clients: Vec::new(),
        })))
    }

    /// Only updates the keyboard visibility if IME passed in is active
    pub async fn update_keyboard_visibility_from_ime<'a>(
        &'a self,
        check_ime: &'a Arc<Mutex<ImeState>>,
        visible: bool,
    ) {
        let mut state = await!(self.0.lock());
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
        let ime = Ime::new(keyboard_type, action, initial_state, client_proxy, self.clone());
        let mut state = await!(self.0.lock());
        state.active_ime = Some(ime.downgrade());
        if let Ok(chan) = fuchsia_async::Channel::from_channel(editor.into_channel()) {
            ime.bind_ime(chan);
        }
        state.text_input_context_clients.retain(|listener| {
            // drop listeners if they error on send
            bind_new_text_field(&ime, &listener).is_ok()
        });
    }

    pub async fn show_keyboard(&self) {
        await!(self.0.lock()).update_keyboard_visibility(true);
    }

    pub async fn hide_keyboard(&self) {
        await!(self.0.lock()).update_keyboard_visibility(false);
    }

    /// This is called by the operating system when input from the physical keyboard comes in.
    /// It also is called by legacy onscreen keyboards that just simulate physical keyboard input.
    async fn inject_input(&mut self, mut event: uii::InputEvent) {
        let mut state = await!(self.0.lock());
        let ime = {
            let active_ime_weak = match state.active_ime {
                Some(ref v) => v,
                None => return, // no currently active IME
            };
            match Ime::upgrade(active_ime_weak) {
                Some(active_ime) => active_ime,
                None => return, // IME no longer exists
            }
        };
        state.text_input_context_clients.retain(|listener| {
            // drop listeners if they error on send
            listener.send_on_input_event(&mut event).is_ok()
        });
        // only use the default text input handler in ime.rs if there are no text_input_context_clients
        // attached to handle it
        if state.text_input_context_clients.len() == 0 {
            await!(ime.inject_input(event));
        }
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
                            await!(self_clone.get_input_method_editor(
                                keyboard_type,
                                action,
                                initial_state,
                                client,
                                editor,
                            ));
                        }
                        uii::ImeServiceRequest::ShowKeyboard { .. } => {
                            await!(self_clone.show_keyboard());
                        }
                        uii::ImeServiceRequest::HideKeyboard { .. } => {
                            await!(self_clone.hide_keyboard());
                        }
                        uii::ImeServiceRequest::InjectInput { event, .. } => {
                            await!(self_clone.inject_input(event));
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
                let mut state = await!(self_clone.0.lock());
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

    pub fn bind_text_input_context(&self, chan: fuchsia_async::Channel) {
        let self_clone = self.clone();
        fuchsia_async::spawn(
            async move {
                let mut stream = txt::TextInputContextRequestStream::from_channel(chan);
                let control_handle = stream.control_handle();
                {
                    let mut state = await!(self_clone.0.lock());

                    let active_ime_opt = match state.active_ime {
                        Some(ref weak) => Ime::upgrade(weak),
                        None => None, // no currently active IME
                    };

                    if let Some(active_ime) = active_ime_opt {
                        if let Err(e) = bind_new_text_field(&active_ime, &control_handle) {
                            fx_log_err!("Error when binding text field for newly connected TextInputContext: {}", e);
                        }
                    }
                    state.text_input_context_clients.push(control_handle)
                }
                while let Some(msg) = await!(stream.try_next())
                    .context("error reading value from text input context request stream")?
                {
                    match msg {
                        txt::TextInputContextRequest::HideKeyboard { .. } => {
                            await!(self_clone.hide_keyboard());
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
        );
    }
}

pub fn bind_new_text_field(
    active_ime: &Ime,
    control_handle: &txt::TextInputContextControlHandle,
) -> Result<(), fidl::Error> {
    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream::<txt::TextFieldMarker>()
            .expect("Failed to create text field request stream");
    // TODO(lard): this currently overwrites active_ime's TextField, since it only supports
    // one at a time. In the future, ImeService should multiplex multiple TextField
    // implementations into Ime.
    active_ime.bind_text_field(request_stream);
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

    fn async_service_test<T, F>(test_fn: T)
    where
        T: FnOnce(uii::ImeServiceProxy, uii::ImeVisibilityServiceProxy) -> F,
        F: Future,
    {
        let mut executor = fasync::Executor::new()
            .expect("Creating fuchsia_async executor for IME service tests failed");
        let ime_service = ImeService::new();
        let ime_service_proxy = {
            let (service_proxy, service_server_end) =
                fidl::endpoints::create_proxy::<uii::ImeServiceMarker>().unwrap();
            let chan = fasync::Channel::from_channel(service_server_end.into_channel()).unwrap();
            ime_service.bind_ime_service(chan);
            service_proxy
        };
        let visibility_service_proxy = {
            let (service_proxy, service_server_end) =
                fidl::endpoints::create_proxy::<uii::ImeVisibilityServiceMarker>().unwrap();
            let chan = fasync::Channel::from_channel(service_server_end.into_channel()).unwrap();
            ime_service.bind_ime_visibility_service(chan);
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
                let msg = await!(ev_stream.try_next())
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);

                // expect asking for keyboard to reclose results in another message
                ime_service.hide_keyboard().unwrap();
                let msg = await!(ev_stream.try_next())
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);

                // expect asking for keyboard to to open in another message
                ime_service.show_keyboard().unwrap();
                let msg = await!(ev_stream.try_next())
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, true);

                // expect asking for keyboard to close/open from IME works
                let (ime, _editor_stream) = bind_ime_for_test(&ime_service);
                ime.hide().unwrap();
                let msg = await!(ev_stream.try_next())
                    .expect("expected working event stream")
                    .expect("visibility service should have sent message");
                let uii::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } = msg;
                assert_eq!(visible, false);
                ime.show().unwrap();
                let msg = await!(ev_stream.try_next())
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
                let msg = await!(editor_stream.try_next())
                    .expect("expected working event stream")
                    .expect("ime should have sent message");
                if let uii::InputMethodEditorClientRequest::DidUpdateState {
                    state, event: _, ..
                } = msg
                {
                    assert_eq!(state.text, "a");
                    assert_eq!(state.selection.base, 1);
                    assert_eq!(state.selection.extent, 1);
                } else {
                    panic!("request should be DidUpdateState");
                }

                // press left arrow
                simulate_keypress(&ime_service, 0, HID_USAGE_KEY_LEFT);
                let msg = await!(editor_stream.try_next())
                    .expect("expected working event stream")
                    .expect("ime should have sent message");
                if let uii::InputMethodEditorClientRequest::DidUpdateState {
                    state, event: _, ..
                } = msg
                {
                    assert_eq!(state.text, "a");
                    assert_eq!(state.selection.base, 0);
                    assert_eq!(state.selection.extent, 0);
                } else {
                    panic!("request should be DidUpdateState");
                }
            }
        });
    }

    #[test]
    fn test_inject_input_sends_action() {
        async_service_test(|ime_service, _visibility_service| {
            async move {
                let (_ime, mut editor_stream) = bind_ime_for_test(&ime_service);
                simulate_keypress(&ime_service, 0, HID_USAGE_KEY_ENTER);
                let msg = await!(editor_stream.try_next())
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
