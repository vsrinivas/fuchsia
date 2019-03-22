// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_helpers::clone_state;
use crate::ime_service::ImeService;
use failure::ResultExt;
use fidl::encoding::OutOfLine;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_input::InputMethodEditorRequest as ImeReq;
use fidl_fuchsia_ui_text as txt;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::prelude::*;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use regex::Regex;
use std::char;
use std::collections::HashMap;
use std::ops::Range;
use std::sync::{Arc, Weak};
use unicode_segmentation::{GraphemeCursor, UnicodeSegmentation};

// TODO(lard): move constants into common, centralized location?
pub const HID_USAGE_KEY_BACKSPACE: u32 = 0x2a;
pub const HID_USAGE_KEY_RIGHT: u32 = 0x4f;
pub const HID_USAGE_KEY_LEFT: u32 = 0x50;
pub const HID_USAGE_KEY_ENTER: u32 = 0x28;
pub const HID_USAGE_KEY_DELETE: u32 = 0x2e;

/// The internal state of the IME, usually held within the IME behind an Arc<Mutex>
/// so it can be accessed from multiple places.
pub struct ImeState {
    text_state: uii::TextInputState,

    /// A handle to call methods on the text field.
    client: Box<uii::InputMethodEditorClientProxyInterface>,

    keyboard_type: uii::KeyboardType,
    action: uii::InputMethodAction,
    ime_service: ImeService,

    /// We expose a TextField interface to an input method. There are also legacy
    /// input methods that just send key events through inject_input — in this case,
    /// input_method would be None, and these events would be handled by the
    /// inject_input method. ImeState can only handle talking to one input_method
    /// at a time; it's the responsibility of some other code (likely inside
    /// ImeService) to multiplex multiple TextField interfaces into this one.
    input_method: Option<txt::TextFieldControlHandle>,

    /// A number used to serve the TextField interface. It increments any time any
    /// party makes a change to the state.
    revision: u64,

    /// A TextPoint is a u64 token that represents a character position in the new
    /// TextField interface. Each token is a unique ID; this represents the ID we
    /// will assign to the next TextPoint that is created. It increments every time
    /// a TextPoint is created. This is never reset, even when TextPoints are
    /// invalidated; TextPoints have globally unique IDs.
    next_text_point_id: u64,

    /// A TextPoint is a u64 token that represents a character position in the new
    /// TextField interface. This maps TextPoint IDs to byte indexes inside
    /// `text_state.text`. When a new revision is created, all preexisting TextPoints
    /// are deleted, which means we clear this out.
    text_points: HashMap<u64, usize>,

    /// We don't actually apply any edits in a transaction until CommitEdit() is called.
    /// This is a queue of edit requests that will get applied on commit.
    transaction_changes: Vec<txt::TextFieldRequest>,

    /// If there is an inflight transaction started with `TextField.BeginEdit()`, this
    /// contains the revision number specified in the `BeginEdit` call. If there is no
    /// inflight transaction, this is `None`.
    transaction_revision: Option<u64>,
}

/// A service that talks to a text field, providing it edits and cursor state updates
/// in response to user input.
#[derive(Clone)]
pub struct Ime(Arc<Mutex<ImeState>>);

