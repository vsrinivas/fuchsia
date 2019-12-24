// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use core::convert::TryFrom;
use fidl_fuchsia_ui_text as txt;

/// A version of txt::TextFieldState that does not have mandatory fields wrapped in Options.
/// It also implements Clone.
pub struct TextFieldState {
    pub document: txt::Range,
    pub selection: txt::Selection,
    pub revision: u64,
    pub composition: Option<txt::Range>,
    pub composition_highlight: Option<txt::Range>,
    pub dead_key_highlight: Option<txt::Range>,
}

impl Clone for TextFieldState {
    fn clone(&self) -> Self {
        TextFieldState {
            document: clone_range(&self.document),
            selection: txt::Selection {
                range: clone_range(&self.selection.range),
                anchor: self.selection.anchor,
                affinity: self.selection.affinity,
            },
            revision: self.revision,
            composition: self.composition.as_ref().map(clone_range),
            composition_highlight: self.composition_highlight.as_ref().map(clone_range),
            dead_key_highlight: self.dead_key_highlight.as_ref().map(clone_range),
        }
    }
}

impl TryFrom<txt::TextFieldState> for TextFieldState {
    type Error = Error;
    fn try_from(state: txt::TextFieldState) -> Result<Self, Self::Error> {
        let txt::TextFieldState {
            revision,
            selection,
            document,
            composition,
            composition_highlight,
            dead_key_highlight,
        } = state;
        let document = match document {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected document field to be set on TextFieldState"
                )))
            }
        };
        let selection = match selection {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected selection field to be set on TextFieldState"
                )))
            }
        };
        let revision = match revision {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected revision field to be set on TextFieldState"
                )))
            }
        };
        Ok(TextFieldState {
            document,
            selection,
            revision,
            composition,
            composition_highlight,
            dead_key_highlight,
        })
    }
}

impl Into<txt::TextFieldState> for TextFieldState {
    fn into(self) -> txt::TextFieldState {
        let TextFieldState {
            revision,
            selection,
            document,
            composition,
            composition_highlight,
            dead_key_highlight,
        } = self;
        txt::TextFieldState {
            document: Some(document),
            selection: Some(selection),
            revision: Some(revision),
            composition,
            composition_highlight,
            dead_key_highlight,
        }
    }
}

fn clone_range(range: &txt::Range) -> txt::Range {
    txt::Range {
        start: txt::Position { id: range.start.id },
        end: txt::Position { id: range.end.id },
    }
}
