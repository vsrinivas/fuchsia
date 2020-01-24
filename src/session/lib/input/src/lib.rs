// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod testing_utilities;
mod fake_input_device_binding;
mod fake_input_handler;

pub mod input_device;
pub mod keyboard;
pub mod mouse;
pub mod touch;

pub mod ime_handler;
pub mod input_handler;
pub mod mouse_handler;
pub mod shortcut_handler;
pub mod touch_handler;

pub mod input_pipeline;