impl Ime {
    pub fn new<I: 'static + uii::InputMethodEditorClientProxyInterface>(
        keyboard_type: uii::KeyboardType,
        action: uii::InputMethodAction,
        initial_state: uii::TextInputState,
        client: I,
        ime_service: ImeService,
    ) -> Ime {
        let state = ImeState {
            text_state: initial_state,
            client: Box::new(client),
            keyboard_type,
            action,
            ime_service,
            revision: 0,
            next_text_point_id: 0,
            text_points: HashMap::new(),
            input_method: None,
            transaction_changes: Vec::new(),
            transaction_revision: None,
        };
        Ime(Arc::new(Mutex::new(state)))
    }

    pub fn downgrade(&self) -> Weak<Mutex<ImeState>> {
        Arc::downgrade(&self.0)
    }

    pub fn upgrade(weak: &Weak<Mutex<ImeState>>) -> Option<Ime> {
        weak.upgrade().map(|arc| Ime(arc))
    }

    pub fn bind_text_field(&self, mut stream: txt::TextFieldRequestStream) {
        let control_handle = stream.control_handle();
        {
            let mut state = self.0.lock();
            let res = control_handle.send_on_update(&mut state.as_text_field_state());
            if let Err(e) = res {
                fx_log_err!("{}", e);
            } else {
                state.input_method = Some(control_handle);
            }
        }
        let mut self_clone = self.clone();
        fuchsia_async::spawn(
            async move {
                while let Some(msg) = await!(stream.try_next())
                    .context("error reading value from text field request stream")?
                {
                    if let Err(e) = self_clone.handle_text_field_msg(msg) {
                        fx_log_err!("error when replying to TextFieldRequest: {}", e);
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
        );
    }

    pub fn bind_ime(&self, chan: fuchsia_async::Channel) {
        let self_clone = self.clone();
        let self_clone_2 = self.clone();
        fuchsia_async::spawn(
            async move {
                let mut stream = uii::InputMethodEditorRequestStream::from_channel(chan);
                while let Some(msg) = await!(stream.try_next())
                    .context("error reading value from IME request stream")?
                {
                    self_clone.handle_ime_message(msg);
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e))
                .then(async move |()| {
                    // this runs when IME stream closes
                    // clone to ensure we only hold one lock at a time
                    let ime_service = self_clone_2.0.lock().ime_service.clone();
                    ime_service.update_keyboard_visibility_from_ime(&self_clone_2.0, false);
                }),
        );
    }

    /// Handles a TextFieldRequest, returning a FIDL error if one occurred when sending a reply.
    fn handle_text_field_msg(&mut self, msg: txt::TextFieldRequest) -> Result<(), fidl::Error> {
        let mut ime_state = self.0.lock();
        match msg {
            txt::TextFieldRequest::PointOffset { old_point, offset, revision, responder } => {
                if revision != ime_state.revision {
                    return responder
                        .send(&mut txt::TextPoint { id: 0 }, txt::TextError::BadRevision);
                }
                let old_byte_index =
                    if let Some(old_byte_index) = get_point(&ime_state.text_points, &old_point) {
                        old_byte_index
                    } else {
                        return responder
                            .send(&mut txt::TextPoint { id: 0 }, txt::TextError::BadRequest);
                    };
                let char_to_byte = ime_state.char_to_byte();
                let old_char_index = if let Ok(v) = char_to_byte.binary_search(&old_byte_index) {
                    v
                } else {
                    fx_log_err!(
                        "was unable to find char index for byte index {:?} from {:?}",
                        &old_byte_index,
                        &old_point
                    );
                    return responder
                        .send(&mut txt::TextPoint { id: 0 }, txt::TextError::BadRequest);
                };
                let new_char_index =
                    (old_char_index as i64 + offset).max(0).min(char_to_byte.len() as i64 - 1)
                        as usize;
                let new_byte_index = char_to_byte[new_char_index];
                let mut new_point = ime_state.new_point(new_byte_index);
                return responder.send(&mut new_point, txt::TextError::Ok);
            }
            txt::TextFieldRequest::Distance { range, revision, responder } => {
                if revision != ime_state.revision {
                    return responder.send(0, txt::TextError::BadRevision);
                }
                match get_range(&ime_state.text_points, &range, false) {
                    Some((byte_start, byte_end)) => {
                        let char_to_byte = ime_state.char_to_byte();
                        let char_start = if let Ok(v) = char_to_byte.binary_search(&byte_start) {
                            v
                        } else {
                            fx_log_err!(
                                "was unable to find char index for byte index {:?} in range {:?}",
                                &byte_start,
                                &range
                            );
                            return responder.send(0, txt::TextError::BadRequest);
                        };
                        let char_end = if let Ok(v) = char_to_byte.binary_search(&byte_end) {
                            v
                        } else {
                            fx_log_err!(
                                "was unable to find char index for byte index {:?} in range {:?}",
                                &byte_end,
                                &range
                            );
                            return responder.send(0, txt::TextError::BadRequest);
                        };
                        return responder
                            .send(char_end as i64 - char_start as i64, txt::TextError::Ok);
                    }
                    None => {
                        return responder.send(0, txt::TextError::BadRequest);
                    }
                }
            }
            txt::TextFieldRequest::Contents { range, revision, responder } => {
                if revision != ime_state.revision {
                    return responder.send(
                        "",
                        &mut txt::TextPoint { id: 0 },
                        txt::TextError::BadRevision,
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
                                    txt::TextError::Ok,
                                );
                            }
                            None => {
                                return responder.send(
                                    "",
                                    &mut txt::TextPoint { id: 0 },
                                    txt::TextError::BadRequest,
                                );
                            }
                        }
                    }
                    None => {
                        return responder.send(
                            "",
                            &mut txt::TextPoint { id: 0 },
                            txt::TextError::BadRequest,
                        );
                    }
                }
            }
            txt::TextFieldRequest::BeginEdit { revision, .. } => {
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = Some(revision);
                return Ok(());
            }
            txt::TextFieldRequest::CommitEdit { responder, .. } => {
                if ime_state.transaction_revision != Some(ime_state.revision) {
                    return responder.send(txt::TextError::BadRevision);
                }
                let res = if ime_state.apply_transaction() {
                    ime_state.increment_revision(None, true);
                    responder.send(txt::TextError::Ok)
                } else {
                    responder.send(txt::TextError::BadRequest)
                };
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = None;
                return res;
            }
            txt::TextFieldRequest::AbortEdit { .. } => {
                ime_state.transaction_changes = Vec::new();
                ime_state.transaction_revision = None;
                return Ok(());
            }
            req @ txt::TextFieldRequest::Replace { .. }
            | req @ txt::TextFieldRequest::SetSelection { .. }
            | req @ txt::TextFieldRequest::SetComposition { .. }
            | req @ txt::TextFieldRequest::ClearComposition { .. }
            | req @ txt::TextFieldRequest::SetDeadKeyHighlight { .. }
            | req @ txt::TextFieldRequest::ClearDeadKeyHighlight { .. } => {
                if ime_state.transaction_revision.is_some() {
                    ime_state.transaction_changes.push(req)
                }
                return Ok(());
            }
        }
    }

    /// Handles a request from the legancy IME API, an InputMethodEditorRequest.
    fn handle_ime_message(&self, msg: uii::InputMethodEditorRequest) {
        match msg {
            ImeReq::SetKeyboardType { keyboard_type, .. } => {
                let mut state = self.0.lock();
                state.keyboard_type = keyboard_type;
            }
            ImeReq::SetState { state, .. } => {
                self.set_state(state);
            }
            ImeReq::InjectInput { event, .. } => {
                self.inject_input(event);
            }
            ImeReq::Show { .. } => {
                // clone to ensure we only hold one lock at a time
                let ime_service = self.0.lock().ime_service.clone();
                ime_service.show_keyboard();
            }
            ImeReq::Hide { .. } => {
                // clone to ensure we only hold one lock at a time
                let ime_service = self.0.lock().ime_service.clone();
                ime_service.hide_keyboard();
            }
        }
    }

    fn set_state(&self, input_state: uii::TextInputState) {
        let mut state = self.0.lock();
        state.text_state = input_state;
        // the old C++ IME implementation didn't call did_update_state here, so this second argument is false.
        state.increment_revision(None, false);
    }

    pub fn inject_input(&self, event: uii::InputEvent) {
        let mut state = self.0.lock();
        let keyboard_event = match event {
            uii::InputEvent::Keyboard(e) => e,
            _ => return,
        };

        if keyboard_event.phase == uii::KeyboardEventPhase::Pressed
            || keyboard_event.phase == uii::KeyboardEventPhase::Repeat
        {
            if keyboard_event.code_point != 0 {
                state.type_keycode(keyboard_event.code_point);
                state.increment_revision(Some(keyboard_event), true)
            } else {
                match keyboard_event.hid_usage {
                    HID_USAGE_KEY_BACKSPACE => {
                        state.delete_backward();
                        state.increment_revision(Some(keyboard_event), true);
                    }
                    HID_USAGE_KEY_DELETE => {
                        state.delete_forward();
                        state.increment_revision(Some(keyboard_event), true);
                    }
                    HID_USAGE_KEY_LEFT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, false);
                        state.increment_revision(Some(keyboard_event), true);
                    }
                    HID_USAGE_KEY_RIGHT => {
                        state.cursor_horizontal_move(keyboard_event.modifiers, true);
                        state.increment_revision(Some(keyboard_event), true);
                    }
                    HID_USAGE_KEY_ENTER => {
                        state.client.on_action(state.action).unwrap_or_else(|e| {
                            fx_log_warn!("error sending action to ImeClient: {:?}", e)
                        });
                    }
                    _ => {
                        // Not an editing key, forward the event to clients.
                        state.increment_revision(Some(keyboard_event), true);
                    }
                }
            }
        }
    }
}

