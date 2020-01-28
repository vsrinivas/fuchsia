// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an implementation of `ImeState`, the internal state object of `LegacyIme`.

use super::position;
use crate::fidl_helpers::clone_state;
use crate::ime_service::ImeService;
use crate::index_convert as idx;
use fidl_fuchsia_ui_input as uii;
use fidl_fuchsia_ui_text as txt;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use std::char;
use std::collections::HashMap;
use std::ops::Range;
use text::text_field_state::TextFieldState;

/// The internal state of the IME, held within `LegacyIme` inside `Arc<Mutex<ImeState>>`, so it can
/// be accessed from multiple message handler async tasks. Methods that aren't message handlers
/// don't usually need to access `Arc<Mutex<LegacyIme>>` itself, and so can be put in here instead
/// of on `LegacyIme` itself.
pub struct ImeState {
    pub text_state: uii::TextInputState,

    /// A handle to call methods on the text field.
    pub client: Box<dyn uii::InputMethodEditorClientProxyInterface>,

    pub keyboard_type: uii::KeyboardType,

    pub action: uii::InputMethodAction,
    pub ime_service: ImeService,

    /// We expose a TextField interface to an input method. There are also legacy
    /// input methods that just send key events through inject_input — in this case,
    /// input_method would be None, and these events would be handled by the
    /// inject_input method. ImeState can only handle talking to one input_method
    /// at a time; it's the responsibility of some other code (likely inside
    /// ImeService) to multiplex multiple TextField interfaces into this one.
    pub input_method: Option<txt::TextFieldControlHandle>,

    /// A number used to serve the TextField interface. It increments any time any
    /// party makes a change to the state.
    pub revision: u64,

    /// A TextPoint is a u64 token that represents a character position in the new
    /// TextField interface. Each token is a unique ID; this represents the ID we
    /// will assign to the next TextPoint that is created. It increments every time
    /// a TextPoint is created. This is never reset, even when TextPoints are
    /// invalidated; TextPoints have globally unique IDs.
    pub next_text_point_id: u64,

    /// A TextPoint is a u64 token that represents a character position in the new
    /// TextField interface. This maps TextPoint IDs to byte indexes inside
    /// `text_state.text`. When a new revision is created, all preexisting TextPoints
    /// are deleted, which means we clear this out.
    pub text_points: HashMap<u64, usize>,

    /// We don't actually apply any edits in a transaction until CommitEdit() is called.
    /// This is a queue of edit requests that will get applied on commit.
    pub transaction_changes: Vec<txt::TextFieldRequest>,

    /// If there is an inflight transaction started with `TextField.BeginEdit()`, this
    /// contains the revision number specified in the `BeginEdit` call. If there is no
    /// inflight transaction, this is `None`.
    pub transaction_revision: Option<u64>,
}

/// Looks up a TextPoint's byte index from a list of points. Usually this list will be
/// `ImeState.text_points`, but in the middle of a transaction, we clone it to a temporary list
/// so that we can mutate them without mutating the original list. That way, if the transaction
/// gets rejected, the original list is left intact.
pub fn get_point(point_list: &HashMap<u64, usize>, point: &txt::Position) -> Option<usize> {
    point_list.get(&point.id).cloned()
}

/// Looks up a TextRange's byte indices from a list of points. If `fix_inversion` is true, we
/// also will sort the result so that start <= end. You almost always want to sort the result,
/// although sometimes you want to know if the range was inverted. The Distance function, for
/// instance, returns a negative result if the range given was inverted.
pub fn get_range(
    point_list: &HashMap<u64, usize>,
    range: &txt::Range,
    fix_inversion: bool,
) -> Option<(usize, usize)> {
    match (get_point(point_list, &range.start), get_point(point_list, &range.end)) {
        (Some(a), Some(b)) if a >= b && fix_inversion => Some((b, a)),
        (Some(a), Some(b)) => Some((a, b)),
        _ => None,
    }
}

impl ImeState {
    /// Forwards a keyboard event to any listening clients without changing the actual state of the
    /// IME at all.
    pub fn forward_event(&mut self, ev: uii::KeyboardEvent) {
        let mut state = idx::text_state_byte_to_codeunit(clone_state(&self.text_state));
        self.client
            .did_update_state(&mut state, Some(&mut uii::InputEvent::Keyboard(ev)))
            .unwrap_or_else(|e| fx_log_warn!("error sending state update to ImeClient: {:?}", e));
    }

