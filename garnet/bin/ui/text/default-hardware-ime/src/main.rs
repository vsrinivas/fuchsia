// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::Error;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_text as txt;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::prelude::*;
use serde_json::{self as json, Map, Value};
use std::collections::HashMap;
use std::convert::TryInto;
use std::fs;
use std::sync::Arc;
use text_common::text_field_state::TextFieldState;

const DEFAULT_LAYOUT_PATH: &'static str = "/pkg/data/us.json";

// Keys specially handled by the IME.
// TODO(beckie): Move constants into common, centralized location?
const ENTER: u32 = 0x28;
const BACKSPACE: u32 = 0x2A;
const DELETE: u32 = 0x4C;
const RIGHT_ARROW: u32 = 0x4F;
const LEFT_ARROW: u32 = 0x50;
const DOWN_ARROW: u32 = 0x51;
const UP_ARROW: u32 = 0x52;
const NUM_ENTER: u32 = 0x58;

struct DefaultHardwareImeState {
    layout: Value,
    text_field: Option<txt::TextFieldProxy>,
    last_selection: Option<txt::Selection>,
    last_revision: Option<u64>,
    dead_key_state: Option<Value>,
    unicode_input_mode: bool,
    unicode_input_buffer: String,
}

#[derive(Clone)]
struct DefaultHardwareIme(Arc<Mutex<DefaultHardwareImeState>>);

impl DefaultHardwareIme {
    fn new() -> Result<DefaultHardwareIme, Error> {
        let data = fs::read_to_string(DEFAULT_LAYOUT_PATH)?;
        let layout = json::from_str(&data)?;
        let state = DefaultHardwareImeState {
            layout: layout,
            text_field: None,
            last_selection: None,
            last_revision: None,
            dead_key_state: None,
            unicode_input_mode: false,
            unicode_input_buffer: String::new(),
        };
        Ok(DefaultHardwareIme(Arc::new(Mutex::new(state))))
    }

    fn on_focus(&self, text_field: txt::TextFieldProxy) {
        let this = self.clone();
        fasync::spawn(
            async move {
                let mut evt_stream = text_field.take_event_stream();
                await!(this.0.lock()).text_field = Some(text_field);
                while let Some(evt) = await!(evt_stream.next()) {
                    match evt {
                        Ok(txt::TextFieldEvent::OnUpdate { state }) => {
                            await!(this.0.lock()).on_update(state.try_into().unwrap());
                        }
                        Err(e) => {
                            fx_log_err!(
                                "DefaultHardwareIme received an error from a TextField: {:#?}",
                                e
                            );
                        }
                    }
                }
            },
        );
    }
}

impl DefaultHardwareImeState {
    fn on_update(&mut self, state: TextFieldState) {
        self.last_selection = Some(state.selection);
        self.last_revision = Some(state.revision);
    }