/// Horizontal motion type for the cursor.
enum HorizontalMotion {
    GraphemeLeft(GraphemeTraversal),
    GraphemeRight,
    WordLeft,
    WordRight,
}

/// How the cursor should traverse grapheme clusters.
enum GraphemeTraversal {
    /// Move by whole grapheme clusters at a time.
    ///
    /// This traversal mode should be used when using arrow keys, or when deleting forward (with the
    /// <kbd>Delete</kbd> key).
    WholeGrapheme,
    /// Generally move by whole grapheme clusters, but allow moving through individual combining
    /// characters, if present at the end of the grapheme cluster.
    ///
    /// This traversal mode should be used when deleting backward (<kbd>Backspace</kbd>), but not
    /// when deleting forward or using arrow keys.
    ///
    /// This ensures that when a user is typing text and composes a character out of individual
    /// combining diacritics, it should be possible to correct a mistake by pressing
    /// <kbd>Backspace</kbd>. If we were to allow _moving the cursor_ left and right through
    /// diacritics, that would only cause user confusion, as the blinking caret would not move
    /// visibly while within a single grapheme cluster.
    CombiningCharacters,
}

/// Looks up a TextPoint's byte index from a list of points. Usually this list will be
/// `ImeState.text_points`, but in the middle of a transaction, we clone it to a temporary list
/// so that we can mutate them without mutating the original list. That way, if the transaction
/// gets rejected, the original list is left intact.
fn get_point(point_list: &HashMap<u64, usize>, point: &txt::TextPoint) -> Option<usize> {
    point_list.get(&point.id).cloned()
}

/// Looks up a TextRange's byte indices from a list of points. If `fix_inversion` is true, we
/// also will sort the result so that start <= end. You almost always want to sort the result,
/// although sometimes you want to know if the range was inverted. The Distance function, for
/// instance, returns a negative result if the range given was inverted.
fn get_range(
    point_list: &HashMap<u64, usize>,
    range: &txt::TextRange,
    fix_inversion: bool,
) -> Option<(usize, usize)> {
    match (get_point(point_list, &range.start), get_point(point_list, &range.end)) {
        (Some(a), Some(b)) if a >= b && fix_inversion => Some((b, a)),
        (Some(a), Some(b)) => Some((a, b)),
        _ => None,
    }
}

