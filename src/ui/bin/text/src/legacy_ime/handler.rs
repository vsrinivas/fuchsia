// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an implementation of `LegacyIme` itself.

use {
    anyhow::{format_err, Context, Error, Result},
    fidl_fuchsia_ui_input::{self as uii, InputMethodEditorRequest as ImeReq},
    fidl_fuchsia_ui_input3 as ui_input3,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{lock::Mutex, prelude::*},
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

use super::{
    state::ImeState, HID_USAGE_KEY_BACKSPACE, HID_USAGE_KEY_DELETE, HID_USAGE_KEY_ENTER,
    HID_USAGE_KEY_LEFT, HID_USAGE_KEY_RIGHT,
};
use crate::{index_convert as idx, keyboard::events, text_manager::TextManager};

/// An input method provides edits and cursor updates to a text field. This Legacy Input Method
/// provides edits to a text field over the legacy pair of interfaces `InputMethodEditor` and
/// `InputMethodEditorClient`. It can provide these edits one of two ways:
///
/// 1. If `inject_input()` is called with a `KeyboardEvent`, `LegacyIme` contains an implementation
/// of a QWERTY latin keyboard input method, and will send the appropriate edits to the text field.
/// This can also handle arrow keys with modifiers to perform selection/caret movement.
/// 2. If `bind_text_field()` is called with a `TextFieldRequestStream`, `LegacyIme` can serve the
/// new `TextField` interface, translating edits or content requests from that into requests for the
/// `InputMethodEditorClient`.
#[derive(Clone)]
pub struct LegacyIme(Arc<Mutex<ImeState>>);

impl LegacyIme {
    pub fn new<I: 'static + uii::InputMethodEditorClientProxyInterface>(
        keyboard_type: uii::KeyboardType,
        action: uii::InputMethodAction,
        initial_state: uii::TextInputState,
        client: I,
        text_manager: TextManager,
    ) -> LegacyIme {
        let state = ImeState {
            text_state: initial_state,
            client: Box::new(client),
            keyboard_type,
            action,
            text_manager,
            revision: 0,
            next_text_point_id: 0,
            text_points: HashMap::new(),
            keys_pressed: HashSet::new(),
        };
        LegacyIme(Arc::new(Mutex::new(state)))
    }

    pub fn downgrade(&self) -> Weak<Mutex<ImeState>> {
        Arc::downgrade(&self.0)
    }

    pub fn upgrade(weak: &Weak<Mutex<ImeState>>) -> Option<LegacyIme> {
        weak.upgrade().map(|arc| LegacyIme(arc))
    }

    /// Binds a `TextField` to this `LegacyIme`, replacing and unbinding any previous `TextField`.
    /// All requests from the request stream will be translated into requests for
    /// `InputMethodEditorClient`, and for events, vice-versa.
    /// Handles all state updates passed down the `InputMethodEditorRequestStream`.
    pub fn bind_ime(&self, mut stream: uii::InputMethodEditorRequestStream) {
        let self_clone = self.clone();
        let self_clone_2 = self.clone();
        fuchsia_async::Task::spawn(
            async move {
                while let Some(msg) = stream
                    .try_next()
                    .await
                    .context("error reading value from IME request stream")?
                {
                    self_clone
                        .handle_ime_message(msg)
                        .await
                        .unwrap_or_else(|e| fx_log_warn!("error handling ime message: {:?}", e));
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e))
            .then(|()| {
                async move {
                    // this runs when IME stream closes
                    // clone to ensure we only hold one lock at a time
                    let text_manager = self_clone_2.0.lock().await.text_manager.clone();
                    text_manager.update_keyboard_visibility_from_ime(&self_clone_2.0, false).await;
                }
            }),
        )
        .detach();
    }

    /// Handles a request from the legancy IME API, an InputMethodEditorRequest.
    async fn handle_ime_message(&self, msg: uii::InputMethodEditorRequest) -> Result<()> {
        match msg {
            ImeReq::SetKeyboardType { keyboard_type, .. } => {
                let mut state = self.0.lock().await;
                state.keyboard_type = keyboard_type;
                Ok(())
            }
            ImeReq::SetState { state, .. } => {
                self.set_state(state).await;
                Ok(())
            }
            ImeReq::InjectInput { .. } => {
                Err(format_err!("InjectInput is deprecated, use DispatchKey3!"))
            }
            ImeReq::DispatchKey3 { event, responder, .. } => {
                let key_event = {
                    let state = self.0.lock().await;
                    events::KeyEvent::new(&event, state.keys_pressed.clone())
                        .context("error converting key")?
                };
                self.inject_input(key_event).await?;
                self.update_keys_pressed(&event).await?;
                // False since legacy API doesn't support input handling.
                // Handling here depends on the hardcoded order of input processing.
                // This should be reworked in future IME design.
                responder.send(false).context("error sending response for DispatchKey3")
            }
            ImeReq::Show { .. } => {
                // clone to ensure we only hold one lock at a time
                let text_manager = self.0.lock().await.text_manager.clone();
                text_manager.show_keyboard().await;
                Ok(())
            }
            ImeReq::Hide { .. } => {
                // clone to ensure we only hold one lock at a time
                let text_manager = self.0.lock().await.text_manager.clone();
                text_manager.hide_keyboard().await;
                Ok(())
            }
        }
    }

    /// Sets the internal state. Expects input_state to use codeunits; automatically
    /// converts to byte indices before storing.
    pub async fn set_state(&self, input_state: uii::TextInputState) {
        let mut state = self.0.lock().await;
        state.text_state = idx::text_state_codeunit_to_byte(input_state);
        // the old C++ IME implementation didn't call did_update_state here, so this second argument is false.
        state.increment_revision(false);
    }

    /// Forwards an event to the `InputMethodEditorClient`, by sending a state update that makes no
    /// changes alongside a `KeyboardEvent`. Note that this state update will always have no changes
    /// — if you'd like to send a change, use `inject_input()`.
    pub(crate) async fn forward_event(&self, event: &events::KeyEvent) -> Result<(), Error> {
        let mut state = self.0.lock().await;
        state.forward_event(event.clone())
    }

    /// Uses `ImeState`'s internal latin input method implementation to determine the edit
    /// corresponding to `keyboard_event`, and sends this as a state update to
    /// `InputMethodEditorClient`. This method can handle arrow keys to move selections, even with
    /// modifier keys implemented correctly. However, it does *not* send the actual key event to the
    /// client; for that, you should simultaneously call `forward_event()`.
    pub(crate) async fn inject_input(&self, key_event: events::KeyEvent) -> Result<(), Error> {
        let keyboard_event: uii::KeyboardEvent =
            key_event.try_into().context("error converting key event to keyboard event")?;

        let mut state = self.0.lock().await;

        if keyboard_event.phase == uii::KeyboardEventPhase::Pressed
            || keyboard_event.phase == uii::KeyboardEventPhase::Repeat
        {
            if keyboard_event.code_point != 0 {
                state.type_keycode(keyboard_event.code_point);
                state.increment_revision(true);
            } else {
                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_BACKSPACE => {
                        state.delete_backward();
                        state.increment_revision(true);
                    }
                    HID_USAGE_KEY_DELETE => {
                        state.delete_forward();
                        state.increment_revision(true);
                    }
                    HID_USAGE_KEY_LEFT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, false);
                        state.increment_revision(true);
                    }
                    HID_USAGE_KEY_RIGHT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, true);
                        state.increment_revision(true);
                    }
                    HID_USAGE_KEY_ENTER => match state.action {
                        // Pressing "Enter" inserts a newline when the input
                        // method is used to edit multi-line text. For other
                        // input actions, we report back to the
                        // client that it needs to perform some action (move
                        // focus, perform a search etc.).
                        uii::InputMethodAction::Newline => {
                            state.type_keycode('\n' as u32);
                            state.increment_revision(true);
                        }
                        _ => {
                            state
                                .client
                                .on_action(state.action)
                                .context("error sending action to ImeClient")?;
                        }
                    },
                    _ => {
                        // Not an editing key, forward the event to clients.
                        state.increment_revision(true);
                    }
                }
            }
        }
        Ok(())
    }

    /// Updates currently pressed keys.
    async fn update_keys_pressed(&self, event: &ui_input3::KeyEvent) -> Result<(), Error> {
        let type_ = event.type_.ok_or(format_err!("Expected type to be populated."))?;
        let key = event.key.ok_or(format_err!("Expected key to be populated."))?;
        let keys_pressed = &mut self.0.lock().await.keys_pressed;
        match type_ {
            ui_input3::KeyEventType::Sync | ui_input3::KeyEventType::Pressed => {
                keys_pressed.insert(key);
            }
            ui_input3::KeyEventType::Cancel | ui_input3::KeyEventType::Released => {
                keys_pressed.remove(&key);
            }
        };
        Ok(())
    }
}
