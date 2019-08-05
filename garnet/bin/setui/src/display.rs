// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::display_controller::spawn_display_controller;
pub use self::display_fidl_handler::spawn_display_fidl_handler;

mod display_controller;
mod display_fidl_handler;