    fn modifiers_match(
        current: &HashMap<&'static str, bool>,
        to_match: &Map<String, Value>,
    ) -> bool {
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

    fn get_key_mapping(&self, event: uii::KeyboardEvent) -> Value {
        let mut current_modifiers = HashMap::new();
        current_modifiers.insert("caps", (event.modifiers & uii::MODIFIER_CAPS_LOCK) != 0);
        current_modifiers.insert("shift", (event.modifiers & uii::MODIFIER_SHIFT) != 0);
        current_modifiers.insert("ctrl", (event.modifiers & uii::MODIFIER_CONTROL) != 0);
        current_modifiers.insert("alt", (event.modifiers & uii::MODIFIER_ALT) != 0);
        current_modifiers.insert("super", (event.modifiers & uii::MODIFIER_SUPER) != 0);
        if self.layout["tables"].is_array() {
            for table in self.layout["tables"].as_array().unwrap() {
                if table["modifiers"].is_object() {
                    let modifiers = table["modifiers"].as_object().unwrap();
                    if DefaultHardwareImeState::modifiers_match(&current_modifiers, modifiers) {
                        if table["map"].is_object() {
                            let map = table["map"].as_object().unwrap();
                            let key = &event.hid_usage.to_string();
                            if map.contains_key(key) {
                                return map[key].clone();
                            }
                        }
                    }
                }
            }
        }
        return Value::Null;
    }

    async fn on_input_event(&mut self, event: uii::KeyboardEvent) {
        if event.phase != uii::KeyboardEventPhase::Pressed
            && event.phase != uii::KeyboardEventPhase::Repeat
        {
            return;
        }

        // Handle keys that don't produce input (arrow keys, etc).
        if self.unicode_input_mode {
            match event.hid_usage {
                BACKSPACE => {
                    // TODO: Set or reset composition highlight.
                    if let Some((index, _)) = self.unicode_input_buffer.char_indices().last() {
                        self.unicode_input_buffer.truncate(index);
                    }
                    return;
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

                    let field = self.text_field.as_ref().unwrap();
                    let mut range = clone_range(self.last_selection.as_ref().unwrap());
                    field.begin_edit(self.last_revision.unwrap()).unwrap();
                    field.replace(&mut range, &output).unwrap();
                    on_commit_edit(await!(field.commit_edit()));

                    self.unicode_input_mode = false;
                    self.unicode_input_buffer = String::new();
                    return;
                }
                _ => {}
            }
        } else {
            match event.hid_usage {
                LEFT_ARROW | UP_ARROW => {
                    await!(self.adjust_selection(event.modifiers, -1));
                    return;
                }
                RIGHT_ARROW | DOWN_ARROW => {
                    await!(self.adjust_selection(event.modifiers, 1));
                    return;
                }
                BACKSPACE => {
                    await!(self.delete_selection(event.modifiers, -1));
                    return;
                }
                DELETE => {
                    await!(self.delete_selection(event.modifiers, 1));
                    return;
                }
                _ => {}
            }
        }

        // Handle keys that do produce input (as determined by the layout).
        let map = self.get_key_mapping(event);
        if map == Value::Null {
            return;
        }
        if map["output"].is_string() {
            let mut output = map["output"].as_str().unwrap().to_string();
            if let Some(dead_key) = &self.dead_key_state {
                // TODO: Remove deadkey highlight.
                if dead_key[&output].is_string() {
                    output = dead_key[&output].as_str().unwrap().to_string();
                } else if dead_key["\u{00A0}"].is_string() {
                    output = output + dead_key["\u{00A0}"].as_str().unwrap();
                } else if dead_key["\u{0020}"].is_string() {
                    output = dead_key["\u{0020}"].as_str().unwrap().to_string() + &output;
                }
                self.dead_key_state = None;
            }
            if self.unicode_input_mode {
                // TODO: Set or reset composition highlight.
                self.unicode_input_buffer += &output;
            } else {
                let field = self.text_field.as_ref().unwrap();
                let mut range = clone_range(self.last_selection.as_ref().unwrap());
                field.begin_edit(self.last_revision.unwrap()).unwrap();
                field.replace(&mut range, &output).unwrap();
                on_commit_edit(await!(field.commit_edit()));
            }
        }
        if map["deadkey"].is_object() {
            // TODO: Set or reset deadkey highlight.
            self.dead_key_state = Some(map["deadkey"].clone());
        }
        if map["unicode"].is_string() {
            // TODO: Set or reset composition highlight.
            self.unicode_input_mode = true;
            self.unicode_input_buffer = String::new();
        }
    }

    async fn adjust_selection(&mut self, modifiers: u32, distance: i64) {
        let field = self.text_field.as_ref().unwrap();
        let (anchor, mut focus) = clone_anchor_focus(self.last_selection.as_ref().unwrap());
        let revision = self.last_revision.unwrap();
        let (focus, error) = await!(field.position_offset(&mut focus, distance, revision)).unwrap();
        // TODO: By word, line, etc based on modifiers?
        if error != txt::Error::Ok {
            fx_log_err!("DefaultHardwareIme received a Error: {:#?}", error);
            return;
        }
        let range = if (modifiers & uii::MODIFIER_SHIFT) != 0 {
            txt::Range { start: clone_point(&anchor), end: clone_point(&focus) }
        } else {
            txt::Range { start: clone_point(&focus), end: clone_point(&focus) }
        };
        let mut selection = txt::Selection {
            range: range,
            anchor: txt::SelectionAnchor::AnchoredAtStart,
            affinity: self.last_selection.as_ref().unwrap().affinity,
        };
        field.begin_edit(revision).unwrap();
        field.set_selection(&mut selection).unwrap();
        on_commit_edit(await!(field.commit_edit()));
        self.last_selection = Some(selection);
    }

    async fn delete_selection(&mut self, _modifiers: u32, distance: i64) {
        let field = self.text_field.as_ref().unwrap();
        let mut range = clone_range(self.last_selection.as_ref().unwrap());
        let revision = self.last_revision.unwrap();
        let (dist, error) = await!(field.distance(&mut range, revision)).unwrap();
        if error != txt::Error::Ok {
            fx_log_err!("DefaultHardwareIme received a Error: {:#?}", error);
            return;
        }
        // If the selection is non-empty, simply delete it.
        if dist > 0 {
            field.begin_edit(revision).unwrap();
            field.replace(&mut range, "").unwrap();
            on_commit_edit(await!(field.commit_edit()));
            return;
        }
        // If the selection is empty, delete (distance) from the caret.
        let mut start = clone_start(self.last_selection.as_ref().unwrap());
        let (end, error) = await!(field.position_offset(&mut start, distance, revision)).unwrap();
        // TODO: By word, line, etc based on modifiers?
        if error != txt::Error::Ok {
            fx_log_err!("DefaultHardwareIme received a Error: {:#?}", error);
            return;
        }
        range = txt::Range { start: start, end: end };
        field.begin_edit(revision).unwrap();
        field.replace(&mut range, "").unwrap();
        on_commit_edit(await!(field.commit_edit()));
    }
}

fn on_commit_edit(error: Result<txt::Error, fidl::Error>) {
    match error {
        Ok(e) => {
            if e != txt::Error::Ok {
                fx_log_err!("DefaultHardwareIme received a Error: {:#?}", e);
            }
        }
        Err(e) => {
            fx_log_err!("DefaultHardwareIme received a fidl::Error: {:#?}", e);
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["default-hardware-ime"]).expect("syslog init should not fail");
    let ime = DefaultHardwareIme::new()?;
    let text_service = connect_to_service::<txt::TextInputContextMarker>()?;
    let mut evt_stream = text_service.take_event_stream();
    while let Some(evt) = await!(evt_stream.next()) {
        match evt {
            Ok(txt::TextInputContextEvent::OnFocus { text_field }) => {
                ime.on_focus(text_field.into_proxy()?);
            }
            Ok(txt::TextInputContextEvent::OnInputEvent { event }) => match event {
                uii::InputEvent::Keyboard(ke) => {
                    let mut lock = await!(ime.0.lock());
                    await!(lock.on_input_event(ke));
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

fn clone_range(selection: &txt::Selection) -> txt::Range {
    txt::Range {
        start: txt::Position { id: selection.range.start.id },
        end: txt::Position { id: selection.range.end.id },
    }
}

fn clone_anchor_focus(selection: &txt::Selection) -> (txt::Position, txt::Position) {
    if selection.anchor == txt::SelectionAnchor::AnchoredAtStart {
        return (
            txt::Position { id: selection.range.start.id },
            txt::Position { id: selection.range.end.id },
        );
    } else {
        return (
            txt::Position { id: selection.range.end.id },
            txt::Position { id: selection.range.start.id },
        );
    }
}

fn clone_point(point: &txt::Position) -> txt::Position {
    txt::Position { id: point.id }
}

fn clone_start(selection: &txt::Selection) -> txt::Position {
    txt::Position { id: selection.range.start.id }
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