    /// Any time the state is updated, this method is called, which allows ImeState to inform any
    /// listening clients (either TextField or InputMethodEditorClientProxy) that state has updated.
    /// If InputMethodEditorClient caused the update with SetState, set call_did_update_state so that
    /// we don't send its own edit back to it. Otherwise, set to true.
    pub fn increment_revision(&mut self, call_did_update_state: bool) {
        self.revision += 1;
        self.text_points = HashMap::new();
        let state = self.as_text_field_state();
        if let Some(input_method) = &self.input_method {
            if let Err(e) = input_method.send_on_update(state.into()) {
                fx_log_err!("error when sending update to TextField listener: {}", e);
            }
        }

        if call_did_update_state {
            let mut state = idx::text_state_byte_to_codeunit(clone_state(&self.text_state));
            self.client.did_update_state(&mut state, None).unwrap_or_else(|e| {
                fx_log_warn!("error sending state update to ImeClient: {:?}", e)
            });
        }
    }

    /// Converts the current self.text_state (the IME API v1 representation of the text field's state)
    /// into the v2 representation TextFieldState.
    pub fn as_text_field_state(&mut self) -> TextFieldState {
        let anchor_first = self.text_state.selection.base < self.text_state.selection.extent;
        let composition = if self.text_state.composing.start < 0
            || self.text_state.composing.end < 0
        {
            None
        } else {
            let start = self.new_point(self.text_state.composing.start as usize);
            let end = self.new_point(self.text_state.composing.end as usize);
            let text_range = if self.text_state.composing.start < self.text_state.composing.end {
                txt::Range { start, end }
            } else {
                txt::Range { start: end, end: start }
            };
            Some(text_range)
        };
        let selection = txt::Selection {
            range: txt::Range {
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
                txt::SelectionAnchor::AnchoredAtStart
            } else {
                txt::SelectionAnchor::AnchoredAtEnd
            },
            affinity: txt::Affinity::Upstream,
        };
        TextFieldState {
            document: txt::Range {
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
    pub fn new_point(&mut self, index: usize) -> txt::Position {
        let id = self.next_text_point_id;
        self.next_text_point_id += 1;
        self.text_points.insert(id, index);
        txt::Position { id }
    }

    // gets start and len, and sets base/extent to start of string if don't exist
    pub fn selection(&mut self) -> Range<usize> {
        let s = &mut self.text_state.selection;
        s.base = s.base.max(0).min(self.text_state.text.len() as i64);
        s.extent = s.extent.max(0).min(self.text_state.text.len() as i64);
        let start = s.base.min(s.extent) as usize;
        let end = s.base.max(s.extent) as usize;
        start..end
    }

    /// Return bool indicates if transaction was successful and valid
    pub fn apply_transaction(&mut self) -> bool {
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

                    if selection.anchor == txt::SelectionAnchor::AnchoredAtStart {
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

    pub fn delete_backward(&mut self) {
        self.text_state.revision += 1;

        // set base and extent to 0 if either is -1, to ensure there is a selection/cursor
        self.selection();

        if self.text_state.selection.base == self.text_state.selection.extent {
            // Select one grapheme or character to the left, so that it can be uniformly handled by
            // the selection-deletion code below.
            self.text_state.selection.base = position::adjacent_cursor_position(
                &self.text_state.text,
                self.text_state.selection.base as usize,
                position::HorizontalMotion::GraphemeLeft(
                    position::GraphemeTraversal::CombiningCharacters,
                ),
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
            self.text_state.selection.extent = position::adjacent_cursor_position(
                &self.text_state.text,
                self.text_state.selection.base as usize,
                position::HorizontalMotion::GraphemeRight,
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
                new_position = position::adjacent_cursor_position(
                    &self.text_state.text,
                    new_position as usize,
                    if go_right {
                        position::HorizontalMotion::WordRight
                    } else {
                        position::HorizontalMotion::WordLeft
                    },
                ) as i64;
            }
        } else {
            // new position based previous value of extent
            new_position = position::adjacent_cursor_position(
                &self.text_state.text,
                new_position as usize,
                match (go_right, ctrl_pressed) {
                    (true, true) => position::HorizontalMotion::WordRight,
                    (false, true) => position::HorizontalMotion::WordLeft,
                    (true, false) => position::HorizontalMotion::GraphemeRight,
                    (false, false) => position::HorizontalMotion::GraphemeLeft(
                        position::GraphemeTraversal::WholeGrapheme,
                    ),
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
