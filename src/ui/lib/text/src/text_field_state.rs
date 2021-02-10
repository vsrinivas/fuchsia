// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use core::convert::TryFrom;
use fidl_fuchsia_ui_text as txt;

/// A version of txt::TextFieldStateLegacy that does not have mandatory fields wrapped in Options.
/// It also implements Clone.
pub struct TextFieldStateLegacy {
    pub document: txt::Range,
    pub selection: txt::Selection,
    pub revision: u64,
    pub composition: Option<txt::Range>,
    pub composition_highlight: Option<txt::Range>,
    pub dead_key_highlight: Option<txt::Range>,
}

impl Clone for TextFieldStateLegacy {
    fn clone(&self) -> Self {
        TextFieldStateLegacy {
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

impl TryFrom<txt::TextFieldStateLegacy> for TextFieldStateLegacy {
    type Error = Error;
    fn try_from(state: txt::TextFieldStateLegacy) -> Result<Self, Self::Error> {
        let txt::TextFieldStateLegacy {
            revision,
            selection,
            document,
            composition,
            composition_highlight,
            dead_key_highlight,
            ..
        } = state;
        let document = match document {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected document field to be set on TextFieldStateLegacy"
                )))
            }
        };
        let selection = match selection {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected selection field to be set on TextFieldStateLegacy"
                )))
            }
        };
        let revision = match revision {
            Some(v) => v,
            None => {
                return Err(format_err!(format!(
                    "Expected revision field to be set on TextFieldStateLegacy"
                )))
            }
        };
        Ok(TextFieldStateLegacy {
            document,
            selection,
            revision,
            composition,
            composition_highlight,
            dead_key_highlight,
        })
    }
}

impl Into<txt::TextFieldStateLegacy> for TextFieldStateLegacy {
    fn into(self) -> txt::TextFieldStateLegacy {
        let TextFieldStateLegacy {
            revision,
            selection,
            document,
            composition,
            composition_highlight,
            dead_key_highlight,
        } = self;
        txt::TextFieldStateLegacy {
            document: Some(document),
            selection: Some(selection),
            revision: Some(revision),
            composition,
            composition_highlight,
            dead_key_highlight,
            ..txt::TextFieldStateLegacy::EMPTY
        }
    }
}

fn clone_range(range: &txt::Range) -> txt::Range {
    txt::Range {
        start: txt::Position { id: range.start.id },
        end: txt::Position { id: range.end.id },
    }
}
