// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod testing_utilities;
mod fake_input_device_binding;
mod fake_input_handler;
mod utils;

pub mod consumer_controls;
pub mod input_device;
pub mod keyboard;
pub mod mouse;
pub mod touch;

pub mod factory_reset_handler;
pub mod ime_handler;
pub mod input_handler;
pub mod keymap;
pub mod media_buttons_handler;
pub mod mouse_handler;
pub mod shortcut_handler;
pub mod text_settings;
pub mod touch_handler;
pub mod touch_injector_handler;

pub mod focus_listening;
pub mod input_pipeline;

pub use utils::Position;
pub use utils::Size;
