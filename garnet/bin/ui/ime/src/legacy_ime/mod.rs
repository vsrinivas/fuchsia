// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod handler;
mod position;
mod state;
#[cfg(test)]
mod tests;

pub use handler::Ime;
pub use state::ImeState;

pub const HID_USAGE_KEY_BACKSPACE: u32 = 0x2a;
pub const HID_USAGE_KEY_RIGHT: u32 = 0x4f;
pub const HID_USAGE_KEY_LEFT: u32 = 0x50;
pub const HID_USAGE_KEY_ENTER: u32 = 0x28;
pub const HID_USAGE_KEY_DELETE: u32 = 0x2e;
