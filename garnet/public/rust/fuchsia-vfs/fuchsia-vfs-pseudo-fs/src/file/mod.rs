// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Module holding different kinds of pseudo files and their building blocks.

use {
    crate::directory::entry::DirectoryEntry,
    libc::{S_IRUSR, S_IWUSR},
};

pub mod asynchronous;
pub mod simple;

/// A base trait for all the pseudo files.  Most clients will probably just use the DirectoryEntry
/// trait to deal with the pseudo files uniformly.
pub trait PseudoFile: DirectoryEntry {}

/// POSIX emulation layer access attributes set by default for files created with read_only().
pub const DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IRUSR;

/// POSIX emulation layer access attributes set by default for files created with write_only().
pub const DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IWUSR;

/// POSIX emulation layer access attributes set by default for files created with read_write().
pub const DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES: u32 =
    DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES | DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES;

pub mod test_utils;

mod common;
mod connection;
