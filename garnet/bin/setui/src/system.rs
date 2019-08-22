// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::system_controller::spawn_system_controller;
pub use self::system_fidl_handler::spawn_system_fidl_handler;

mod system_controller;
mod system_fidl_handler;
