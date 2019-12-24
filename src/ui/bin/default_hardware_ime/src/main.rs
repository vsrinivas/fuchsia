// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_text as txt;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::prelude::*;
use serde_json::{self as json, Map, Value};
use std::collections::HashMap;
use std::collections::VecDeque;
use std::convert::TryInto;
use std::fs;
use std::sync::Arc;
use text::text_field_state::TextFieldState;

mod keymap;

const ENABLE_TEXTFIELD: bool = false;
const LEGACY_LAYOUT_PATH: &'static str = "/pkg/data/us-legacy.json";
const MAX_QUEUED_INPUTS: usize = 100;

type DeadKeyMap = serde_json::map::Map<String, Value>;

// Keys specially handled by the IME.
// TODO(beckie): Move constants into common, centralized location?
const ENTER: u32 = 0x28;
const BACKSPACE: u32 = 0x2A;
const NUM_ENTER: u32 = 0x58;

struct DefaultHardwareImeState {
    legacy_layout: Value,
    current_field: Option<CurrentField>,
    dead_key_state: Option<DeadKeyMap>,
    unicode_input_mode: bool,
    unicode_input_buffer: String,
    input_queue: VecDeque<uii::KeyboardEvent>,
}

struct CurrentField {
    proxy: txt::TextFieldProxy,
    last_revision: u64,
    last_selection: txt::Selection,
}

#[derive(Clone)]
struct DefaultHardwareIme(Arc<Mutex<DefaultHardwareImeState>>);

impl DefaultHardwareIme {
    fn new() -> Result<DefaultHardwareIme, Error> {
        let data = fs::read_to_string(LEGACY_LAYOUT_PATH)?;
        let legacy_layout = json::from_str(&data)?;

        let state = DefaultHardwareImeState {
            legacy_layout,
            current_field: None,
            dead_key_state: None,
            unicode_input_mode: false,
            unicode_input_buffer: String::new(),
            input_queue: VecDeque::new(),
        };
        Ok(DefaultHardwareIme(Arc::new(Mutex::new(state))))
    }

    fn on_focus(&self, text_field: txt::TextFieldProxy) {
        let this = self.clone();
        fasync::spawn(async move {
            let mut evt_stream = text_field.take_event_stream();
            // wait for first onupdate to populate self.current_field
            let res = evt_stream.next().await;
            if let Some(Ok(txt::TextFieldEvent::OnUpdate { state })) = res {
                let internal_state = match state.try_into() {
                    Ok(v) => v,
                    Err(e) => {
                        fx_log_err!("got invalid TextFieldState: {}", e);
                        return;
                    }
                };
                this.0.lock().await.on_first_update(text_field, internal_state);
                this.process_text_field_events(evt_stream).await.unwrap_or_else(|e| {
                    fx_log_err!("{}", e);
                });
            } else {
                fx_log_err!("failed to get OnUpdate from newly focused TextField: {:?}", res);
            }
        });
    }

    async fn process_text_field_events(
        &self,
        mut evt_stream: txt::TextFieldEventStream,
    ) -> Result<(), Error> {
        while let Some(msg) = evt_stream.next().await {
            match msg {
                Ok(txt::TextFieldEvent::OnUpdate { state }) => {
                    let mut lock = self.0.lock().await;
                    lock.on_update(state.try_into()?);
                    lock.process_input_queue().await;
                }
                Err(e) => {
                    return Err(format_err!(
                        "error when receiving message from TextFieldEventStream: {}",
                        e
                    ));
                }
            }
        }
        Ok(())
    }
}

#[derive(Debug)]
enum OnInputError {
    Retry,
    Err(Error),
}
impl<E: Into<Error>> From<E> for OnInputError {
    fn from(other: E) -> OnInputError {
        OnInputError::Err(other.into())
    }
}

impl DefaultHardwareImeState {
    fn on_first_update(&mut self, text_field: txt::TextFieldProxy, state: TextFieldState) {
        self.current_field = Some(CurrentField {
            proxy: text_field,
            last_revision: state.revision,
            last_selection: state.selection,
        });
    }

