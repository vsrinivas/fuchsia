// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is an implementation of an immutable "simple" pseudo directories.  Use [`simple()`] to
//! construct actual instances.  See [`Simple`] for details.

#[cfg(test)]
mod tests;

use crate::directory::{
    immutable::connection::ImmutableConnection, simple, traversal_position::AlphabeticalTraversal,
};

use std::sync::Arc;

pub type Connection = ImmutableConnection<AlphabeticalTraversal>;
pub type Simple = simple::Simple<Connection>;

/// Creates an immutable empty "simple" directory.  This directory holds a "static" set of entries,
/// allowing the server to add or remove entries via the [`add_entry`] and [`remove_entry`]
/// methods.
///
/// Also see [`directory::immutable::lazy::Lazy`] directory, where the entries are "dynamic" in a
/// sense that a specific listing (and, potentially, the entries themselves) are generated only
/// when requested.
pub fn simple() -> Arc<Simple> {
    Simple::new(false)
}