impl ImeState {
    /// Any time the state is updated, this method is called, which allows ImeState to inform any
    /// listening clients (either TextField or InputMethodEditorClientProxy) that state has updated.
    /// If InputMethodEditorClient caused the update with SetState, set call_did_update_state so that
    /// we don't send its own edit back to it. Otherwise, set to true.
    pub fn increment_revision(
        &mut self,
        e: Option<uii::KeyboardEvent>,
        call_did_update_state: bool,
    ) {
        self.revision += 1;
        self.text_points = HashMap::new();
        let mut state = self.as_text_field_state();
        if let Some(input_method) = &self.input_method {
            if let Err(e) = input_method.send_on_update(&mut state) {
                fx_log_err!("error when sending update to TextField listener: {}", e);
            }
        }

        if call_did_update_state {
            if let Some(ev) = e {
                self.client
                    .did_update_state(
                        &mut self.text_state,
                        Some(OutOfLine(&mut uii::InputEvent::Keyboard(ev))),
                    )
                    .unwrap_or_else(|e| {
                        fx_log_warn!("error sending state update to ImeClient: {:?}", e)
                    });
            } else {
                self.client.did_update_state(&mut self.text_state, None).unwrap_or_else(|e| {
                    fx_log_warn!("error sending state update to ImeClient: {:?}", e)
                });
            }
        }
    }

    /// Generates a map of char indices to byte indices for the current text. Definitely would not be ~very performant
    /// on large strings, but this is on legacy state objects, which crash if they get larger than the max FIDL message
    /// size anyway.
    fn char_to_byte(&self) -> Vec<usize> {
        let mut char_to_byte: Vec<usize> =
            self.text_state.text.char_indices().map(|(i, _)| i).collect();
        char_to_byte.push(self.text_state.text.len()); // Add final valid code point position at end of string
        char_to_byte
    }

    /// Converts the current self.text_state (the IME API v1 representation of the text field's state)
    /// into the v2 representation txt::TextFieldState.
    fn as_text_field_state(&mut self) -> txt::TextFieldState {
        let anchor_first = self.text_state.selection.base < self.text_state.selection.extent;
        let composition = if self.text_state.composing.start < 0
            || self.text_state.composing.end < 0
        {
            None
        } else {
            let start = self.new_point(self.text_state.composing.start as usize);
            let end = self.new_point(self.text_state.composing.end as usize);
            let text_range = if self.text_state.composing.start < self.text_state.composing.end {
                txt::TextRange { start, end }
            } else {
                txt::TextRange { start: end, end: start }
            };
            Some(Box::new(text_range))
        };
        let selection =
            if self.text_state.selection.base < 0 || self.text_state.selection.extent < 0 {
                None
            } else {
                Some(Box::new(txt::TextSelection {
                    range: txt::TextRange {
                        start: self.new_point(if anchor_first {
                            self.text_state.selection.base as usize
                        } else {
                            self.text_state.selection.extent as usize
                        }),
                        end: self.new_point(if anchor_first {
                            self.text_state.selection.extent as usize
                        } else {
                            self.text_state.selection.base as usize
                        }),
                    },
                    anchor: if anchor_first {
                        txt::TextSelectionAnchor::AnchoredAtStart
                    } else {
                        txt::TextSelectionAnchor::AnchoredAtEnd
                    },
                    affinity: txt::TextAffinity::Upstream,
                }))
            };
        txt::TextFieldState {
            document: txt::TextRange {
                start: self.new_point(0),
                end: self.new_point(self.text_state.text.len()),
            },
            selection,
            composition,
            // unfortunately, since the old API doesn't support these, we have to set highlights to None.
            composition_highlight: None,
            dead_key_highlight: None,
            revision: self.revision,
        }
    }

    /// Creates a new TextPoint corresponding to the byte index `index`.
    fn new_point(&mut self, index: usize) -> txt::TextPoint {
        let id = self.next_text_point_id;
        self.next_text_point_id += 1;
        self.text_points.insert(id, index);
        txt::TextPoint { id }
    }

    // gets start and len, and sets base/extent to start of string if don't exist
    pub fn selection(&mut self) -> Range<usize> {
        let s = &mut self.text_state.selection;
        s.base = s.base.max(0).min(self.text_state.text.len() as i64);
        s.extent = s.extent.max(0).min(self.text_state.text.len() as i64);
        let start = s.base.min(s.extent) as usize;
        let end = s.base.max(s.extent) as usize;
        (start..end)
    }