    fn on_update(&mut self, state: TextFieldState) {
        if let Some(s) = &mut self.current_field {
            s.last_selection = state.selection;
            s.last_revision = state.revision;
        }
    }

    async fn process_input_queue(&mut self) {
        while let Some(key) = self.input_queue.pop_front() {
            match self.on_input_event(&key).await {
                Ok(()) => {} // next
                Err(OnInputError::Retry) => {
                    // put it back in queue and return
                    self.input_queue.push_front(key);
                    return;
                }
                Err(OnInputError::Err(e)) => {
                    fx_log_err!("{:?}", e);
                }
            }
        }
    }

    /// Returns Err(OnInputError::Err) if something unrecoverably fails
    /// Returns Err(OnInputError::Retry) if you should retry. In this case, it's important that we
    /// *don't* mutate anything in self, so that retrying many times is idempotent.
    /// Returns Ok(()) if edit was successfully committed.
    async fn on_input_event<'a>(
        &'a mut self,
        event: &'a uii::KeyboardEvent,
    ) -> Result<(), OnInputError> {
        // only process input events if there is an active text field
        let field_state = match &mut self.current_field {
            Some(v) => v,
            None => return Ok(()),
        };
        if event.phase != uii::KeyboardEventPhase::Pressed
            && event.phase != uii::KeyboardEventPhase::Repeat
        {
            return Ok(());
        }

        // Handle unicode input mode keys
        if self.unicode_input_mode {
            match event.hid_usage {
                BACKSPACE => {
                    // TODO: Set or reset composition highlight.
                    if let Some((index, _)) = self.unicode_input_buffer.char_indices().last() {
                        self.unicode_input_buffer.truncate(index);
                    }
                    return Ok(());
                }
                ENTER | NUM_ENTER => {
                    // Just support hex input for now.
                    // TODO: Expand to support character name input.
                    // TODO: Remove composition highlight.

                    let output = self
                        .unicode_input_buffer
                        .split(|c: char| c == '+' || c.is_whitespace())
                        .filter_map(parse_code_point)
                        .collect::<String>();

                    let mut range = clone_range(&field_state.last_selection.range);
                    field_state.proxy.begin_edit(field_state.last_revision)?;
                    field_state.proxy.replace(&mut range, &output)?;
                    convert_commit_result(field_state.proxy.commit_edit().await)?;

                    self.unicode_input_mode = false;
                    self.unicode_input_buffer = String::new();
                    return Ok(());
                }
                _ => {}
            }
        }

        // Handle keys that do produce input (as determined by the layout).
        match get_key_mapping(&self.legacy_layout, event) {
            Err(e) => fx_log_err!("failed to find key mapping: {}", e),
            Ok(Keymapping::Output(mut output)) => {
                if let Some(dead_key) = &self.dead_key_state {
                    // TODO: Remove deadkey highlight.
                    if let Some(res) = dead_key[&output].as_str() {
                        output = res.to_string();
                    } else if let Some(res) = dead_key["\u{00A0}"].as_str() {
                        output = output + res;
                    } else if let Some(res) = dead_key["\u{0020}"].as_str() {
                        output = res.to_string() + &output;
                    }
                }
                if self.unicode_input_mode {
                    // TODO: Set or reset composition highlight.
                    self.unicode_input_buffer += &output;
                } else {
                    let mut range = clone_range(&field_state.last_selection.range);
                    field_state.proxy.begin_edit(field_state.last_revision)?;
                    field_state.proxy.replace(&mut range, &output)?;
                    convert_commit_result(field_state.proxy.commit_edit().await)?;
                }
                self.dead_key_state = None;
            }
            Ok(Keymapping::Deadkey(deadkey)) => {
                // TODO: Set or reset deadkey highlight.
                self.dead_key_state = Some(deadkey);
            }
            Ok(Keymapping::UnicodeMode) => {
                // TODO: Set or reset composition highlight.
                self.unicode_input_mode = true;
                self.unicode_input_buffer = String::new();
            }
        }

        Ok(())
    }
}

