// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_input3::{KeyMeaning, NonPrintableKey};
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use num::ToPrimitive;
use num_derive::{FromPrimitive, ToPrimitive};

/// List of all shortcut actions used by Gazelle window manager.
/// These are used as a shortcut id in [ui_shortcut2::Shortcut::id].
#[derive(FromPrimitive, ToPrimitive)]
pub(crate) enum ShortcutAction {
    FocusNext = 1,
    FocusPrev = 2,
    Close = 3,
}

/// Returns an array of keyboard shortcut definitions.
pub(crate) fn all_shortcuts() -> Vec<ui_shortcut2::Shortcut> {
    vec![
        // Alt+tab to switch focus to next application.
        create_shortcut(
            ShortcutAction::FocusNext.to_u32().unwrap(),
            vec![
                KeyMeaning::NonPrintableKey(NonPrintableKey::Alt),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Tab),
            ],
        ),
        // Shift+Alt+tab to switch focus to previous application.
        create_shortcut(
            ShortcutAction::FocusPrev.to_u32().unwrap(),
            vec![
                KeyMeaning::NonPrintableKey(NonPrintableKey::Shift),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Alt),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Tab),
            ],
        ),
        // Ctrl+Alt+tab to switch focus to next application (for Fuchsia emulator).
        create_shortcut(
            ShortcutAction::FocusNext.to_u32().unwrap(),
            vec![
                KeyMeaning::NonPrintableKey(NonPrintableKey::Control),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Alt),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Tab),
            ],
        ),
        // Shift+Ctrl+Alt+tab to switch focus to previous application (for Fuchsia emulator).
        create_shortcut(
            ShortcutAction::FocusPrev.to_u32().unwrap(),
            vec![
                KeyMeaning::NonPrintableKey(NonPrintableKey::Shift),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Control),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Alt),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Tab),
            ],
        ),
        // Ctrl+Shift+w to close the focused application.
        create_shortcut(
            ShortcutAction::Close.to_u32().unwrap(),
            vec![
                KeyMeaning::NonPrintableKey(NonPrintableKey::Control),
                KeyMeaning::NonPrintableKey(NonPrintableKey::Shift),
                KeyMeaning::Codepoint('w' as u32),
            ],
        ),
    ]
}

fn create_shortcut(id: u32, keys: Vec<KeyMeaning>) -> ui_shortcut2::Shortcut {
    ui_shortcut2::Shortcut {
        id,
        key_meanings: keys,
        options: ui_shortcut2::Options { ..ui_shortcut2::Options::EMPTY },
    }
}
