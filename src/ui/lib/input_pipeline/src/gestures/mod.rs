// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod args;
mod click;
mod gesture_arena;
mod inspect_keys;
mod motion;
mod one_finger_button;
mod one_finger_drag;
mod primary_tap;
mod scroll;
mod secondary_tap;

#[cfg(test)]
mod tests;

pub use gesture_arena::make_input_handler as make_touchpad_gestures_handler;