fn convert_commit_result(fidl_result: Result<txt::Error, fidl::Error>) -> Result<(), OnInputError> {
    match fidl_result {
        Ok(e) => match e {
            txt::Error::Ok => Ok(()),
            txt::Error::BadRevision => Err(OnInputError::Retry),
            e => Err(format_err!("DefaultHardwareIme received a Error: {:#?}", e).into()),
        },
        Err(e) => Err(format_err!("DefaultHardwareIme received a fidl::Error: {:#?}", e).into()),
    }
}

fn get_key_mapping(legacy_layout: &Value, event: &uii::KeyboardEvent) -> Result<Keymapping, Error> {
    let key = &event.hid_usage.to_string();
    let mut current_modifiers = HashMap::new();
    current_modifiers.insert("caps", (event.modifiers & uii::MODIFIER_CAPS_LOCK) != 0);
    current_modifiers.insert("shift", (event.modifiers & uii::MODIFIER_SHIFT) != 0);
    current_modifiers.insert("ctrl", (event.modifiers & uii::MODIFIER_CONTROL) != 0);
    current_modifiers.insert("alt", (event.modifiers & uii::MODIFIER_ALT) != 0);
    current_modifiers.insert("super", (event.modifiers & uii::MODIFIER_SUPER) != 0);
    let tables = match legacy_layout["tables"].as_array() {
        Some(v) => v,
        None => return Err(format_err!("expected legacy_layout.tables to be a JSON array")),
    };
    for table in tables {
        let modifiers = match table["modifiers"].as_object() {
            Some(v) => v,
            None => return Err(format_err!("expected table.modifiers to be a JSON object")),
        };
        let map = match table["map"].as_object() {
            Some(v) => v,
            None => return Err(format_err!("expected table.map to be a JSON object")),
        };
        if modifiers_match(&current_modifiers, modifiers) && map.contains_key(key) {
            let key_obj = map[key].as_object();
            if let Some(output) = key_obj.and_then(|m| m["output"].as_str()) {
                return Ok(Keymapping::Output(output.to_string()));
            } else if let Some(deadkey) = key_obj.and_then(|m| m["deadkey"].as_object()) {
                return Ok(Keymapping::Deadkey(deadkey.clone()));
            } else if let Some(_) = key_obj.and_then(|m| m["unicode"].as_str()) {
                return Ok(Keymapping::UnicodeMode);
            } else {
                return Err(format_err!(
                    "expected object to either contain 'deadkey', 'output', or 'unicode' keys"
                ));
            }
        }
    }
    return Err(format_err!("couldn't find matching key in keymap"));
}

#[allow(dead_code)]
enum Keymapping {
    /// This key combination indicates you should just output some text directly.
    Output(String),

    /// This key combination indicates you should use this dead key lookup table on the next typed
    /// character.
    Deadkey(DeadKeyMap),

    /// This key combination indicates you should start arbitrary unicode insert mode
    UnicodeMode,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["default-hardware-ime"]).expect("syslog init should not fail");
    serve_keymap(keymap::KeymapService::new()?).await.context("error serving keymap")?;

    if ENABLE_TEXTFIELD {
        let ime = DefaultHardwareIme::new()?;
        fasync::spawn(
            serve_textfield(ime.clone())
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    }

    Ok(())
}

async fn serve_textfield(ime: DefaultHardwareIme) -> Result<(), Error> {
    let text_service = connect_to_service::<txt::TextInputContextMarker>()?;
    let mut evt_stream = text_service.take_event_stream();
    while let Some(evt) = evt_stream.next().await {
        match evt {
            Ok(txt::TextInputContextEvent::OnFocus { text_field }) => {
                ime.on_focus(text_field.into_proxy()?);
            }
            Ok(txt::TextInputContextEvent::OnInputEvent { event }) => match event {
                uii::InputEvent::Keyboard(ke) => {
                    let mut lock = ime.0.lock().await;
                    // drop inputs if the queue is really long
                    if lock.input_queue.len() < MAX_QUEUED_INPUTS {
                        lock.input_queue.push_back(ke);
                    }
                    lock.process_input_queue().await;
                }
                _ => {
                    fx_log_err!("DefaultHardwareIme received a non-keyboard event");
                }
            },
            Err(e) => {
                fx_log_err!(
                    "DefaultHardwareIme received an error from a TextInputContext: {:#?}",
                    e
                );
            }
        }
    }
    Ok(())
}

