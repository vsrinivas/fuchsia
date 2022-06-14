// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an implementation of `ImeState`, the internal state object of `LegacyIme`.

use {
    anyhow::{Context as _, Error},
    core::convert::TryInto,
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as uii,
    fuchsia_syslog::fx_log_warn,
    std::{
        char,
        collections::{HashMap, HashSet},
        ops::Range,
    },
};

use super::position;
use crate::{index_convert as idx, keyboard::events::KeyEvent, text_manager::TextManager};

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
    pub text_manager: TextManager,

    /// Currently pressed keys.
    pub keys_pressed: HashSet<input::Key>,

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
}

/// Looks up a TextPoint's byte index from a list of points. Usually this list will be
/// `ImeState.text_points`, but in the middle of a transaction, we clone it to a temporary list
/// so that we can mutate them without mutating the original list. That way, if the transaction
/// gets rejected, the original list is left intact.
impl ImeState {
    /// Forwards a keyboard event to any listening clients without changing the actual state of the
    /// IME at all.
    pub(crate) fn forward_event(&mut self, key_event: KeyEvent) -> Result<(), Error> {
        let keyboard_event: uii::KeyboardEvent =
            key_event.try_into().context("error converting key event to keyboard event")?;

        let mut state = idx::text_state_byte_to_codeunit(self.text_state.clone());
        self.client
            .did_update_state(&mut state, Some(&mut uii::InputEvent::Keyboard(keyboard_event)))
            .with_context(|| {
                format!(
                    "ImeState::forward_event: error sending state update to ImeClient: {:?}",
                    &keyboard_event
                )
            })?;
        Ok(())
    }

    /// Any time the state is updated, this method is called, which allows ImeState to inform any
    /// listening clients (either TextField or InputMethodEditorClientProxy) that state has updated.
    /// If InputMethodEditorClient caused the update with SetState, set call_did_update_state so that
    /// we don't send its own edit back to it. Otherwise, set to true.
    pub fn increment_revision(&mut self, call_did_update_state: bool) {
        self.revision += 1;
        self.text_points = HashMap::new();

        if call_did_update_state {
            let mut state = idx::text_state_byte_to_codeunit(self.text_state.clone());
            self.client.did_update_state(&mut state, None).unwrap_or_else(|e| {
                fx_log_warn!(
                    "ImeState::increment_revision: error sending state update to ImeClient: {:?}",
                    e
                )
            });
        }
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
