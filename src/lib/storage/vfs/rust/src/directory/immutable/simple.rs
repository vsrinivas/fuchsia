// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an implementation of an immutable "simple" pseudo directories.  Use [`simple()`] to
//! construct actual instances.  See [`Simple`] for details.

#[cfg(test)]
mod tests;

use crate::directory::{immutable::connection, simple};

use {fidl_fuchsia_io::INO_UNKNOWN, std::sync::Arc};

pub type Connection = connection::io1::ImmutableConnection;
pub type Simple = simple::Simple<Connection>;

/// Creates an immutable empty "simple" directory.  This directory holds a "static" set of entries,
/// allowing the server to add or remove entries via the
/// [`crate::directory::helper::DirectlyMutable::add_entry()`] and
/// [`crate::directory::helper::DirectlyMutable::remove_entry()`] methods.
///
/// Also see [`crate::directory::immutable::lazy::Lazy`] directory, where the entries are "dynamic" in a
/// sense that a specific listing (and, potentially, the entries themselves) are generated only
/// when requested.
pub fn simple() -> Arc<Simple> {
    Simple::new(false, INO_UNKNOWN)
}

pub fn simple_with_inode(inode: u64) -> Arc<Simple> {
    Simple::new(false, inode)
}