async fn serve_keymap(keymap_service: keymap::KeymapService) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: ui_input::KeyboardLayoutStateRequestStream| {
        let keymap_service = keymap_service.clone();
        fuchsia_async::spawn(
            keymap::handle_watch_keymap(stream, keymap_service)
                .unwrap_or_else(|e: anyhow::Error| fx_log_err!("couldn't run: {:?}", e)),
        );
    });
    fs.take_and_serve_directory_handle()?;

    Ok(fs.collect().await)
}

fn clone_range(range: &txt::Range) -> txt::Range {
    txt::Range {
        start: txt::Position { id: range.start.id },
        end: txt::Position { id: range.end.id },
    }
}

fn parse_code_point(s: &str) -> Option<char> {
    if s.is_empty() {
        return None;
    }
    match u32::from_str_radix(s, 16) {
        Err(_) => Some(std::char::REPLACEMENT_CHARACTER),
        Ok(cc) => match std::char::from_u32(cc) {
            None => Some(std::char::REPLACEMENT_CHARACTER),
            Some(c) => Some(c),
        },
    }
}

fn modifiers_match(current: &HashMap<&'static str, bool>, to_match: &Map<String, Value>) -> bool {
    for (key, value) in to_match {
        match current.get(key.as_str()) {
            Some(&other_value) => {
                if value.as_bool() != Some(other_value) {
                    return false;
                }
            }
            None => {
                if value.as_bool() != None {
                    return false;
                }
            }
        }
    }
    return true;
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::future::join;

    fn default_state() -> TextFieldState {
        TextFieldState {
            document: txt::Range { start: txt::Position { id: 0 }, end: txt::Position { id: 0 } },
            selection: txt::Selection {
                range: txt::Range { start: txt::Position { id: 0 }, end: txt::Position { id: 0 } },
                anchor: txt::SelectionAnchor::AnchoredAtStart,
                affinity: txt::Affinity::Upstream,
            },
            revision: 0,
            composition: None,
            composition_highlight: None,
            dead_key_highlight: None,
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn state_sends_edits_on_input_and_retries() {
        let ime = DefaultHardwareIme::new().unwrap();
        let (proxy, server_end) = fidl::endpoints::create_proxy::<txt::TextFieldMarker>().unwrap();
        let mut request_stream = server_end.into_stream().unwrap();

        let client = async move {
            let mut lock = ime.0.lock().await;

            // simulate onupdate
            lock.on_first_update(proxy, default_state().into());

            // dispatch input event, but drop input if we've already queued some wild number of
            // events
            lock.input_queue.push_back(uii::KeyboardEvent {
                event_time: 0,
                device_id: 0,
                phase: uii::KeyboardEventPhase::Pressed,
                hid_usage: 4,
                code_point: 33,
                modifiers: 0,
            });
            lock.process_input_queue().await;

            // try again
            lock.process_input_queue().await;
        };
        let server = async move {
            // first set of edits, reply with BadRevision
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::BeginEdit { .. } => {}
                _ => panic!("expected first BeginEdit request"),
            }
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::Replace { new_text, .. } => {
                    assert_eq!("a", new_text);
                }
                _ => panic!("expected first Replace request"),
            }
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::CommitEdit { responder, .. } => {
                    responder.send(txt::Error::BadRevision).unwrap();
                }
                _ => panic!("expected first CommitEdit request"),
            }

            // second round of updates
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::BeginEdit { .. } => {}
                _ => panic!("expected second BeginEdit request"),
            }
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::Replace { new_text, .. } => {
                    assert_eq!("a", new_text);
                }
                _ => panic!("expected second Replace request"),
            }
            let msg = request_stream.try_next().await.unwrap().unwrap();
            match msg {
                txt::TextFieldRequest::CommitEdit { responder, .. } => {
                    responder.send(txt::Error::Ok).unwrap();
                }
                _ => panic!("expected second CommitEdit request"),
            }
        };
        join(server, client).await;
    }
}