    /// Return bool indicates if transaction was successful and valid
    fn apply_transaction(&mut self) -> bool {
        let mut moved_points = self.text_points.clone();
        let mut new_state = clone_state(&self.text_state);
        for edit in &self.transaction_changes {
            match edit {
                txt::TextFieldRequest::Replace { range, new_text, .. } => {
                    let (start, end) = match get_range(&moved_points, &range, true) {
                        Some(v) => v,
                        None => return false,
                    };
                    let first_half = if let Some(v) = new_state.text.get(..start) {
                        v
                    } else {
                        fx_log_err!(
                            "IME: out of bounds string request for range (..{}) on string {:?}",
                            start,
                            new_state.text
                        );
                        return false;
                    };
                    let second_half = if let Some(v) = new_state.text.get(end..) {
                        v
                    } else {
                        fx_log_err!(
                            "IME: out of bounds string request for range ({}..) on string {:?}",
                            end,
                            new_state.text
                        );
                        return false;
                    };
                    new_state.text = format!("{}{}{}", first_half, new_text, second_half);

                    // adjust char index of points after the insert
                    let delete_len = end as i64 - start as i64;
                    let insert_len = new_text.len() as i64;
                    for (_, byte_index) in moved_points.iter_mut() {
                        if start < *byte_index && *byte_index <= end {
                            *byte_index = start + insert_len as usize;
                        } else if end < *byte_index {
                            *byte_index = (*byte_index as i64 - delete_len as i64
                                + insert_len as i64)
                                as usize;
                        }
                    }

                    adjust_range(
                        start as i64,
                        end as i64,
                        new_text.len() as i64,
                        &mut new_state.selection.extent,
                        &mut new_state.selection.base,
                    );
                    if new_state.composing.start >= 0 && new_state.composing.end >= 0 {
                        adjust_range(
                            start as i64,
                            end as i64,
                            new_text.len() as i64,
                            &mut new_state.composing.start,
                            &mut new_state.composing.end,
                        );
                    }
                }
                txt::TextFieldRequest::SetSelection { selection, .. } => {
                    let (start, end) = match get_range(&moved_points, &selection.range, false) {
                        Some(v) => v,
                        None => return false,
                    };

                    if selection.anchor == txt::TextSelectionAnchor::AnchoredAtStart {
                        new_state.selection.base = start as i64;
                        new_state.selection.extent = end as i64;
                    } else {
                        new_state.selection.extent = start as i64;
                        new_state.selection.base = end as i64;
                    }
                }
                txt::TextFieldRequest::SetComposition { composition_range, .. } => {
                    let (start, end) = match get_range(&moved_points, &composition_range, true) {
                        Some(v) => v,
                        None => return false,
                    };
                    new_state.composing.start = start as i64;
                    new_state.composing.end = end as i64;
                }
                txt::TextFieldRequest::ClearComposition { .. } => {
                    new_state.composing.start = -1;
                    new_state.composing.end = -1;
                }
                _ => {
                    fx_log_warn!("the input method tried to an unsupported TextFieldRequest on this proxy to InputMethodEditorClient");
                }
            }
        }

        self.text_state = new_state;
        true
    }

    pub fn type_keycode(&mut self, code_point: u32) {
        self.text_state.revision += 1;

        let replacement = match char::from_u32(code_point) {
            Some(v) => v.to_string(),
            None => return,
        };

        let selection = self.selection();
        self.text_state.text.replace_range(selection.clone(), &replacement);

        self.text_state.selection.base = selection.start as i64 + replacement.len() as i64;
        self.text_state.selection.extent = self.text_state.selection.base;
    }

    /// Calculates an adjacent cursor position to left or right of the current position.
    ///
    /// * `start`: Starting position in the string, as a byte offset.
    /// * `motion`: Whether to go right or left, and whether to allow entering grapheme clusters.
    fn adjacent_cursor_position(&self, start: usize, motion: HorizontalMotion) -> usize {
        match motion {
            HorizontalMotion::GraphemeRight => self.adjacent_cursor_position_grapheme_right(start),
            HorizontalMotion::GraphemeLeft(traversal) => {
                self.adjacent_cursor_position_grapheme_left(start, traversal)
            }
            HorizontalMotion::WordLeft => self.adjacent_cursor_position_word_left(start),
            HorizontalMotion::WordRight => self.adjacent_cursor_position_word_right(start),
        }
    }

    fn adjacent_cursor_position_word_left(&self, start: usize) -> usize {
        if start == 0 {
            return 0;
        }
        let text = &self.text_state.text[0..start];
        // Find the next word to the left.
        let word = match UnicodeSegmentation::unicode_words(text).rev().next() {
            Some(word) => word,
            // No words - go to the string start.
            None => return 0,
        };
        // Find start of the next word.
        if let Some((pos, _)) = UnicodeSegmentation::split_word_bound_indices(text)
            .rev()
            .find(|(_, next_word)| next_word == &word)
        {
            pos
        } else {
            0
        }
    }

    fn adjacent_cursor_position_word_right(&self, start: usize) -> usize {
        let text = &self.text_state.text[start..];
        let text_length = text.len();
        if text_length == 0 {
            return start;
        }
        let mut words_iter = UnicodeSegmentation::unicode_words(text);
        // Find the next word to the right.
        let word = match words_iter.next() {
            Some(word) => word,
            // No words - go the end of the string.
            None => return start + text_length,
        };
        let mut word_bound_indices = UnicodeSegmentation::split_word_bound_indices(text);
        // Skip over boundaries until a next word is found.
        let word_bound = word_bound_indices.find(|(_, next_word)| next_word == &word);

        // Return start of the next boundary after the word, if there is one.
        if let Some((next_boundary_pos, _)) = word_bound_indices.next() {
            start + next_boundary_pos
        } else if let Some((pos, next_word)) = word_bound {
            // Last word - go to end of the word.
            start + pos + next_word.len()
        } else {
            // No more words - go to the end of the line.
            start + text_length
        }
    }

