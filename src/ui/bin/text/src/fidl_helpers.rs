// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
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
