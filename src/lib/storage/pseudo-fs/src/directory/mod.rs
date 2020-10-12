// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module hodling different kinds of pseudo directories and their buidling blocks.

#[macro_use]
pub mod test_utils;

#[macro_use]
mod common;
mod connection;
mod traversal_position;
mod watchers;

use libc::S_IRUSR;

pub mod controllable;
pub mod controlled;
pub mod entry;
pub mod lazy;
pub mod simple;

/// POSIX emulation layer access attributes set by default for directories created with empty().
pub const DEFAULT_DIRECTORY_PROTECTION_ATTRIBUTES: u32 = S_IRUSR;