    fn get_grapheme_boundary(&self, start: usize, next: bool) -> Option<usize> {
        let text_length = self.text_state.text.len();
        let mut cursor = GraphemeCursor::new(start, text_length, true);
        let result = if next {
            cursor.next_boundary(&self.text_state.text, 0)
        } else {
            cursor.prev_boundary(&self.text_state.text, 0)
        };
        result.unwrap_or(None)
    }

    fn adjacent_cursor_position_grapheme_right(&self, start: usize) -> usize {
        self.get_grapheme_boundary(start, true).unwrap_or(self.text_state.text.len())
    }

    fn adjacent_cursor_position_grapheme_left(
        &self,
        start: usize,
        traversal: GraphemeTraversal,
    ) -> usize {
        let prev_boundary = self.get_grapheme_boundary(start, false);
        if let Some(offset) = prev_boundary {
            if let GraphemeTraversal::CombiningCharacters = traversal {
                let grapheme_str = &self.text_state.text[offset..start];
                let last_char_str = match grapheme_str.char_indices().last() {
                    Some((last_char_offset, _c)) => Some(&grapheme_str[last_char_offset..]),
                    None => None,
                };
                if let Some(last_char_str) = last_char_str {
                    lazy_static! {
                        /// A regex that matches combining characters, e.g. accents and other
                        /// diacritics. Rust does not provide a way to check the Unicode categories
                        /// of `char`s directly, so this is the simplest workaround for now.
                        static ref COMBINING_REGEX: Regex = Regex::new(r"\p{M}$").unwrap();
                    }
                    if COMBINING_REGEX.is_match(last_char_str) {
                        return start - last_char_str.len();
                    }
                }
            }
            offset
        } else {
            // Can't go left from the beginning of the string.
            0
        }
    }

    pub fn delete_backward(&mut self) {
        self.text_state.revision += 1;

        // set base and extent to 0 if either is -1, to ensure there is a selection/cursor
        self.selection();

        if self.text_state.selection.base == self.text_state.selection.extent {
            // Select one grapheme or character to the left, so that it can be uniformly handled by
            // the selection-deletion code below.
            self.text_state.selection.base = self.adjacent_cursor_position(
                self.text_state.selection.base as usize,
                HorizontalMotion::GraphemeLeft(GraphemeTraversal::CombiningCharacters),
            ) as i64;
        }
        self.delete_selection();
    }

    pub fn delete_forward(&mut self) {
        self.text_state.revision += 1;

        // Ensure valid selection/cursor.
        self.selection();

        if self.text_state.selection.base == self.text_state.selection.extent {
            // Select one grapheme to the right so that it can be handled by the selection-deletion
            // code below.
            self.text_state.selection.extent = self.adjacent_cursor_position(
                self.text_state.selection.base as usize,
                HorizontalMotion::GraphemeRight,
            ) as i64;
        }
        self.delete_selection();
    }

    /// Deletes the selected text if the selection isn't empty.
    /// Does not increment revision number. Should only be called from methods that do.
    fn delete_selection(&mut self) {
        // Delete the current selection.
        let selection = self.selection();
        if selection.start != selection.end {
            self.text_state.text.replace_range(selection.clone(), "");
            self.text_state.selection.extent = selection.start as i64;
            self.text_state.selection.base = self.text_state.selection.extent;
        }
    }

    pub fn cursor_horizontal_move(&mut self, modifiers: u32, go_right: bool) {
        self.text_state.revision += 1;

        let shift_pressed = modifiers & uii::MODIFIER_SHIFT != 0;
        let ctrl_pressed = modifiers & uii::MODIFIER_CONTROL != 0;
        let selection = self.selection();
        let text_is_selected = selection.start != selection.end;
        let mut new_position = self.text_state.selection.extent;

        if !shift_pressed && text_is_selected {
            // canceling selection, new position based on start/end of selection
            if go_right {
                new_position = selection.end as i64;
            } else {
                new_position = selection.start as i64;
            }
            if ctrl_pressed {
                new_position = self.adjacent_cursor_position(
                    new_position as usize,
                    if go_right { HorizontalMotion::WordRight } else { HorizontalMotion::WordLeft },
                ) as i64;
            }
        } else {
            // new position based previous value of extent
            new_position = self.adjacent_cursor_position(
                new_position as usize,
                match (go_right, ctrl_pressed) {
                    (true, true) => HorizontalMotion::WordRight,
                    (false, true) => HorizontalMotion::WordLeft,
                    (true, false) => HorizontalMotion::GraphemeRight,
                    (false, false) => {
                        HorizontalMotion::GraphemeLeft(GraphemeTraversal::WholeGrapheme)
                    }
                },
            ) as i64;
        }

        self.text_state.selection.extent = new_position;
        if !shift_pressed {
            self.text_state.selection.base = new_position;
        }
        self.text_state.selection.affinity = uii::TextAffinity::Downstream;
    }
}

