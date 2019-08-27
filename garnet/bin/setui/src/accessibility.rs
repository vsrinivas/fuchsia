// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::accessibility_controller::spawn_accessibility_controller;
pub use self::accessibility_fidl_handler::spawn_accessibility_fidl_handler;

mod accessibility_controller;
mod accessibility_fidl_handler;
