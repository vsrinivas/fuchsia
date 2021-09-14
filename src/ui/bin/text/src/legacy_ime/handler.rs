// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an implementation of `LegacyIme` itself.

use {
    anyhow::{format_err, Context as _, Error, Result},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_ui_input::{self as uii, InputMethodEditorRequest as ImeReq},
    fidl_fuchsia_ui_input3 as ui_input3, fidl_fuchsia_ui_text as txt,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{lock::Mutex, prelude::*},
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
        sync::{Arc, Weak},
    },
};

use super::{
    state::{get_point, get_range, ImeState},
    HID_USAGE_KEY_BACKSPACE, HID_USAGE_KEY_DELETE, HID_USAGE_KEY_ENTER, HID_USAGE_KEY_LEFT,
    HID_USAGE_KEY_RIGHT,
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
            input_method: None,
            transaction_changes: Vec::new(),
            transaction_revision: None,
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
    pub fn bind_text_field(&self, mut stream: txt::TextFieldLegacyRequestStream) {
        let mut self_clone = self.clone();
        fuchsia_async::Task::spawn(
            async move {
                let control_handle = stream.control_handle();
                {
                    let mut state = self_clone.0.lock().await;
                    let res = control_handle.send_on_update(state.as_text_field_state().into());
                    if let Err(e) = res {
                        fx_log_err!("{}", e);
                    } else {
                        state.input_method = Some(control_handle);
                    }
                }
                while let Some(msg) = stream
                    .try_next()
                    .await
                    .context("error reading value from text field request stream")?
                {
                    if let Err(e) = self_clone.handle_text_field_msg(msg).await {
                        fx_log_err!("error when replying to TextFieldRequest: {}", e);
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        )
        .detach();
    }

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

    /// Handles a TextFieldRequest, returning a FIDL error if one occurred when sending a reply.
    async fn handle_text_field_msg(
        &mut self,
        msg: txt::TextFieldLegacyRequest,
    ) -> Result<(), fidl::Error> {
        let mut ime_state = self.0.lock().await;
        match msg {
            txt::TextFieldLegacyRequest::PositionOffset {
                old_position,
                offset,
                revision,
                responder,
            } => {
                if revision != ime_state.revision {
                    return responder
                        .send(&mut txt::Position { id: 0 }, txt::ErrorLegacy::BadRevision);
                }
                let old_char_index = if let Some(v) =
                    get_point(&ime_state.text_points, &old_position).and_then(|old_byte_index| {
                        idx::byte_to_char(&ime_state.text_state.text, old_byte_index)
                    }) {
                    v
                } else {
                    return responder
                        .send(&mut txt::Position { id: 0 }, txt::ErrorLegacy::BadRequest);
                };
                let new_char_index = (old_char_index as i64 + offset)
                    .max(0)
                    .min(ime_state.text_state.text.chars().count() as i64);
                // ok to .expect() here, since char_to_byte can only fail if new_char_index is out of the char indices
                let new_byte_index = idx::char_to_byte(&ime_state.text_state.text, new_char_index)
                    .expect("did not expect character to fail");
                let mut new_point = ime_state.new_point(new_byte_index);
                return responder.send(&mut new_point, txt::ErrorLegacy::Ok);
            }
            txt::TextFieldLegacyRequest::Distance { range, revision, responder } => {
                if revision != ime_state.revision {
                    return responder.send(0, txt::ErrorLegacy::BadRevision);
                }
                let (byte_start, byte_end) = match get_range(&ime_state.text_points, &range, false)
                {
                    Some(v) => v,
                    None => {
                        return responder.send(0, txt::ErrorLegacy::BadRequest);
                    }
                };
                let (char_start, char_end) = match (
                    idx::byte_to_char(&ime_state.text_state.text, byte_start),
                    idx::byte_to_char(&ime_state.text_state.text, byte_end),
                ) {
                    (Some(a), Some(b)) => (a, b),
                    _ => {
                        return responder.send(0, txt::ErrorLegacy::BadRequest);
                    }
                };
                return responder.send(char_end as i64 - char_start as i64, txt::ErrorLegacy::Ok);
            }
            txt::TextFieldLegacyRequest::Contents { range, revision, responder } => {
                if revision != ime_state.revision {
                    return responder.send(
                        "",
                        &mut txt::Position { id: 0 },
                        txt::ErrorLegacy::BadRevision,
                    );
                }
                match get_range(&ime_state.text_points, &range, true) {
                    Some((start, end)) => {
                        let mut start_point = ime_state.new_point(start);
                        match ime_state.text_state.text.get(start..end) {
                            Some(contents) => {
                                return responder.send(
                                    contents,
                                    &mut start_point,
                                    txt::ErrorLegacy::Ok,
                                );
                            }
                            None => {
                                return responder.send(
                                    "",
                                    &mut txt::Position { id: 0 },
                                    txt::ErrorLegacy::BadRequest,
                                );
                            }
                        }
                    }
                    None => {
                        return responder.send(
                            "",
                            &mut txt::Position { id: 0 },
                            txt::ErrorLegacy::BadRequest,
                        );
                    }
                }
            }
            txt::TextFieldLegacyRequest::BeginEdit { revision, .. } => {
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = Some(revision);
                return Ok(());
            }
            txt::TextFieldLegacyRequest::CommitEdit { responder, .. } => {
                if ime_state.transaction_revision != Some(ime_state.revision) {
                    return responder.send(txt::ErrorLegacy::BadRevision);
                }
                let res = if ime_state.apply_transaction() {
                    let res = responder.send(txt::ErrorLegacy::Ok);
                    ime_state.increment_revision(true);
                    res
                } else {
                    responder.send(txt::ErrorLegacy::BadRequest)
                };
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = None;
                return res;
            }
            txt::TextFieldLegacyRequest::AbortEdit { .. } => {
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = None;
                return Ok(());
            }
            req @ txt::TextFieldLegacyRequest::Replace { .. }
            | req @ txt::TextFieldLegacyRequest::SetSelection { .. }
            | req @ txt::TextFieldLegacyRequest::SetComposition { .. }
            | req @ txt::TextFieldLegacyRequest::ClearComposition { .. }
            | req @ txt::TextFieldLegacyRequest::SetDeadKeyHighlight { .. }
            | req @ txt::TextFieldLegacyRequest::ClearDeadKeyHighlight { .. } => {
                if ime_state.transaction_revision.is_some() {
                    ime_state.transaction_changes.push(req)
                }
                return Ok(());
            }
        }
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
                    HID_USAGE_KEY_ENTER => {
                        state
                            .client
                            .on_action(state.action)
                            .context("error sending action to ImeClient")?;
                    }
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
