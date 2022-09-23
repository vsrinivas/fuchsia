// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod testing_utilities;
mod fake_input_device_binding;
mod fake_input_handler;
mod mouse_config;
mod utils;

pub mod consumer_controls_binding;
pub mod input_device;
pub mod keyboard_binding;
pub mod mouse_binding;
pub mod touch_binding;

pub mod autorepeater;
pub mod chromebook_keyboard_handler;
pub mod click_drag_handler;
pub mod dead_keys_handler;
pub mod display_ownership;
pub mod factory_reset_handler;
pub mod ime_handler;
pub mod immersive_mode_shortcut_handler;
pub mod input_handler;
pub mod inspect_handler;
pub mod keyboard_handler;
pub mod keymap_handler;
pub mod light_sensor;
pub use light_sensor::{light_sensor_binding, light_sensor_handler};
pub mod media_buttons_handler;
pub mod modifier_handler;
pub mod mouse_injector_handler;
pub mod pointer_display_scale_handler;
pub mod pointer_sensor_scale_handler;
pub mod shortcut_handler;
pub mod text_settings_handler;
pub mod touch_injector_handler;

pub mod activity;
pub mod focus_listener;
pub mod gestures;
pub mod input_pipeline;

pub use gestures::make_touchpad_gestures_handler;
pub use utils::CursorMessage;
pub use utils::Position;
pub use utils::Size;
