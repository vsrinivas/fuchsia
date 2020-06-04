// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
pub mod constants;

pub use core_macros::{ffx_command, ffx_plugin};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}