/// This function modifies a range in response to an edit.
fn adjust_range(
    edit_start: i64,
    edit_end: i64,
    insert_len: i64,
    range_a: &mut i64,
    range_b: &mut i64,
) {
    let (range_start, range_end) =
        if *range_a < *range_b { (*range_a, *range_b) } else { (*range_b, *range_a) };
    if edit_end <= range_start {
        // if replace happened strictly before selection, push selection forwards
        *range_b += insert_len + edit_start - edit_end;
        *range_a += insert_len + edit_start - edit_end;
    } else if edit_start < range_end {
        // if our replace intersected with the selection at all, set the selection to the end of the insert
        *range_b = edit_start + insert_len;
        *range_a = edit_start + insert_len;
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fidl_helpers::default_state;
    use fidl;
    use fuchsia_zircon as zx;
    use std::sync::mpsc::{channel, Receiver, Sender};

    fn set_up(
        text: &str,
        base: i64,
        extent: i64,
    ) -> (Ime, Receiver<uii::TextInputState>, Receiver<uii::InputMethodAction>) {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut state = default_state();
        state.text = text.to_string();
        state.selection.base = base;
        state.selection.extent = extent;
        let ime = Ime::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            state,
            client,
            ImeService::new(),
        );
        (ime, statechan, actionchan)
    }

    fn simulate_keypress<K: Into<u32> + Copy>(
        ime: &mut Ime,
        key: K,
        hid_key: bool,
        modifiers: u32,
    ) {
        let hid_usage = if hid_key { key.into() } else { 0 };
        let code_point = if hid_key { 0 } else { key.into() };
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Pressed,
            hid_usage,
            code_point,
            modifiers,
        }));
        ime.inject_input(uii::InputEvent::Keyboard(uii::KeyboardEvent {
            event_time: 0,
            device_id: 0,
            phase: uii::KeyboardEventPhase::Released,
            hid_usage,
            code_point,
            modifiers,
        }));
    }

    struct MockImeClient {
        pub state: Mutex<Sender<uii::TextInputState>>,
        pub action: Mutex<Sender<uii::InputMethodAction>>,
    }
    impl MockImeClient {
        fn new() -> (MockImeClient, Receiver<uii::TextInputState>, Receiver<uii::InputMethodAction>)
        {
            let (s_send, s_rec) = channel();
            let (a_send, a_rec) = channel();
            let client = MockImeClient { state: Mutex::new(s_send), action: Mutex::new(a_send) };
            (client, s_rec, a_rec)
        }
    }
    impl uii::InputMethodEditorClientProxyInterface for MockImeClient {
        fn did_update_state(
            &self,
            state: &mut uii::TextInputState,
            mut _event: Option<fidl::encoding::OutOfLine<uii::InputEvent>>,
        ) -> Result<(), fidl::Error> {
            let state2 = clone_state(state);
            self.state
                .lock()
                .send(state2)
                .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        }
        fn on_action(&self, action: uii::InputMethodAction) -> Result<(), fidl::Error> {
            self.action
                .lock()
                .send(action)
                .map_err(|_| fidl::Error::ClientWrite(zx::Status::PEER_CLOSED))
        }
    }

    #[test]
    fn test_mock_ime_channels() {
        let (client, statechan, actionchan) = MockImeClient::new();
        let mut ime = Ime::new(
            uii::KeyboardType::Text,
            uii::InputMethodAction::Search,
            default_state(),
            client,
            ImeService::new(),
        );
        assert_eq!(true, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
        simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE);
        assert_eq!(false, statechan.try_recv().is_err());
        assert_eq!(true, actionchan.try_recv().is_err());
    }

    #[test]
    fn test_delete_backward_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        // a second delete still does nothing, but increments revision
        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        // a second delete still does nothing, but increments revision
        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_beginning_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_beginning_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 0);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("bcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_first_char_selected() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 0, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("bcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_last_char_selected() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 8, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_end_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_end_string() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_combining_diacritic() {
        // U+0301: combining acute accent. 2 bytes.
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi\u{0301}", 11, 11);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_combining_diacritic() {
        // U+0301: combining acute accent. 2 bytes.
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi\u{0301}jkl", 8, 8);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghjkl", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_backward_emoji() {
        // Emoji with a color modifier.
        let text = "abcdefghi👦🏻";
        let len = text.len() as i64;
        let (mut ime, statechan, _actionchan) = set_up(text, len, len);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_emoji() {
        // Emoji with a color modifier.
        let text = "abcdefghi👦🏻";
        let (mut ime, statechan, _actionchan) = set_up(text, 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    /// Flags are more complicated because they consist of two REGIONAL INDICATOR SYMBOL LETTERs.
    #[test]
    fn test_delete_backward_flag() {
        // French flag
        let text = "abcdefghi🇫🇷";
        let len = text.len() as i64;
        let (mut ime, statechan, _actionchan) = set_up(text, len, len);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_delete_forward_flag() {
        // French flag
        let text = "abcdefghi🇫🇷";
        let (mut ime, statechan, _actionchan) = set_up(text, 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_DELETE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_delete_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 6);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_inverted() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcghi", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_delete_no_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", -1, -1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefghi", state.text);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abdefghi", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_delete_with_zero_width_selection_at_end() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 9, 9);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_delete_selection_out_of_bounds() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 20, 24);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abcdefgh", state.text);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 1, 5);

        // right with shift
        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_SHIFT);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(6, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_left_on_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 6, 3);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_cursor_right_on_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdefghi", 3, 9);

        // left with shift
        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_SHIFT);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(8, state.selection.base);
        assert_eq!(8, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(9, state.selection.base);
        assert_eq!(9, state.selection.extent);
    }

    #[test]
    fn test_cursor_word_left_no_words() {
        let (mut ime, statechan, _actionchan) = set_up("¿ - _ - ?", 5, 5);

        // left with control
        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_word_left() {
        let (mut ime, statechan, _actionchan) = set_up("4.2 . foobar", 7, 7);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(6, state.selection.base);
        assert_eq!(6, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_word_right_no_words() {
        let (mut ime, statechan, _actionchan) = set_up("¿ - _ - ?", 5, 5);

        // right with control
        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(10, state.selection.base);
        assert_eq!(10, state.selection.extent);
    }

    #[test]
    fn test_cursor_word_right() {
        let (mut ime, statechan, _actionchan) = set_up("4.2 . foobar", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(12, state.selection.base);
        assert_eq!(12, state.selection.extent);

        // Try to navigate off text limits.
        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(12, state.selection.base);
        assert_eq!(12, state.selection.extent);
    }

    #[test]
    fn test_cursor_word_off_limits() {
        let (mut ime, statechan, _actionchan) = set_up("word", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(4, state.selection.base);
        assert_eq!(4, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(4, state.selection.base);
        assert_eq!(4, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(0, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_cursor_word() {
        let (mut ime, statechan, _actionchan) = set_up("a.c   2.2 ¿? x yz", 8, 8);

        simulate_keypress(
            &mut ime,
            HID_USAGE_KEY_RIGHT,
            true,
            uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
        );
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!(8, state.selection.base);
        assert_eq!(9, state.selection.extent);

        simulate_keypress(
            &mut ime,
            HID_USAGE_KEY_RIGHT,
            true,
            uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
        );
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!(8, state.selection.base);
        assert_eq!(15, state.selection.extent);

        simulate_keypress(&mut ime, HID_USAGE_KEY_LEFT, true, uii::MODIFIER_CONTROL);
        let state = statechan.try_recv().unwrap();
        assert_eq!(4, state.revision);
        assert_eq!(6, state.selection.base);
        assert_eq!(6, state.selection.extent);

        simulate_keypress(
            &mut ime,
            HID_USAGE_KEY_LEFT,
            true,
            uii::MODIFIER_CONTROL | uii::MODIFIER_SHIFT,
        );
        let state = statechan.try_recv().unwrap();
        assert_eq!(5, state.revision);
        assert_eq!(6, state.selection.base);
        assert_eq!(0, state.selection.extent);
    }

    #[test]
    fn test_type_empty_string() {
        let (mut ime, statechan, _actionchan) = set_up("", 0, 0);

        simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("a", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("ab", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_at_beginning() {
        let (mut ime, statechan, _actionchan) = set_up("cde", 0, 0);

        simulate_keypress(&mut ime, 'a', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("acde", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);

        simulate_keypress(&mut ime, 'b', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("abcde", state.text);
        assert_eq!(2, state.selection.base);
        assert_eq!(2, state.selection.extent);
    }

    #[test]
    fn test_type_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 2, 5);

        simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_inverted_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 5, 2);

        simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("abxf", state.text);
        assert_eq!(3, state.selection.base);
        assert_eq!(3, state.selection.extent);
    }

    #[test]
    fn test_type_invalid_selection() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", -10, 1);

        simulate_keypress(&mut ime, 'x', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("xbcdef", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    #[test]
    fn test_set_state() {
        let (mut ime, statechan, _actionchan) = set_up("abcdef", 1, 1);

        let mut override_state = default_state();
        override_state.text = "meow?".to_string();
        override_state.selection.base = 4;
        override_state.selection.extent = 5;
        ime.set_state(override_state);
        simulate_keypress(&mut ime, '!', false, uii::MODIFIER_NONE);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow!", state.text);
        assert_eq!(5, state.selection.base);
        assert_eq!(5, state.selection.extent);
    }

    #[test]
    fn test_action() {
        let (mut ime, statechan, actionchan) = set_up("abcdef", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_ENTER, true, uii::MODIFIER_NONE);
        assert!(statechan.try_recv().is_err()); // assert did not update state
        assert!(actionchan.try_recv().is_ok()); // assert DID send action
    }

    #[test]
    fn test_unicode_selection() {
        let (mut ime, statechan, _actionchan) = set_up("m😸eow", 1, 1);

        simulate_keypress(&mut ime, HID_USAGE_KEY_RIGHT, true, uii::MODIFIER_SHIFT);
        assert!(statechan.try_recv().is_ok());

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_SHIFT);
        let state = statechan.try_recv().unwrap();
        assert_eq!(3, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }

    #[test]
    fn test_unicode_backspace() {
        let base: i64 = "m😸".len() as i64;
        let (mut ime, statechan, _actionchan) = set_up("m😸eow", base, base);

        simulate_keypress(&mut ime, HID_USAGE_KEY_BACKSPACE, true, uii::MODIFIER_SHIFT);
        let state = statechan.try_recv().unwrap();
        assert_eq!(2, state.revision);
        assert_eq!("meow", state.text);
        assert_eq!(1, state.selection.base);
        assert_eq!(1, state.selection.extent);
    }
}
