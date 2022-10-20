// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod child_view;
pub mod event;
pub mod utils;
pub mod window;

#[cfg(test)]
mod tests;

pub use {
    child_view::*,
    event::*,
    fidl_fuchsia_input::Key,
    fidl_fuchsia_ui_input3::{
        KeyEvent, KeyEventStatus, KeyEventType, KeyMeaning, KeyboardEvent, Modifiers,
        NonPrintableKey,
    },
    fidl_fuchsia_ui_shortcut2::Shortcut,
    pointer_fusion::*,
    utils::*,
    window::*,
};
