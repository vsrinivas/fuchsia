// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_input as uii;

/// Generates a default `TextInputState`, suitable for tests.
#[cfg(test)]
pub fn default_state() -> uii::TextInputState {
    uii::TextInputState {
        revision: 1,
        text: "".to_string(),
        selection: uii::TextSelection { base: 0, extent: 0, affinity: uii::TextAffinity::Upstream },
        composing: uii::TextRange { start: -1, end: -1 },
    }
}

/// Clones a `TextInputState`.
pub fn clone_state(state: &uii::TextInputState) -> uii::TextInputState {
    uii::TextInputState {
        revision: state.revision,
        text: state.text.clone(),
        selection: uii::TextSelection {
            base: state.selection.base,
            extent: state.selection.extent,
            affinity: state.selection.affinity,
        },
        composing: uii::TextRange { start: state.composing.start, end: state.composing.end },
    }
}

/// Clones a `KeyboardEvent`.
pub fn clone_keyboard_event(ev: &uii::KeyboardEvent) -> uii::KeyboardEvent {
    uii::KeyboardEvent {
        event_time: ev.event_time,
        device_id: ev.device_id,
        phase: ev.phase,
        hid_usage: ev.hid_usage,
        code_point: ev.code_point,
        modifiers: ev.modifiers,
    }
}
